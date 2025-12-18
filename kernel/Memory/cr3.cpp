#include "cr3.h"
#include "../Utils/log.h"

namespace CR3 {

// Global variables
UINT64 g_PteBase = 0;
UINT64 g_PdeBase = 0;
UINT64 g_PpeBase = 0;
UINT64 g_PxeBase = 0;
UINT64 g_SelfMapIdx = 0;
UINT64 g_MmPfnDatabase = 0;
CacheEntry g_CR3Cache[CR3_CACHE_SIZE] = {};
KSPIN_LOCK g_CR3CacheLock = {};
BOOLEAN g_CR3Initialized = FALSE;

NTSTATUS Initialize() {
    if (g_CR3Initialized) {
        return STATUS_SUCCESS;
    }

    KeInitializeSpinLock(&g_CR3CacheLock);

    // Initialize PTE base
    NTSTATUS status = InitializePteBase();
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to initialize PTE base: 0x%X", status);
        return status;
    }

    // Initialize MmPfnDatabase
    status = InitializeMmPfnDatabase();
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("Failed to initialize MmPfnDatabase: 0x%X", status);
        return status;
    }

    g_CR3Initialized = TRUE;
    LOG_INFO("CR3 system initialized successfully");
    return STATUS_SUCCESS;
}

UINT64 GetSystemDirectoryBase() {
    return __readcr3() & 0xFFFFFFFFFFFFF000ULL;
}

PVOID PhysicalToVirtual(UINT64 physicalAddress) {
    PHYSICAL_ADDRESS physAddr;
    physAddr.QuadPart = (LONGLONG)physicalAddress;
    return MmGetVirtualForPhysical(physAddr);
}

UINT64 GetKernelBase() {
    // Get kernel base from IDT
    const UINT64 idtBase = *reinterpret_cast<UINT64*>(__readgsqword(0x18) + 0x38);
    const UINT64 descriptor0 = *reinterpret_cast<UINT64*>(idtBase);
    const UINT64 descriptor1 = *reinterpret_cast<UINT64*>(idtBase + 8);
    const UINT64 isrBase = ((descriptor0 >> 32) & 0xFFFF0000) + (descriptor0 & 0xFFFF) + (descriptor1 << 32);
    
    UINT64 alignedBase = isrBase & 0xFFFFFFFFFFFFF000ULL;

    // Scan backwards for MZ header
    for (; ; alignedBase -= 0x1000) {
        for (UINT8* searchBase = reinterpret_cast<UINT8*>(alignedBase); 
             searchBase < reinterpret_cast<UINT8*>(alignedBase) + 0xFF9; 
             searchBase++) {
            
            if (searchBase[0] == 0x48 && searchBase[1] == 0x8D && 
                searchBase[2] == 0x1D && searchBase[6] == 0xFF) {
                
                const INT32 relativeOffset = *reinterpret_cast<INT32*>(&searchBase[3]);
                const UINT64 address = reinterpret_cast<UINT64>(searchBase + relativeOffset + 7);
                
                if ((address & 0xFFF) == 0) {
                    if (*reinterpret_cast<UINT16*>(address) == 0x5A4D) { // 'MZ'
                        return address;
                    }
                }
            }
        }
    }
}

UINT64 SearchPattern(PVOID moduleBase, const CHAR* section, const CHAR* pattern) {
    auto inRange = [](auto x, auto a, auto b) { return (x >= a && x <= b); };
    auto getBits = [&](auto x) { return inRange((x & ~0x20), 'A', 'F') ? ((x & ~0x20) - 'A' + 0xA) : (inRange(x, '0', '9') ? x - '0' : 0); };
    auto getByte = [&](auto x) { return (getBits(x[0]) << 4 | getBits(x[1])); };

    const auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    const auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<UINT64>(moduleBase) + dosHeader->e_lfanew);
    const auto sectionHeaders = reinterpret_cast<PIMAGE_SECTION_HEADER>(ntHeaders + 1);

    UINT64 rangeStart = 0;
    UINT64 rangeEnd = 0;

    for (auto curSection = sectionHeaders; 
         curSection < sectionHeaders + ntHeaders->FileHeader.NumberOfSections; 
         curSection++) {
        
        if (strcmp(reinterpret_cast<const CHAR*>(curSection->Name), section) == 0) {
            rangeStart = reinterpret_cast<UINT64>(moduleBase) + curSection->VirtualAddress;
            rangeEnd = rangeStart + curSection->Misc.VirtualSize;
            break;
        }
    }

    if (rangeStart == 0) return 0;

    UINT64 firstMatch = 0;
    const CHAR* pat = pattern;

    for (UINT64 cur = rangeStart; cur < rangeEnd; cur++) {
        if (*pat == '\0') {
            return firstMatch;
        }

        if (*reinterpret_cast<UINT8*>(pat) == '?' || 
            *reinterpret_cast<UINT8*>(cur) == getByte(pat)) {
            
            if (!firstMatch) firstMatch = cur;
            if (!pat[2]) return firstMatch;
            
            pat += (*reinterpret_cast<UINT16*>(pat) == 16191 || *reinterpret_cast<UINT8*>(pat) != '?') ? 3 : 2;
        } else {
            pat = pattern;
            firstMatch = 0;
        }
    }

    return 0;
}

