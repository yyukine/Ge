#include <ntifs.h>
#include <ntddk.h>
#include "../../shared/definitions.h"
#include "config.h"
#include "../Utils/log.h"
#include "../Memory/cr3.h"

// Forward declarations
namespace Physical {
    NTSTATUS Initialize();
    NTSTATUS ReadVirtual(UINT64 directoryTableBase, PVOID virtualAddress, PVOID buffer, SIZE_T size);
    NTSTATUS WriteVirtual(UINT64 directoryTableBase, PVOID virtualAddress, PVOID buffer, SIZE_T size);
}

namespace Process {
    NTSTATUS Initialize();
    NTSTATUS Attach(ULONG processId);
    VOID Detach();
    NTSTATUS GetProcessBase(ULONG processId, UINT64* outBase);
    NTSTATUS GetModuleBase(ULONG processId, const WCHAR* moduleName, UINT64* outBase);
}

namespace StealthHook {
    NTSTATUS Initialize();
    VOID Cleanup();
}

namespace Hiding {
    NTSTATUS Initialize();
    NTSTATUS HideProcess(PEPROCESS process);
    VOID Cleanup();
}

namespace Cleanup {
    NTSTATUS PerformFullCleanup();
}

namespace Mouse {
    NTSTATUS Initialize();
    NTSTATUS MoveMouse(LONG x, LONG y, USHORT buttonFlags);
    VOID Cleanup();
}

// Global state
struct DriverState {
    ULONG AttachedProcessId;
    UINT64 AttachedCR3;
    PEPROCESS AttachedProcess;
    KSPIN_LOCK StateLock;
};

DriverState g_State = {};

// Device objects
PDEVICE_OBJECT g_DeviceObject = nullptr;
UNICODE_STRING g_DeviceName = {};
UNICODE_STRING g_SymbolicLink = {};

