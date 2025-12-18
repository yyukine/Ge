#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include "../shared/definitions.h"

class Driver {
public:
    Driver();
    ~Driver();

    // Connection
    bool Connect(const std::wstring& symbolicLink = L"\\\\.\\{A7B2C9E1-4F3D-8A5E-B2C4-9D7E6F1A8B3C}");
    void Disconnect();
    bool IsConnected() const { return m_Handle != INVALID_HANDLE_VALUE; }

    // Process operations
    bool Attach(ULONG processId);
    void Detach();
    ULONGLONG GetProcessBase(ULONG processId);
    ULONGLONG GetModuleBase(ULONG processId, const std::wstring& moduleName);
    ULONGLONG GetCR3(ULONG processId);

    // Memory operations
    template<typename T>
    T Read(ULONGLONG address) {
        T buffer = {};
        ReadMemory(address, &buffer, sizeof(T));
        return buffer;
    }

    template<typename T>
    bool Write(ULONGLONG address, const T& value) {
        return WriteMemory(address, const_cast<T*>(&value), sizeof(T));
    }

    bool ReadMemory(ULONGLONG address, PVOID buffer, SIZE_T size);
    bool WriteMemory(ULONGLONG address, PVOID buffer, SIZE_T size);

    // Advanced operations
    bool ReadString(ULONGLONG address, std::string& outString, SIZE_T maxLength = 256);
    bool ReadWString(ULONGLONG address, std::wstring& outString, SIZE_T maxLength = 256);
    
    template<typename T>
    std::vector<T> ReadArray(ULONGLONG address, SIZE_T count) {
        std::vector<T> result(count);
        if (ReadMemory(address, result.data(), count * sizeof(T))) {
            return result;
        }
        return {};
    }

    // Utility
    ULONG FindProcessId(const std::wstring& processName);
    
    // Status
    DWORD GetLastError() const { return m_LastError; }

private:
    bool SendIoctl(ULONG controlCode, PVOID inputBuffer, ULONG inputSize, PVOID outputBuffer, ULONG outputSize);

    HANDLE m_Handle;
    ULONG m_AttachedPid;
    DWORD m_LastError;
};
