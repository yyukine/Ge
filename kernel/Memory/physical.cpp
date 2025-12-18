#include <ntifs.h>
#include "../../shared/definitions.h"
#include "../Core/config.h"
#include "../Utils/log.h"
#include "cr3.h"

namespace Physical {

// Dynamic function pointers
typedef PVOID(*MmCopyMemory_t)(PVOID, MM_COPY_ADDRESS, SIZE_T, ULONG, PSIZE_T);
typedef PVOID(*MmMapIoSpaceEx_t)(PHYSICAL_ADDRESS, SIZE_T, ULONG);
typedef VOID(*MmUnmapIoSpace_t)(PVOID, SIZE_T);

MmCopyMemory_t pMmCopyMemory = nullptr;
MmMapIoSpaceEx_t pMmMapIoSpaceEx = nullptr;
MmUnmapIoSpace_t pMmUnmapIoSpace = nullptr;

NTSTATUS Initialize() {
    UNICODE_STRING funcName;

    // Resolve MmCopyMemory
    RtlInitUnicodeString(&funcName, L"MmCopyMemory");
    pMmCopyMemory = reinterpret_cast<MmCopyMemory_t>(MmGetSystemRoutineAddress(&funcName));
    if (!pMmCopyMemory) {
        LOG_ERROR("Failed to resolve MmCopyMemory");
        return STATUS_UNSUCCESSFUL;
    }

    // Resolve MmMapIoSpaceEx
    RtlInitUnicodeString(&funcName, L"MmMapIoSpaceEx");
    pMmMapIoSpaceEx = reinterpret_cast<MmMapIoSpaceEx_t>(MmGetSystemRoutineAddress(&funcName));
    if (!pMmMapIoSpaceEx) {
        LOG_ERROR("Failed to resolve MmMapIoSpaceEx");
        return STATUS_UNSUCCESSFUL;
    }

    // Resolve MmUnmapIoSpace
    RtlInitUnicodeString(&funcName, L"MmUnmapIoSpace");
    pMmUnmapIoSpace = reinterpret_cast<MmUnmapIoSpace_t>(MmGetSystemRoutineAddress(&funcName));
    if (!pMmUnmapIoSpace) {
        LOG_ERROR("Failed to resolve MmUnmapIoSpace");
        return STATUS_UNSUCCESSFUL;
    }

    LOG_INFO("Physical memory functions initialized");
    return STATUS_SUCCESS;
}

NTSTATUS ReadPhysical(PVOID physicalAddress, PVOID buffer, SIZE_T size, PSIZE_T bytesRead) {
    if (!physicalAddress || !buffer || !size || !bytesRead) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!pMmCopyMemory) {
        LOG_ERROR("MmCopyMemory not initialized");
        return STATUS_UNSUCCESSFUL;
    }

    MM_COPY_ADDRESS copyAddr = {};
    copyAddr.PhysicalAddress.QuadPart = reinterpret_cast<LONGLONG>(physicalAddress);

    SIZE_T copied = 0;
    PVOID result = pMmCopyMemory(buffer, copyAddr, size, MM_COPY_MEMORY_PHYSICAL, &copied);

    if (bytesRead) *bytesRead = copied;

    return (result != nullptr && copied == size) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS WritePhysical(PVOID physicalAddress, PVOID buffer, SIZE_T size, PSIZE_T bytesWritten) {
    if (!physicalAddress || !buffer || !size || !bytesWritten) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!pMmMapIoSpaceEx || !pMmUnmapIoSpace) {
        LOG_ERROR("Mm functions not initialized");
        return STATUS_UNSUCCESSFUL;
    }

    PHYSICAL_ADDRESS physAddr = {};
    physAddr.QuadPart = reinterpret_cast<LONGLONG>(physicalAddress);

    PVOID mappedMem = pMmMapIoSpaceEx(physAddr, size, PAGE_READWRITE);
    if (!mappedMem) {
        LOG_ERROR("Failed to map physical memory at 0x%llX", (UINT64)physicalAddress);
        return STATUS_UNSUCCESSFUL;
    }

    __try {
        RtlCopyMemory(mappedMem, buffer, size);
        *bytesWritten = size;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pMmUnmapIoSpace(mappedMem, size);
        LOG_ERROR("Exception during physical write");
        return GetExceptionCode();
    }

    pMmUnmapIoSpace(mappedMem, size);
    return STATUS_SUCCESS;
}