NTSTATUS InitializePteBase() {
    CR3 systemCR3;
    systemCR3.Flags = GetSystemDirectoryBase();
    
    UINT64 dirbasePhys = systemCR3.AddressOfPageDirectory << 12;
    PMMPTE ptEntry = reinterpret_cast<PMMPTE>(PhysicalToVirtual(dirbasePhys));
    
    if (!ptEntry) {
        LOG_ERROR("Failed to get virtual address for dirbase");
        return STATUS_UNSUCCESSFUL;
    }

    for (UINT64 idx = 0; idx < 0x200; idx++) {
        if (ptEntry[idx].Hard.PageFrameNumber == systemCR3.AddressOfPageDirectory) {
            g_SelfMapIdx = idx;
            g_PteBase = (idx + 0x1FFFE00ULL) << 39ULL;
            g_PdeBase = (idx << 30ULL) + g_PteBase;
            g_PpeBase = (idx << 30ULL) + g_PteBase + (idx << 21ULL);
            g_PxeBase = (idx << 12ULL) + g_PpeBase;
            
            LOG_INFO("PTE Base initialized: 0x%llX", g_PteBase);
            return STATUS_SUCCESS;
        }
    }

    LOG_ERROR("Failed to find self-referencing PML4 entry");
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS InitializeMmPfnDatabase() {
    UINT64 kernelBase = GetKernelBase();
    if (!kernelBase) {
        LOG_ERROR("Failed to get kernel base");
        return STATUS_UNSUCCESSFUL;
    }

    // Pattern: B9 ? ? ? ? 48 8B 05 ? ? ? ? 48 89 43 18
    UINT64 patternAddr = SearchPattern(
        reinterpret_cast<PVOID>(kernelBase),
        ".text",
        "B9 ? ? ? ? 48 8B 05 ? ? ? ? 48 89 43 18"
    );

    if (!patternAddr) {
        LOG_ERROR("Failed to find MmPfnDatabase pattern");
        return STATUS_UNSUCCESSFUL;
    }

    patternAddr += 5; // Skip to next instruction
    INT32 relativeOffset = *reinterpret_cast<INT32*>(patternAddr + 3);
    UINT64 resolvedBase = patternAddr + relativeOffset + 7;
    g_MmPfnDatabase = *reinterpret_cast<UINT64*>(resolvedBase);

    LOG_INFO("MmPfnDatabase initialized: 0x%llX", g_MmPfnDatabase);
    return STATUS_SUCCESS;
}

UINT64 BruteforceDirectoryBase(UINT64 baseAddress) {
    if (!baseAddress || !g_MmPfnDatabase) {
        LOG_ERROR("Invalid parameters for CR3 bruteforce");
        return 0;
    }

    virt_addr_t virtAddr;
    virtAddr.value = reinterpret_cast<PVOID>(baseAddress);

    PPHYSICAL_MEMORY_RANGE memRanges = MmGetPhysicalMemoryRanges();
    if (!memRanges) {
        LOG_ERROR("Failed to get physical memory ranges");
        return 0;
    }

    UINT64 foundCR3 = 0;
    UINT64 cr3PteBase = g_SelfMapIdx * 8 + g_PxeBase;

    __try {
        for (INT32 rangeIdx = 0; rangeIdx < 200; rangeIdx++) {
            if (memRanges[rangeIdx].BaseAddress.QuadPart == 0 &&
                memRanges[rangeIdx].NumberOfBytes.QuadPart == 0) {
                break;
            }

            UINT64 startPfn = memRanges[rangeIdx].BaseAddress.QuadPart >> 12;
            UINT64 endPfn = startPfn + (memRanges[rangeIdx].NumberOfBytes.QuadPart >> 12);

            for (UINT64 pfn = startPfn; pfn < endPfn; pfn++) {
                PMMPFN curMmpfn = reinterpret_cast<PMMPFN>(g_MmPfnDatabase + 0x30 * pfn);

                if (curMmpfn->Flags) {
                    if (curMmpfn->Flags == 1) continue;
                    if (curMmpfn->PteAddress != cr3PteBase) continue;

                    UINT64 decryptedEprocess = ((curMmpfn->Flags | 0xF000000000000000ULL) >> 0xD) | 0xFFFF000000000000ULL;

                    if (MmIsAddressValid(reinterpret_cast<PVOID>(decryptedEprocess))) {
                        foundCR3 = pfn << 12;
                        goto cleanup;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_ERROR("Exception during CR3 bruteforce");
    }

cleanup:
    ExFreePool(memRanges);
    
    if (foundCR3) {
        LOG_INFO("Found CR3: 0x%llX for base 0x%llX", foundCR3, baseAddress);
    } else {
        LOG_WARNING("CR3 bruteforce failed for base 0x%llX", baseAddress);
    }

    return foundCR3;
}

BOOLEAN GetCachedCR3(ULONG processId, UINT64* outCR3) {
    if (!processId || !outCR3) return FALSE;

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_CR3CacheLock, &oldIrql);

    for (ULONG i = 0; i < CR3_CACHE_SIZE; i++) {
        if (g_CR3Cache[i].Valid && g_CR3Cache[i].ProcessId == processId) {
            // Check if cache entry is still valid (timeout)
            LARGE_INTEGER currentTime;
            KeQuerySystemTime(&currentTime);
            
            LONGLONG timeDiff = currentTime.QuadPart - g_CR3Cache[i].Timestamp.QuadPart;
            LONGLONG timeoutTicks = (LONGLONG)CR3_CACHE_TIMEOUT_MS * 10000LL; // Convert to 100ns units

            if (timeDiff < timeoutTicks) {
                *outCR3 = g_CR3Cache[i].DirectoryTableBase;
                KeReleaseSpinLock(&g_CR3CacheLock, oldIrql);
                LOG_DEBUG("CR3 cache hit for PID %lu: 0x%llX", processId, *outCR3);
                return TRUE;
            } else {
                // Invalidate expired entry
                g_CR3Cache[i].Valid = FALSE;
            }
        }
    }

    KeReleaseSpinLock(&g_CR3CacheLock, oldIrql);
    LOG_DEBUG("CR3 cache miss for PID %lu", processId);
    return FALSE;
}

VOID AddCR3ToCache(ULONG processId, UINT64 cr3) {
    if (!processId || !cr3) return;

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_CR3CacheLock, &oldIrql);

    // Find empty slot or oldest entry
    ULONG targetSlot = 0;
    LARGE_INTEGER oldestTime;
    oldestTime.QuadPart = MAXLONGLONG;

    for (ULONG i = 0; i < CR3_CACHE_SIZE; i++) {
        if (!g_CR3Cache[i].Valid) {
            targetSlot = i;
            break;
        }

        if (g_CR3Cache[i].ProcessId == processId) {
            targetSlot = i;
            break;
        }

        if (g_CR3Cache[i].Timestamp.QuadPart < oldestTime.QuadPart) {
            oldestTime = g_CR3Cache[i].Timestamp;
            targetSlot = i;
        }
    }

    // Add to cache
    g_CR3Cache[targetSlot].ProcessId = processId;
    g_CR3Cache[targetSlot].DirectoryTableBase = cr3;
    KeQuerySystemTime(&g_CR3Cache[targetSlot].Timestamp);
    g_CR3Cache[targetSlot].Valid = TRUE;

    KeReleaseSpinLock(&g_CR3CacheLock, oldIrql);
    LOG_DEBUG("Added CR3 to cache: PID %lu = 0x%llX", processId, cr3);
}

VOID InvalidateCR3Cache() {
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_CR3CacheLock, &oldIrql);

    for (ULONG i = 0; i < CR3_CACHE_SIZE; i++) {
        g_CR3Cache[i].Valid = FALSE;
    }

    KeReleaseSpinLock(&g_CR3CacheLock, oldIrql);
    LOG_INFO("CR3 cache invalidated");
}

VOID InvalidateCR3ForProcess(ULONG processId) {
    if (!processId) return;

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_CR3CacheLock, &oldIrql);

    for (ULONG i = 0; i < CR3_CACHE_SIZE; i++) {
        if (g_CR3Cache[i].Valid && g_CR3Cache[i].ProcessId == processId) {
            g_CR3Cache[i].Valid = FALSE;
            LOG_DEBUG("Invalidated CR3 cache for PID %lu", processId);
            break;
        }
    }

    KeReleaseSpinLock(&g_CR3CacheLock, oldIrql);
}

NTSTATUS GetProcessCR3(ULONG processId, UINT64 baseAddress, UINT64* outCR3) {
    if (!processId || !baseAddress || !outCR3) {
        return STATUS_INVALID_PARAMETER;
    }

#if CR3_CACHE_ENABLED
    // Try cache first
    if (GetCachedCR3(processId, outCR3)) {
        return STATUS_SUCCESS;
    }
#endif

    // Bruteforce CR3
    UINT64 cr3 = BruteforceDirectoryBase(baseAddress);
    if (!cr3) {
        // Fallback: try reading from EPROCESS
        PEPROCESS process = NULL;
        NTSTATUS status = PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(processId)), &process);
        
        if (NT_SUCCESS(status) && process) {
            cr3 = *reinterpret_cast<PUINT64>(reinterpret_cast<PUCHAR>(process) + 0x28); // DirectoryTableBase offset
            ObDereferenceObject(process);
            
            if (cr3) {
                LOG_INFO("Got CR3 from EPROCESS fallback: 0x%llX", cr3);
            }
        }
    }

    if (!cr3) {
        LOG_ERROR("Failed to get CR3 for PID %lu", processId);
        return STATUS_UNSUCCESSFUL;
    }

    *outCR3 = cr3;

#if CR3_CACHE_ENABLED
    AddCR3ToCache(processId, cr3);
#endif

    return STATUS_SUCCESS;
}

} // namespace CR3
