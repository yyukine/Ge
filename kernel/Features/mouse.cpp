#include <ntifs.h>
#include <ntddmou.h>
#include "../Core/config.h"
#include "../Utils/log.h"

namespace Mouse {

// Mouse callback function pointer
typedef VOID(*PMOUSE_SERVICE_CALLBACK)(
    PDEVICE_OBJECT DeviceObject,
    PMOUSE_INPUT_DATA InputDataStart,
    PMOUSE_INPUT_DATA InputDataEnd,
    PULONG InputDataConsumed
);

// Global state
PDEVICE_OBJECT g_MouseDevice = nullptr;
PMOUSE_SERVICE_CALLBACK g_MouseCallback = nullptr;
BOOLEAN g_Initialized = FALSE;

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

NTSTATUS Initialize() {
#if !SUPPORT_MOUSE_INJECTION
    return STATUS_NOT_IMPLEMENTED;
#endif

    if (g_Initialized) {
        return STATUS_SUCCESS;
    }

    // Get mouse class driver
    UNICODE_STRING mouseClass;
    RtlInitUnicodeString(&mouseClass, L"\\Driver\\MouClass");

    PDRIVER_OBJECT mouseDriver = nullptr;
    NTSTATUS status = ObReferenceObjectByName(
        &mouseClass,
        OBJ_CASE_INSENSITIVE,
        nullptr,
        0,
        *IoDriverObjectType,
        KernelMode,
        nullptr,
        reinterpret_cast<PVOID*>(&mouseDriver)
    );

    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to reference MouClass driver: 0x%X", status);
        return status;
    }

    // Get mouse device
    UNICODE_STRING mouseDevice;
    RtlInitUnicodeString(&mouseDevice, L"\\Device\\PointerClass0");

    PFILE_OBJECT fileObject = nullptr;
    status = IoGetDeviceObjectPointer(
        &mouseDevice,
        FILE_READ_DATA,
        &fileObject,
        &g_MouseDevice
    );

    if (fileObject) {
        ObDereferenceObject(fileObject);
    }

    if (!NT_SUCCESS(status) || !g_MouseDevice) {
        ObDereferenceObject(mouseDriver);
        LOG_ERROR("Failed to get mouse device: 0x%X", status);
        return status;
    }

    // Find ServiceCallback in device extension
    __try {
        // Device extension contains the callback
        // Offset varies by Windows version, but typically at +0x38 or +0x40
        PVOID deviceExtension = g_MouseDevice->DeviceExtension;
        
        if (deviceExtension) {
            // Try common offsets
            ULONG offsets[] = { 0x38, 0x40, 0x48 };
            
            for (ULONG i = 0; i < sizeof(offsets) / sizeof(ULONG); i++) {
                PMOUSE_SERVICE_CALLBACK* callbackPtr = reinterpret_cast<PMOUSE_SERVICE_CALLBACK*>(
                    reinterpret_cast<PUCHAR>(deviceExtension) + offsets[i]
                );

                if (MmIsAddressValid(callbackPtr) && MmIsAddressValid(*callbackPtr)) {
                    g_MouseCallback = *callbackPtr;
                    
                    if (g_MouseCallback) {
                        LOG_INFO("Mouse callback found at offset 0x%X: 0x%p", 
                                offsets[i], g_MouseCallback);
                        g_Initialized = TRUE;
                        ObDereferenceObject(mouseDriver);
                        return STATUS_SUCCESS;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("Exception while finding mouse callback");
    }

    ObDereferenceObject(mouseDriver);
    LOG_ERROR("Failed to find mouse service callback");
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS MoveMouse(LONG x, LONG y, USHORT buttonFlags) {
#if !SUPPORT_MOUSE_INJECTION
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    UNREFERENCED_PARAMETER(buttonFlags);
    return STATUS_NOT_IMPLEMENTED;
#endif

    if (!g_Initialized || !g_MouseCallback || !g_MouseDevice) {
        LOG_ERROR("Mouse not initialized");
        return STATUS_DEVICE_NOT_READY;
    }

    // Create mouse input data
    MOUSE_INPUT_DATA mouseData = {};
    mouseData.LastX = x;
    mouseData.LastY = y;
    mouseData.ButtonFlags = buttonFlags;
    mouseData.Flags = MOUSE_MOVE_RELATIVE;

    // Call the service callback
    __try {
        ULONG consumed = 0;
        g_MouseCallback(
            g_MouseDevice,
            &mouseData,
            &mouseData + 1,
            &consumed
        );

        LOG_DEBUG("Mouse moved: (%ld, %ld) buttons: 0x%X", x, y, buttonFlags);
        return STATUS_SUCCESS;

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("Exception during mouse move");
        return GetExceptionCode();
    }
}

NTSTATUS LeftClick() {
    NTSTATUS status = MoveMouse(0, 0, MOUSE_LEFT_BUTTON_DOWN);
    if (!NT_SUCCESS(status)) return status;

    // Small delay
    LARGE_INTEGER interval;
    interval.QuadPart = -10000LL; // 1ms
    KeDelayExecutionThread(KernelMode, FALSE, &interval);

    return MoveMouse(0, 0, MOUSE_LEFT_BUTTON_UP);
}

NTSTATUS RightClick() {
    NTSTATUS status = MoveMouse(0, 0, MOUSE_RIGHT_BUTTON_DOWN);
    if (!NT_SUCCESS(status)) return status;

    LARGE_INTEGER interval;
    interval.QuadPart = -10000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &interval);

    return MoveMouse(0, 0, MOUSE_RIGHT_BUTTON_UP);
}

VOID Cleanup() {
    g_MouseCallback = nullptr;
    g_MouseDevice = nullptr;
    g_Initialized = FALSE;
    LOG_INFO("Mouse cleanup complete");
}

} // namespace Mouse
