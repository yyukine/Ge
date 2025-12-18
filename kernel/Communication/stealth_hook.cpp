#include <ntifs.h>
#include "../../shared/definitions.h"
#include "../Core/config.h"
#include "../Utils/log.h"

namespace StealthHook {

// Hook state
typedef NTSTATUS(*OriginalIrpHandler_t)(PDEVICE_OBJECT, PIRP);
OriginalIrpHandler_t g_OriginalHandler = nullptr;
PDRIVER_OBJECT g_TargetDriver = nullptr;
PDEVICE_OBJECT g_TargetDevice = nullptr;
BOOLEAN g_HookActive = FALSE;

// Shared memory for communication
PVOID g_SharedMemory = nullptr;
ULONG g_SharedMemorySize = sizeof(HookedRequest);

extern "C" POBJECT_TYPE* IoDriverObjectType;

extern "C" NTSTATUS ObReferenceObjectByName(
    PUNICODE_STRING ObjectName,
    ULONG Attributes,
    PACCESS_STATE PassedAccessState,
    ACCESS_MASK DesiredAccess,
    POBJECT_TYPE ObjectType,
    KPROCESSOR_MODE AccessMode,
    PVOID ParseContext,
    PVOID* Object
);

// Forward declarations
namespace Process {
    NTSTATUS Attach(ULONG processId);
    VOID Detach();
    NTSTATUS GetProcessBase(ULONG processId, UINT64* outBase);
    NTSTATUS GetModuleBase(ULONG processId, const WCHAR* moduleName, UINT64* outBase);
}

namespace Physical {
    NTSTATUS ReadVirtual(UINT64 cr3, PVOID va, PVOID buffer, SIZE_T size);
    NTSTATUS WriteVirtual(UINT64 cr3, PVOID va, PVOID buffer, SIZE_T size);
}

namespace CR3 {
    NTSTATUS GetProcessCR3(ULONG pid, UINT64 base, UINT64* outCR3);
}

extern struct DriverState {
    ULONG AttachedProcessId;
    UINT64 AttachedCR3;
    PEPROCESS AttachedProcess;
    KSPIN_LOCK StateLock;
} g_State;

// Process hooked requests
NTSTATUS ProcessHookedRequest(HookedRequest* request) {
    if (!request) {
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS status = STATUS_SUCCESS;

    switch (request->Type) {
        case RequestType::Attach: {
            if (!VALIDATE_SECURITY_CODE(request->Attach.SecurityCode)) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            status = Process::Attach(request->Attach.ProcessId);
            break;
        }

        case RequestType::Detach: {
            if (!VALIDATE_SECURITY_CODE(request->Detach.SecurityCode)) {
                status = STATUS_ACCESS_DENIED;
                break;
            }
            Process::Detach();
            break;
        }

        case RequestType::ReadMemory:
        case RequestType::WriteMemory: {
            auto& rw = request->ReadWrite;
            
            if (!VALIDATE_SECURITY_CODE(rw.SecurityCode)) {
                status = STATUS_ACCESS_DENIED;
                break;
            }

            if (!VALIDATE_ADDRESS(rw.Address) || !VALIDATE_SIZE(rw.Size)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            KIRQL oldIrql;
            KeAcquireSpinLock(&g_State.StateLock, &oldIrql);

            if (g_State.AttachedCR3 == 0) {
                KeReleaseSpinLock(&g_State.StateLock, oldIrql);
                status = STATUS_INVALID_HANDLE;
                break;
            }

            UINT64 cr3 = g_State.AttachedCR3;
            KeReleaseSpinLock(&g_State.StateLock, oldIrql);

            PVOID tempBuffer = ExAllocatePoolWithTag(NonPagedPool, rw.Size, POOL_TAG_BUFFER);
            if (!tempBuffer) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            if (rw.Write) {
                __try {
                    RtlCopyMemory(tempBuffer, reinterpret_cast<PVOID>(rw.Buffer), rw.Size);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    ExFreePoolWithTag(tempBuffer, POOL_TAG_BUFFER);
                    status = STATUS_ACCESS_VIOLATION;
                    break;
                }

                status = Physical::WriteVirtual(cr3, reinterpret_cast<PVOID>(rw.Address), 
                                                tempBuffer, rw.Size);
            } else {
                status = Physical::ReadVirtual(cr3, reinterpret_cast<PVOID>(rw.Address), 
                                               tempBuffer, rw.Size);
                
                if (NT_SUCCESS(status)) {
                    __try {
                        RtlCopyMemory(reinterpret_cast<PVOID>(rw.Buffer), tempBuffer, rw.Size);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        status = STATUS_ACCESS_VIOLATION;
                    }
                }
            }

            ExFreePoolWithTag(tempBuffer, POOL_TAG_BUFFER);
            break;
        }

        case RequestType::GetBase: {
            if (!VALIDATE_SECURITY_CODE(request->GetBase.SecurityCode)) {
                status = STATUS_ACCESS_DENIED;
                break;
            }

            UINT64 base = 0;
            status = Process::GetProcessBase(request->GetBase.ProcessId, &base);
            if (NT_SUCCESS(status)) {
                request->GetBase.BaseAddress = base;
            }
            break;
        }

        case RequestType::GetModule: {
            if (!VALIDATE_SECURITY_CODE(request->GetModule.SecurityCode)) {
                status = STATUS_ACCESS_DENIED;
                break;
            }

            UINT64 moduleBase = 0;
            status = Process::GetModuleBase(
                request->GetModule.ProcessId,
                request->GetModule.ModuleName,
                &moduleBase
            );
            
            if (NT_SUCCESS(status)) {
                request->GetModule.ModuleBase = moduleBase;
            }
            break;
        }

        case RequestType::GetCR3: {
            if (!VALIDATE_SECURITY_CODE(request->GetCR3.SecurityCode)) {
                status = STATUS_ACCESS_DENIED;
                break;
            }

            UINT64 base = 0;
            NTSTATUS st = Process::GetProcessBase(request->GetCR3.ProcessId, &base);
            if (!NT_SUCCESS(st)) {
                status = st;
                break;
            }

            UINT64 cr3 = 0;
            status = CR3::GetProcessCR3(request->GetCR3.ProcessId, base, &cr3);
            if (NT_SUCCESS(status)) {
                request->GetCR3.CR3 = cr3;
            }
            break;
        }

        default:
            status = STATUS_INVALID_PARAMETER;
            break;
    }

    request->Status = status;
    return status;
}

// Hooked IRP handler
NTSTATUS HookedIrpHandler(PDEVICE_OBJECT deviceObject, PIRP irp) {
    // Check if this is our request
    PIO_STACK_LOCATION stackLocation = IoGetCurrentIrpStackLocation(irp);
    
    if (stackLocation->MajorFunction == IRP_MJ_DEVICE_CONTROL) {
        PVOID systemBuffer = irp->AssociatedIrp.SystemBuffer;
        ULONG inputLength = stackLocation->Parameters.DeviceIoControl.InputBufferLength;

        // Check if this looks like our request
        if (systemBuffer && inputLength >= sizeof(HookedRequest)) {
            HookedRequest* request = static_cast<HookedRequest*>(systemBuffer);

            // Validate request type
            if (static_cast<ULONG>(request->Type) >= static_cast<ULONG>(RequestType::Attach) &&
                static_cast<ULONG>(request->Type) <= static_cast<ULONG>(RequestType::FreeMemory)) {
                
                // This is our request - process it
                NTSTATUS status = ProcessHookedRequest(request);
                
                irp->IoStatus.Status = status;
                irp->IoStatus.Information = sizeof(HookedRequest);
                IoCompleteRequest(irp, IO_NO_INCREMENT);
                
                return status;
            }
        }
    }

    // Not our request - pass to original handler
    if (g_OriginalHandler) {
        return g_OriginalHandler(deviceObject, irp);
    }

    // Fallback
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Initialize() {
#if !USE_OBCREATEOBJECT_HOOK
    LOG_INFO("Stealth hook disabled in config");
    return STATUS_SUCCESS;
#endif

    if (g_HookActive) {
        LOG_WARNING("Hook already active");
        return STATUS_ALREADY_COMMITTED;
    }

    // Get target driver
    UNICODE_STRING driverName;
    RtlInitUnicodeString(&driverName, TARGET_DRIVER_PATH);

    NTSTATUS status = ObReferenceObjectByName(
        &driverName,
        OBJ_CASE_INSENSITIVE,
        nullptr,
        0,
        *IoDriverObjectType,
        KernelMode,
        nullptr,
        reinterpret_cast<PVOID*>(&g_TargetDriver)
    );

    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to reference target driver: 0x%X", status);
        return status;
    }

    // Get target device
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, TARGET_DEVICE_PATH);

    PFILE_OBJECT fileObject = nullptr;
    status = IoGetDeviceObjectPointer(&deviceName, FILE_READ_DATA, &fileObject, &g_TargetDevice);
    
    if (fileObject) {
        ObDereferenceObject(fileObject);
    }

    if (!NT_SUCCESS(status) || !g_TargetDevice) {
        ObDereferenceObject(g_TargetDriver);
        g_TargetDriver = nullptr;
        LOG_ERROR("Failed to get target device: 0x%X", status);
        return status;
    }

    // Hook the IRP handler
    g_OriginalHandler = reinterpret_cast<OriginalIrpHandler_t>(
        InterlockedExchangePointer(
            reinterpret_cast<PVOID*>(&g_TargetDriver->MajorFunction[HOOK_IRP_FUNCTION]),
            reinterpret_cast<PVOID>(HookedIrpHandler)
        )
    );

    if (!g_OriginalHandler) {
        LOG_ERROR("Failed to hook IRP handler");
        ObDereferenceObject(g_TargetDriver);
        g_TargetDriver = nullptr;
        g_TargetDevice = nullptr;
        return STATUS_UNSUCCESSFUL;
    }

    g_HookActive = TRUE;
    LOG_INFO("Stealth hook installed successfully on %S", TARGET_DRIVER_PATH);
    LOG_INFO("Original handler: 0x%p", g_OriginalHandler);

    return STATUS_SUCCESS;
}

VOID Cleanup() {
#if !USE_OBCREATEOBJECT_HOOK
    return;
#endif

    if (g_HookActive && g_OriginalHandler && g_TargetDriver) {
        // Restore original handler
        InterlockedExchangePointer(
            reinterpret_cast<PVOID*>(&g_TargetDriver->MajorFunction[HOOK_IRP_FUNCTION]),
            reinterpret_cast<PVOID>(g_OriginalHandler)
        );

        LOG_INFO("Stealth hook removed");
    }

    if (g_TargetDriver) {
        ObDereferenceObject(g_TargetDriver);
        g_TargetDriver = nullptr;
    }

    g_TargetDevice = nullptr;
    g_OriginalHandler = nullptr;
    g_HookActive = FALSE;
}

} // namespace StealthHook
