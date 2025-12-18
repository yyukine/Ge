#include <ntifs.h>
#include "../../shared/definitions.h"
#include "../Core/config.h"
#include "../Utils/log.h"
#include "../Memory/cr3.h"

extern struct DriverState {
    ULONG AttachedProcessId;
    UINT64 AttachedCR3;
    PEPROCESS AttachedProcess;
    KSPIN_LOCK StateLock;
} g_State;

namespace Process {

NTSTATUS Initialize() {
    LOG_INFO("Process module initialized");
    return STATUS_SUCCESS;
}

NTSTATUS Attach(ULONG processId) {
    if (!processId) {
        return STATUS_INVALID_PARAMETER;
    }

    // Lookup process
    PEPROCESS process = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(processId)), &process);
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to lookup process %lu: 0x%X", processId, status);
        return status;
    }

    // Get base address
    PVOID baseAddress = PsGetProcessSectionBaseAddress(process);
    if (!baseAddress) {
        ObDereferenceObject(process);
        LOG_ERROR("Failed to get base address for process %lu", processId);
        return STATUS_UNSUCCESSFUL;
    }

    // Get CR3
    UINT64 cr3 = 0;
    status = CR3::GetProcessCR3(processId, reinterpret_cast<UINT64>(baseAddress), &cr3);
    if (!NT_SUCCESS(status) || !cr3) {
        ObDereferenceObject(process);
        LOG_ERROR("Failed to get CR3 for process %lu", processId);
        return STATUS_UNSUCCESSFUL;
    }

    // Update state
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_State.StateLock, &oldIrql);

    // Detach from previous process if any
    if (g_State.AttachedProcess) {
        ObDereferenceObject(g_State.AttachedProcess);
    }

    g_State.AttachedProcessId = processId;
    g_State.AttachedCR3 = cr3;
    g_State.AttachedProcess = process; // Keep reference

    KeReleaseSpinLock(&g_State.StateLock, oldIrql);

    LOG_INFO("Attached to process %lu (CR3: 0x%llX)", processId, cr3);
    return STATUS_SUCCESS;
}

VOID Detach() {
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_State.StateLock, &oldIrql);

    if (g_State.AttachedProcess) {
        ObDereferenceObject(g_State.AttachedProcess);
        g_State.AttachedProcess = nullptr;
    }

    g_State.AttachedProcessId = 0;
    g_State.AttachedCR3 = 0;

    KeReleaseSpinLock(&g_State.StateLock, oldIrql);

    LOG_INFO("Detached from process");
}

NTSTATUS GetProcessBase(ULONG processId, UINT64* outBase) {
    if (!processId || !outBase) {
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS process = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(processId)), &process);
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to lookup process %lu", processId);
        return status;
    }

    PVOID baseAddress = PsGetProcessSectionBaseAddress(process);
    ObDereferenceObject(process);

    if (!baseAddress) {
        LOG_ERROR("Failed to get base address for process %lu", processId);
        return STATUS_UNSUCCESSFUL;
    }

    *outBase = reinterpret_cast<UINT64>(baseAddress);
    return STATUS_SUCCESS;
}

// PEB structures (minimal)
typedef struct _PEB_LDR_DATA {
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

// Case-insensitive wide string compare
INT CompareUnicodeStrings(const WCHAR* str1, const WCHAR* str2, BOOLEAN caseInsensitive) {
    if (!str1 || !str2) return -1;

    while (*str1 && *str2) {
        WCHAR c1 = *str1;
        WCHAR c2 = *str2;

        if (caseInsensitive) {
            if (c1 >= L'A' && c1 <= L'Z') c1 += 32;
            if (c2 >= L'A' && c2 <= L'Z') c2 += 32;
        }

        if (c1 != c2) return (c1 < c2) ? -1 : 1;
        
        str1++;
        str2++;
    }

    return (*str1 == *str2) ? 0 : (*str1 ? 1 : -1);
}

// Safe copy from target process
BOOLEAN SafeCopyMemory(PVOID dest, PVOID src, SIZE_T size) {
    __try {
        RtlCopyMemory(dest, src, size);
        return TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

NTSTATUS GetModuleBase(ULONG processId, const WCHAR* moduleName, UINT64* outBase) {
    if (!processId || !moduleName || !outBase) {
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS process = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(processId)), &process);
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to lookup process %lu", processId);
        return status;
    }

    // Attach to target process
    KAPC_STATE apcState;
    KeStackAttachProcess(process, &apcState);

    UINT64 foundBase = 0;

    __try {
        PPEB peb = PsGetProcessPeb(process);
        if (!peb) {
            LOG_ERROR("Failed to get PEB");
            __leave;
        }

        PPEB_LDR_DATA ldr = nullptr;
        if (!SafeCopyMemory(&ldr, reinterpret_cast<PUCHAR>(peb) + 0x18, sizeof(PVOID))) {
            LOG_ERROR("Failed to read PEB.Ldr");
            __leave;
        }

        if (!ldr) {
            LOG_ERROR("PEB.Ldr is NULL");
            __leave;
        }

        PEB_LDR_DATA ldrData = {};
        if (!SafeCopyMemory(&ldrData, ldr, sizeof(PEB_LDR_DATA))) {
            LOG_ERROR("Failed to read LDR data");
            __leave;
        }

        if (!ldrData.Initialized) {
            LOG_ERROR("LDR not initialized");
            __leave;
        }

        // Walk module list
        UINT64 listHead = reinterpret_cast<UINT64>(ldr) + FIELD_OFFSET(PEB_LDR_DATA, InLoadOrderModuleList);
        LIST_ENTRY entry = ldrData.InLoadOrderModuleList;
        UINT64 flink = reinterpret_cast<UINT64>(entry.Flink);

        INT guard = 0;
        while (flink && flink != listHead && guard++ < 0x1000) {
            UINT64 entryAddr = flink - FIELD_OFFSET(LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            
            LDR_DATA_TABLE_ENTRY moduleEntry = {};
            if (!SafeCopyMemory(&moduleEntry, reinterpret_cast<PVOID>(entryAddr), sizeof(LDR_DATA_TABLE_ENTRY))) {
                break;
            }

            if (moduleEntry.BaseDllName.Buffer && moduleEntry.BaseDllName.Length > 0) {
                SIZE_T nameLength = moduleEntry.BaseDllName.Length;
                if (nameLength > MAX_MODULE_NAME_LENGTH * sizeof(WCHAR)) {
                    nameLength = MAX_MODULE_NAME_LENGTH * sizeof(WCHAR);
                }

                WCHAR nameBuffer[MAX_MODULE_NAME_LENGTH + 1] = {};
                if (SafeCopyMemory(nameBuffer, moduleEntry.BaseDllName.Buffer, nameLength)) {
                    nameBuffer[nameLength / sizeof(WCHAR)] = L'\0';

                    if (CompareUnicodeStrings(nameBuffer, moduleName, TRUE) == 0) {
                        foundBase = reinterpret_cast<UINT64>(moduleEntry.DllBase);
                        LOG_INFO("Found module %S at 0x%llX", moduleName, foundBase);
                        break;
                    }
                }
            }

            flink = reinterpret_cast<UINT64>(moduleEntry.InLoadOrderLinks.Flink);
        }

    } __finally {
        KeUnstackDetachProcess(&apcState);
        ObDereferenceObject(process);
    }

    if (foundBase) {
        *outBase = foundBase;
        return STATUS_SUCCESS;
    }

    LOG_WARNING("Module %S not found in process %lu", moduleName, processId);
    return STATUS_NOT_FOUND;
}

} // namespace Process
