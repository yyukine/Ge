#pragma once
#include <ntifs.h>
#include "../../shared/definitions.h"
#include "../Core/config.h"

namespace CR3 {

// Global variables for CR3 system
extern UINT64 g_PteBase;
extern UINT64 g_PdeBase;
extern UINT64 g_PpeBase;
extern UINT64 g_PxeBase;
extern UINT64 g_SelfMapIdx;
extern UINT64 g_MmPfnDatabase;

// CR3 Cache entry
struct CacheEntry {
    ULONG ProcessId;
    UINT64 DirectoryTableBase;
    LARGE_INTEGER Timestamp;
    BOOLEAN Valid;
};

// CR3 Cache (simple array-based cache)
constexpr ULONG CR3_CACHE_SIZE = 32;
extern CacheEntry g_CR3Cache[CR3_CACHE_SIZE];
extern KSPIN_LOCK g_CR3CacheLock;
extern BOOLEAN g_CR3Initialized;

// Initialize CR3 system
NTSTATUS Initialize();

// Get system CR3
UINT64 GetSystemDirectoryBase();

// Find PTE base for self-referencing
NTSTATUS InitializePteBase();

// Initialize MmPfnDatabase pointer
NTSTATUS InitializeMmPfnDatabase();

// Bruteforce CR3 for a process
UINT64 BruteforceDirectoryBase(UINT64 baseAddress);

// Get CR3 with caching
NTSTATUS GetProcessCR3(ULONG processId, UINT64 baseAddress, UINT64* outCR3);

// Cache management
BOOLEAN GetCachedCR3(ULONG processId, UINT64* outCR3);
VOID AddCR3ToCache(ULONG processId, UINT64 cr3);
VOID InvalidateCR3Cache();
VOID InvalidateCR3ForProcess(ULONG processId);

// Helper functions
PVOID PhysicalToVirtual(UINT64 physicalAddress);
UINT64 GetKernelBase();

} // namespace CR3
