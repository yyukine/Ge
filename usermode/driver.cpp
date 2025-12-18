#include "driver.h"
#include <TlHelp32.h>
#include <iostream>

Driver::Driver() : m_Handle(INVALID_HANDLE_VALUE), m_AttachedPid(0), m_LastError(0) {
}

Driver::~Driver() {
    Disconnect();
}

bool Driver::Connect(const std::wstring& symbolicLink) {
    if (IsConnected()) {
        return true;
    }

    m_Handle = CreateFileW(
        symbolicLink.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (m_Handle == INVALID_HANDLE_VALUE) {
        m_LastError = ::GetLastError();
        return false;
    }

    return true;
}

void Driver::Disconnect() {
    if (IsConnected()) {
        Detach();
        CloseHandle(m_Handle);
        m_Handle = INVALID_HANDLE_VALUE;
    }
}

bool Driver::SendIoctl(ULONG controlCode, PVOID inputBuffer, ULONG inputSize, PVOID outputBuffer, ULONG outputSize) {
    if (!IsConnected()) {
        m_LastError = ERROR_INVALID_HANDLE;
        return false;
    }

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        m_Handle,
        controlCode,
        inputBuffer,
        inputSize,
        outputBuffer,
        outputSize,
        &bytesReturned,
        nullptr
    );

    if (!result) {
        m_LastError = ::GetLastError();
        return false;
    }

    return true;
}

bool Driver::Attach(ULONG processId) {
    if (!IsConnected()) {
        return false;
    }

    AttachRequest request = {};
    request.SecurityCode = SECURITY_CODE;
    request.ProcessId = processId;

    if (!SendIoctl(IOCTL_ATTACH, &request, sizeof(request), &request, sizeof(request))) {
        return false;
    }

    m_AttachedPid = processId;
    return true;
}

void Driver::Detach() {
    if (!IsConnected() || m_AttachedPid == 0) {
        return;
    }

    DetachRequest request = {};
    request.SecurityCode = SECURITY_CODE;

    SendIoctl(IOCTL_DETACH, &request, sizeof(request), &request, sizeof(request));
    m_AttachedPid = 0;
}

ULONGLONG Driver::GetProcessBase(ULONG processId) {
    if (!IsConnected()) {
        return 0;
    }

    GetBaseRequest request = {};
    request.SecurityCode = SECURITY_CODE;
    request.ProcessId = processId;
    request.BaseAddress = 0;

    if (!SendIoctl(IOCTL_GET_BASE, &request, sizeof(request), &request, sizeof(request))) {
        return 0;
    }

    return request.BaseAddress;
}

ULONGLONG Driver::GetModuleBase(ULONG processId, const std::wstring& moduleName) {
    if (!IsConnected()) {
        return 0;
    }

    GetModuleRequest request = {};
    request.SecurityCode = SECURITY_CODE;
    request.ProcessId = processId;
    request.ModuleBase = 0;

    // Copy module name
    wcsncpy_s(request.ModuleName, moduleName.c_str(), _TRUNCATE);

    if (!SendIoctl(IOCTL_GET_MODULE, &request, sizeof(request), &request, sizeof(request))) {
        return 0;
    }

    return request.ModuleBase;
}

ULONGLONG Driver::GetCR3(ULONG processId) {
    if (!IsConnected()) {
        return 0;
    }

    GetCR3Request request = {};
    request.SecurityCode = SECURITY_CODE;
    request.ProcessId = processId;
    request.CR3 = 0;

    if (!SendIoctl(IOCTL_GET_CR3, &request, sizeof(request), &request, sizeof(request))) {
        return 0;
    }

    return request.CR3;
}

bool Driver::ReadMemory(ULONGLONG address, PVOID buffer, SIZE_T size) {
    if (!IsConnected() || !buffer || size == 0) {
        return false;
    }

    ReadWriteRequest request = {};
    request.SecurityCode = SECURITY_CODE;
    request.ProcessId = m_AttachedPid;
    request.Address = address;
    request.Buffer = reinterpret_cast<ULONGLONG>(buffer);
    request.Size = size;
    request.Write = FALSE;

    return SendIoctl(IOCTL_READ_MEMORY, &request, sizeof(request), &request, sizeof(request));
}

bool Driver::WriteMemory(ULONGLONG address, PVOID buffer, SIZE_T size) {
    if (!IsConnected() || !buffer || size == 0) {
        return false;
    }

    ReadWriteRequest request = {};
    request.SecurityCode = SECURITY_CODE;
    request.ProcessId = m_AttachedPid;
    request.Address = address;
    request.Buffer = reinterpret_cast<ULONGLONG>(buffer);
    request.Size = size;
    request.Write = TRUE;

    return SendIoctl(IOCTL_WRITE_MEMORY, &request, sizeof(request), &request, sizeof(request));
}

bool Driver::ReadString(ULONGLONG address, std::string& outString, SIZE_T maxLength) {
    std::vector<char> buffer(maxLength + 1, 0);
    
    if (!ReadMemory(address, buffer.data(), maxLength)) {
        return false;
    }

    buffer[maxLength] = '\0';
    outString = buffer.data();
    return true;
}

bool Driver::ReadWString(ULONGLONG address, std::wstring& outString, SIZE_T maxLength) {
    std::vector<wchar_t> buffer(maxLength + 1, 0);
    
    if (!ReadMemory(address, buffer.data(), maxLength * sizeof(wchar_t))) {
        return false;
    }

    buffer[maxLength] = L'\0';
    outString = buffer.data();
    return true;
}

ULONG Driver::FindProcessId(const std::wstring& processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            std::wstring exeFile = entry.szExeFile;
            
            // Convert both to lowercase for case-insensitive comparison
            std::transform(exeFile.begin(), exeFile.end(), exeFile.begin(), ::towlower);
            std::wstring searchName = processName;
            std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::towlower);

            if (exeFile == searchName) {
                CloseHandle(snapshot);
                return entry.th32ProcessID;
            }

        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return 0;
}
