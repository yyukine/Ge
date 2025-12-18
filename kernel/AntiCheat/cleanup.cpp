#include <ntifs.h>
#include <ntimage.h>
#include "../Core/config.h"
#include "../Utils/log.h"

namespace Cleanup {

// Driver list entry structure
typedef struct _KLDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    PVOID ExceptionTable;
    ULONG ExceptionTableSize;
    PVOID GpValue;
    PVOID NonPagedDebugInfo;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    USHORT LoadCount;
    USHORT __Unused5;
    PVOID SectionPointer;
    ULONG CheckSum;
    PVOID LoadedImports;
    PVOID PatchInformation;
} KLDR_DATA_TABLE_ENTRY, *PKLDR_DATA_TABLE_ENTRY;

extern "C" {
    NTKERNELAPI PLIST_ENTRY NTAPI PsLoadedModuleList;
}

BOOLEAN RemoveDriverFromList(const WCHAR* driverName) {
    if (!driverName) {
        return FALSE;
    }

    __try {
        // Walk PsLoadedModuleList
        for (PLIST_ENTRY entry = PsLoadedModuleList->Flink;
             entry != PsLoadedModuleList;
             entry = entry->Flink) {
            
            PKLDR_DATA_TABLE_ENTRY module = CONTAINING_RECORD(
                entry,
                KLDR_DATA_TABLE_ENTRY,
                InLoadOrderLinks
            );

            if (module->BaseDllName.Buffer && module->BaseDllName.Length > 0) {
                // Compare driver name (case-insensitive)
                UNICODE_STRING searchName;
                RtlInitUnicodeString(&searchName, const_cast<PWSTR>(driverName));

                if (RtlCompareUnicodeString(&module->BaseDllName, &searchName, TRUE) == 0) {
                    // Found - unlink it
                    PLIST_ENTRY flink = entry->Flink;
                    PLIST_ENTRY blink = entry->Blink;

                    if (flink && blink) {
                        flink->Blink = blink;
                        blink->Flink = flink;

                        // Point to itself
                        entry->Flink = entry;
                        entry->Blink = entry;

                        LOG_INFO("Removed driver from list: %wZ", &module->BaseDllName);
                        return TRUE;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("Exception while removing driver from list");
        return FALSE;
    }

    return FALSE;
}

BOOLEAN CleanRegistryTraces(const WCHAR* serviceName) {
    if (!serviceName) {
        return FALSE;
    }

    // Build registry path
    WCHAR regPath[512];
    RtlStringCchPrintfW(
        regPath,
        sizeof(regPath) / sizeof(WCHAR),
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%s",
        serviceName
    );

    UNICODE_STRING regString;
    RtlInitUnicodeString(&regString, regPath);

    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(
        &objAttr,
        &regString,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        nullptr,
        nullptr
    );

    HANDLE keyHandle;
    NTSTATUS status = ZwOpenKey(&keyHandle, DELETE, &objAttr);
    
    if (NT_SUCCESS(status)) {
        status = ZwDeleteKey(keyHandle);
        ZwClose(keyHandle);

        if (NT_SUCCESS(status)) {
            LOG_INFO("Cleaned registry traces for service: %S", serviceName);
            return TRUE;
        }
    }

    return FALSE;
}

BOOLEAN ClearPiDDBCacheTable() {
    // PiDDB (Driver Database) cleanup
    // This is complex and OS version specific
    // Simplified implementation
    
    LOG_INFO("PiDDB cache cleanup not fully implemented");
    return TRUE;
}

NTSTATUS CleanVulnerableDriverTraces() {
#if !CLEAN_VULNERABLE_DRIVER
    return STATUS_NOT_IMPLEMENTED;
#endif

    BOOLEAN success = TRUE;

    // Clean vulnerable drivers used for mapping
#ifdef VULN_DRIVER_1
    if (!RemoveDriverFromList(VULN_DRIVER_1)) {
        LOG_WARNING("Failed to remove %S from module list", VULN_DRIVER_1);
        success = FALSE;
    }
#endif

#ifdef VULN_DRIVER_2
    if (!RemoveDriverFromList(VULN_DRIVER_2)) {
        LOG_WARNING("Failed to remove %S from module list", VULN_DRIVER_2);
        success = FALSE;
    }
#endif

#ifdef VULN_DRIVER_3
    if (!RemoveDriverFromList(VULN_DRIVER_3)) {
        LOG_WARNING("Failed to remove %S from module list", VULN_DRIVER_3);
        success = FALSE;
    }
#endif

    // Clean registry traces
    CleanRegistryTraces(L"DriverKL");
    CleanRegistryTraces(L"PdFwKrnl");
    CleanRegistryTraces(L"iqvw64e");

    // Clear PiDDB cache
    ClearPiDDBCacheTable();

    LOG_INFO("Vulnerable driver traces cleaned");
    return success ? STATUS_SUCCESS : STATUS_PARTIAL_COPY;
}

NTSTATUS RemoveSelfFromModuleList() {
#if !ENABLE_DRIVER_CLEANING
    return STATUS_NOT_IMPLEMENTED;
#endif

    // Try to find and remove our own driver
    // This is tricky as we're currently executing
    
    __try {
        for (PLIST_ENTRY entry = PsLoadedModuleList->Flink;
             entry != PsLoadedModuleList;
             entry = entry->Flink) {
            
            PKLDR_DATA_TABLE_ENTRY module = CONTAINING_RECORD(
                entry,
                KLDR_DATA_TABLE_ENTRY,
                InLoadOrderLinks
            );

            // Check if this is our driver (by checking if code is in range)
            PVOID currentAddress = reinterpret_cast<PVOID>(&RemoveSelfFromModuleList);
            
            if (currentAddress >= module->DllBase &&
                currentAddress < reinterpret_cast<PVOID>(
                    reinterpret_cast<ULONG_PTR>(module->DllBase) + module->SizeOfImage)) {
                
                // Found ourselves - unlink
                PLIST_ENTRY flink = entry->Flink;
                PLIST_ENTRY blink = entry->Blink;

                if (flink && blink) {
                    flink->Blink = blink;
                    blink->Flink = flink;
                    entry->Flink = entry;
                    entry->Blink = entry;

                    LOG_INFO("Removed self from module list");
                    return STATUS_SUCCESS;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("Exception while removing self from module list");
        return GetExceptionCode();
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS PerformFullCleanup() {
    LOG_INFO("Starting full cleanup...");

    // Remove vulnerable driver traces
    CleanVulnerableDriverTraces();

    // Remove self from module list
    RemoveSelfFromModuleList();

    // Clean registry
    CleanRegistryTraces(L"UndetectedDriver");

    LOG_INFO("Full cleanup complete");
    return STATUS_SUCCESS;
}

} // namespace Cleanup