UINT64 TranslateLinear(UINT64 directoryTableBase, UINT64 virtualAddress) {
    if (!directoryTableBase || !virtualAddress) return 0;

    directoryTableBase &= ~0xFULL;

    virt_addr_t virtAddr;
    virtAddr.value = reinterpret_cast<PVOID>(virtualAddress);

    SIZE_T bytesRead = 0;

    // PML4E
    MMPTE pml4e = {};
    UINT64 pml4eAddr = directoryTableBase + 8 * virtAddr.pml4_index;
    if (!NT_SUCCESS(ReadPhysical(reinterpret_cast<PVOID>(pml4eAddr), &pml4e, sizeof(MMPTE), &bytesRead))) {
        return 0;
    }
    if (!pml4e.Hard.Valid) return 0;

    // PDPTE
    MMPTE pdpte = {};
    UINT64 pdpteAddr = (pml4e.Hard.PageFrameNumber << 12) + 8 * virtAddr.pdpt_index;
    if (!NT_SUCCESS(ReadPhysical(reinterpret_cast<PVOID>(pdpteAddr), &pdpte, sizeof(MMPTE), &bytesRead))) {
        return 0;
    }
    if (!pdpte.Hard.Valid) return 0;

    // 1GB large page
    if (pdpte.Hard.LargePage) {
        return (pdpte.Hard.PageFrameNumber << 30) + (virtualAddress & 0x3FFFFFFFULL);
    }

    // PDE
    MMPTE pde = {};
    UINT64 pdeAddr = (pdpte.Hard.PageFrameNumber << 12) + 8 * virtAddr.pd_index;
    if (!NT_SUCCESS(ReadPhysical(reinterpret_cast<PVOID>(pdeAddr), &pde, sizeof(MMPTE), &bytesRead))) {
        return 0;
    }
    if (!pde.Hard.Valid) return 0;

    // 2MB large page
    if (pde.Hard.LargePage) {
        return (pde.Hard.PageFrameNumber << 21) + (virtualAddress & 0x1FFFFFULL);
    }

    // PTE
    MMPTE pte = {};
    UINT64 pteAddr = (pde.Hard.PageFrameNumber << 12) + 8 * virtAddr.pt_index;
    if (!NT_SUCCESS(ReadPhysical(reinterpret_cast<PVOID>(pteAddr), &pte, sizeof(MMPTE), &bytesRead))) {
        return 0;
    }
    if (!pte.Hard.Valid) return 0;

    // 4KB page
    return (pte.Hard.PageFrameNumber << 12) + (virtualAddress & 0xFFFULL);
}

NTSTATUS ReadVirtual(UINT64 directoryTableBase, PVOID virtualAddress, PVOID buffer, SIZE_T size) {
    if (!directoryTableBase || !virtualAddress || !buffer || !size) {
        return STATUS_INVALID_PARAMETER;
    }

    if (size > MAX_READ_SIZE) {
        LOG_ERROR("Read size too large: 0x%llX", size);
        return STATUS_INVALID_PARAMETER;
    }

    SIZE_T totalRead = 0;
    UINT64 currentVa = reinterpret_cast<UINT64>(virtualAddress);
    PUCHAR outBuffer = static_cast<PUCHAR>(buffer);

    while (totalRead < size) {
        // Calculate page offset and chunk size
        SIZE_T pageOffset = currentVa & (PAGE_SIZE - 1);
        SIZE_T chunkSize = min(PAGE_SIZE - pageOffset, size - totalRead);

        // Translate virtual to physical
        UINT64 physicalAddr = TranslateLinear(directoryTableBase, currentVa);
        if (!physicalAddr) {
            LOG_ERROR("Failed to translate VA 0x%llX", currentVa);
            return STATUS_INVALID_ADDRESS;
        }

        // Read from physical memory
        SIZE_T bytesRead = 0;
        NTSTATUS status = ReadPhysical(
            reinterpret_cast<PVOID>(physicalAddr),
            outBuffer + totalRead,
            chunkSize,
            &bytesRead
        );

        if (!NT_SUCCESS(status) || bytesRead != chunkSize) {
            LOG_ERROR("Physical read failed at PA 0x%llX", physicalAddr);
            return status ? status : STATUS_PARTIAL_COPY;
        }

        totalRead += chunkSize;
        currentVa += chunkSize;
    }

    return STATUS_SUCCESS;
}

NTSTATUS WriteVirtual(UINT64 directoryTableBase, PVOID virtualAddress, PVOID buffer, SIZE_T size) {
    if (!directoryTableBase || !virtualAddress || !buffer || !size) {
        return STATUS_INVALID_PARAMETER;
    }

    if (size > MAX_WRITE_SIZE) {
        LOG_ERROR("Write size too large: 0x%llX", size);
        return STATUS_INVALID_PARAMETER;
    }

    SIZE_T totalWritten = 0;
    UINT64 currentVa = reinterpret_cast<UINT64>(virtualAddress);
    PUCHAR inBuffer = static_cast<PUCHAR>(buffer);

    while (totalWritten < size) {
        // Calculate page offset and chunk size
        SIZE_T pageOffset = currentVa & (PAGE_SIZE - 1);
        SIZE_T chunkSize = min(PAGE_SIZE - pageOffset, size - totalWritten);

        // Translate virtual to physical
        UINT64 physicalAddr = TranslateLinear(directoryTableBase, currentVa);
        if (!physicalAddr) {
            LOG_ERROR("Failed to translate VA 0x%llX for write", currentVa);
            return STATUS_INVALID_ADDRESS;
        }

        // Write to physical memory
        SIZE_T bytesWritten = 0;
        NTSTATUS status = WritePhysical(
            reinterpret_cast<PVOID>(physicalAddr),
            inBuffer + totalWritten,
            chunkSize,
            &bytesWritten
        );

        if (!NT_SUCCESS(status) || bytesWritten != chunkSize) {
            LOG_ERROR("Physical write failed at PA 0x%llX", physicalAddr);
            return status ? status : STATUS_PARTIAL_COPY;
        }

        totalWritten += chunkSize;
        currentVa += chunkSize;
    }

    return STATUS_SUCCESS;
}

} // namespace Physical