// IOCTL Handlers
NTSTATUS HandleAttach(PIRP irp) {
    auto request = static_cast<AttachRequest*>(irp->AssociatedIrp.SystemBuffer);
    
    if (!VALIDATE_SECURITY_CODE(request->SecurityCode)) {
        LOG_ERROR("Invalid security code in Attach request");
        return STATUS_ACCESS_DENIED;
    }

    if (!VALIDATE_PROCESS_ID(request->ProcessId)) {
        LOG_ERROR("Invalid process ID: %lu", request->ProcessId);
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS status = Process::Attach(request->ProcessId);
    if (NT_SUCCESS(status)) {
        LOG_INFO("Attached to process %lu", request->ProcessId);
    }

    return status;
}

NTSTATUS HandleDetach(PIRP irp) {
    auto request = static_cast<DetachRequest*>(irp->AssociatedIrp.SystemBuffer);
    
    if (!VALIDATE_SECURITY_CODE(request->SecurityCode)) {
        LOG_ERROR("Invalid security code in Detach request");
        return STATUS_ACCESS_DENIED;
    }

    Process::Detach();
    LOG_INFO("Detached from process");
    return STATUS_SUCCESS;
}

NTSTATUS HandleReadWrite(PIRP irp, BOOLEAN isWrite) {
    auto request = static_cast<ReadWriteRequest*>(irp->AssociatedIrp.SystemBuffer);
    
    if (!VALIDATE_SECURITY_CODE(request->SecurityCode)) {
        LOG_ERROR("Invalid security code in R/W request");
        return STATUS_ACCESS_DENIED;
    }

    if (!VALIDATE_ADDRESS(request->Address) || !VALIDATE_SIZE(request->Size)) {
        LOG_ERROR("Invalid address or size");
        return STATUS_INVALID_PARAMETER;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_State.StateLock, &oldIrql);

    if (g_State.AttachedProcessId == 0 || g_State.AttachedCR3 == 0) {
        KeReleaseSpinLock(&g_State.StateLock, oldIrql);
        LOG_ERROR("No process attached");
        return STATUS_INVALID_HANDLE;
    }

    UINT64 cr3 = g_State.AttachedCR3;
    KeReleaseSpinLock(&g_State.StateLock, oldIrql);

    PVOID tempBuffer = ExAllocatePoolTag(NonPagedPool, request->Size);
    if (!tempBuffer) {
        LOG_ERROR("Failed to allocate temp buffer");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NTSTATUS status;
    
    if (isWrite) {
        // Copy from usermode buffer
        __try {
            RtlCopyMemory(tempBuffer, reinterpret_cast<PVOID>(request->Buffer), request->Size);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ExFreePoolTag(tempBuffer);
            return STATUS_ACCESS_VIOLATION;
        }

        status = Physical::WriteVirtual(cr3, reinterpret_cast<PVOID>(request->Address), tempBuffer, request->Size);
    } else {
        status = Physical::ReadVirtual(cr3, reinterpret_cast<PVOID>(request->Address), tempBuffer, request->Size);
        
        if (NT_SUCCESS(status)) {
            // Copy to usermode buffer
            __try {
                RtlCopyMemory(reinterpret_cast<PVOID>(request->Buffer), tempBuffer, request->Size);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ExFreePoolTag(tempBuffer);
                return STATUS_ACCESS_VIOLATION;
            }
        }
    }

    ExFreePoolTag(tempBuffer);
    return status;
}

NTSTATUS HandleGetBase(PIRP irp) {
    auto request = static_cast<GetBaseRequest*>(irp->AssociatedIrp.SystemBuffer);
    
    if (!VALIDATE_SECURITY_CODE(request->SecurityCode)) {
        LOG_ERROR("Invalid security code in GetBase request");
        return STATUS_ACCESS_DENIED;
    }

    UINT64 base = 0;
    NTSTATUS status = Process::GetProcessBase(request->ProcessId, &base);
    
    if (NT_SUCCESS(status)) {
        request->BaseAddress = base;
        LOG_INFO("Got process base for PID %lu: 0x%llX", request->ProcessId, base);
    }

    return status;
}

NTSTATUS HandleGetModule(PIRP irp) {
    auto request = static_cast<GetModuleRequest*>(irp->AssociatedIrp.SystemBuffer);
    
    if (!VALIDATE_SECURITY_CODE(request->SecurityCode)) {
        LOG_ERROR("Invalid security code in GetModule request");
        return STATUS_ACCESS_DENIED;
    }

    UINT64 moduleBase = 0;
    NTSTATUS status = Process::GetModuleBase(request->ProcessId, request->ModuleName, &moduleBase);
    
    if (NT_SUCCESS(status)) {
        request->ModuleBase = moduleBase;
        LOG_INFO("Got module base: 0x%llX", moduleBase);
    }

    return status;
}

NTSTATUS HandleGetCR3(PIRP irp) {
    auto request = static_cast<GetCR3Request*>(irp->AssociatedIrp.SystemBuffer);
    
    if (!VALIDATE_SECURITY_CODE(request->SecurityCode)) {
        LOG_ERROR("Invalid security code in GetCR3 request");
        return STATUS_ACCESS_DENIED;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_State.StateLock, &oldIrql);

    if (g_State.AttachedProcessId == request->ProcessId && g_State.AttachedCR3 != 0) {
        request->CR3 = g_State.AttachedCR3;
        KeReleaseSpinLock(&g_State.StateLock, oldIrql);
        return STATUS_SUCCESS;
    }

    KeReleaseSpinLock(&g_State.StateLock, oldIrql);

    // Get base address first
    UINT64 baseAddress = 0;
    NTSTATUS status = Process::GetProcessBase(request->ProcessId, &baseAddress);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Get CR3
    UINT64 cr3 = 0;
    status = CR3::GetProcessCR3(request->ProcessId, baseAddress, &cr3);
    if (NT_SUCCESS(status)) {
        request->CR3 = cr3;
        LOG_INFO("Got CR3 for PID %lu: 0x%llX", request->ProcessId, cr3);
    }

    return status;
}

// Device control handler
NTSTATUS DeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {
    UNREFERENCED_PARAMETER(deviceObject);

    PIO_STACK_LOCATION stackLocation = IoGetCurrentIrpStackLocation(irp);
    ULONG controlCode = stackLocation->Parameters.DeviceIoControl.IoControlCode;
    ULONG inputLength = stackLocation->Parameters.DeviceIoControl.InputBufferLength;
    
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG bytesReturned = 0;

    switch (controlCode) {
        case IOCTL_ATTACH:
            if (inputLength >= sizeof(AttachRequest)) {
                status = HandleAttach(irp);
                bytesReturned = sizeof(AttachRequest);
            }
            break;

        case IOCTL_DETACH:
            if (inputLength >= sizeof(DetachRequest)) {
                status = HandleDetach(irp);
                bytesReturned = sizeof(DetachRequest);
            }
            break;

        case IOCTL_READ_MEMORY:
            if (inputLength >= sizeof(ReadWriteRequest)) {
                status = HandleReadWrite(irp, FALSE);
                bytesReturned = sizeof(ReadWriteRequest);
            }
            break;

        case IOCTL_WRITE_MEMORY:
            if (inputLength >= sizeof(ReadWriteRequest)) {
                status = HandleReadWrite(irp, TRUE);
                bytesReturned = sizeof(ReadWriteRequest);
            }
            break;

        case IOCTL_GET_BASE:
            if (inputLength >= sizeof(GetBaseRequest)) {
                status = HandleGetBase(irp);
                bytesReturned = sizeof(GetBaseRequest);
            }
            break;

        case IOCTL_GET_MODULE:
            if (inputLength >= sizeof(GetModuleRequest)) {
                status = HandleGetModule(irp);
                bytesReturned = sizeof(GetModuleRequest);
            }
            break;

        case IOCTL_GET_CR3:
            if (inputLength >= sizeof(GetCR3Request)) {
                status = HandleGetCR3(irp);
                bytesReturned = sizeof(GetCR3Request);
            }
            break;

        default:
            LOG_WARNING("Unknown IOCTL: 0x%X", controlCode);
            break;
    }

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

// Create/Close handler
NTSTATUS CreateClose(PDEVICE_OBJECT deviceObject, PIRP irp) {
    UNREFERENCED_PARAMETER(deviceObject);
    
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// Unsupported dispatch
NTSTATUS UnsupportedDispatch(PDEVICE_OBJECT deviceObject, PIRP irp) {
    UNREFERENCED_PARAMETER(deviceObject);
    
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
}

// Driver unload
VOID DriverUnload(PDRIVER_OBJECT driverObject) {
    UNREFERENCED_PARAMETER(driverObject);
    
    LOG_INFO("Driver unloading...");

    // Cleanup modules in reverse order
    Mouse::Cleanup();
    Hiding::Cleanup();
    Process::Detach();
    StealthHook::Cleanup();

    if (g_DeviceObject) {
        if (g_SymbolicLink.Buffer) {
            IoDeleteSymbolicLink(&g_SymbolicLink);
        }
        IoDeleteDevice(g_DeviceObject);
    }

    LOG_INFO("Driver unloaded successfully");
}

// Driver entry
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
    UNREFERENCED_PARAMETER(registryPath);

    LOG_INFO("=== UndetectedDriver Loading ===");

    // Initialize state
    KeInitializeSpinLock(&g_State.StateLock);
    g_State.AttachedProcessId = 0;
    g_State.AttachedCR3 = 0;
    g_State.AttachedProcess = nullptr;

    // Initialize CR3 system
    NTSTATUS status = CR3::Initialize();
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to initialize CR3 system: 0x%X", status);
        return status;
    }

    // Initialize physical memory
    status = Physical::Initialize();
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to initialize physical memory: 0x%X", status);
        return status;
    }

    // Initialize process functions
    status = Process::Initialize();
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to initialize process functions: 0x%X", status);
        return status;
    }

    // Initialize hiding module
    status = Hiding::Initialize();
    if (!NT_SUCCESS(status)) {
        LOG_WARNING("Failed to initialize hiding module: 0x%X (non-critical)", status);
    }

    // Initialize mouse injection
    status = Mouse::Initialize();
    if (!NT_SUCCESS(status)) {
        LOG_WARNING("Failed to initialize mouse module: 0x%X (non-critical)", status);
    }

    // Create device
    RtlInitUnicodeString(&g_DeviceName, DEVICE_NAME_STRING);
    status = IoCreateDevice(
        driverObject,
        0,
        &g_DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_DeviceObject
    );

    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to create device: 0x%X", status);
        return status;
    }

    // Create symbolic link
    RtlInitUnicodeString(&g_SymbolicLink, SYMBOLIC_LINK_STRING);
    status = IoCreateSymbolicLink(&g_SymbolicLink, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to create symbolic link: 0x%X", status);
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    // Set dispatch functions
    for (ULONG i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        driverObject->MajorFunction[i] = UnsupportedDispatch;
    }

    driverObject->MajorFunction[IRP_MJ_CREATE] = CreateClose;
    driverObject->MajorFunction[IRP_MJ_CLOSE] = CreateClose;
    driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;
    driverObject->DriverUnload = DriverUnload;

    g_DeviceObject->Flags |= DO_BUFFERED_IO;
    g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

#if USE_OBCREATEOBJECT_HOOK
    // Initialize stealth hook (critical for anti-detection)
    status = StealthHook::Initialize();
    if (!NT_SUCCESS(status)) {
        LOG_WARNING("Failed to initialize stealth hook: 0x%X (continuing with IOCTL)", status);
    } else {
        LOG_INFO("Stealth communication active (ObCreateObject hook)");
    }
#endif

#if ENABLE_DRIVER_CLEANING
    // Clean traces after initialization
    KeDelayExecutionThread(KernelMode, FALSE, &(LARGE_INTEGER){ .QuadPart = -10000000LL }); // 1 second delay
    status = Cleanup::PerformFullCleanup();
    if (NT_SUCCESS(status)) {
        LOG_INFO("Driver traces cleaned successfully");
    }
#endif

    LOG_INFO("=== UndetectedDriver Loaded Successfully ===");
    LOG_INFO("Mode: %s", USE_OBCREATEOBJECT_HOOK ? "STEALTH (Hooked)" : "IOCTL (Standard)");
    LOG_INFO("Status: UNDETECTED");
    return STATUS_SUCCESS;
}
