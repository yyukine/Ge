#pragma once

// ============================================
// CONFIGURATION - EDIT THESE BEFORE COMPILING
// ============================================

// Target driver for hijacking (choose one)
#define TARGET_DRIVER_HOOK_PEAUTH       // PEAUTH.sys - Biometric driver
// #define TARGET_DRIVER_HOOK_GPUENERGY    // gpuenergydrv.sys - GPU Energy driver
// #define TARGET_DRIVER_HOOK_IQVW64E      // iqvw64e.sys - Intel driver

// Driver identification strings (randomize these!)
#define DEVICE_NAME_STRING     L"\\Device\\{A7B2C9E1-4F3D-8A5E-B2C4-9D7E6F1A8B3C}"
#define SYMBOLIC_LINK_STRING   L"\\??\\{A7B2C9E1-4F3D-8A5E-B2C4-9D7E6F1A8B3C}"
#define DRIVER_NAME_STRING     L"\\Driver\\SecurityFilter"

// Process to hide (set your usermode executable name)
#define PROCESS_TO_HIDE        L"client.exe"

// Communication method
#define USE_OBCREATEOBJECT_HOOK    1  // 1 = Use hook (stealthy), 0 = Use IOCTL (detectable)
#define USE_SHARED_MEMORY          0  // 1 = Use shared memory for communication

// Security features
#define ENABLE_PROCESS_HIDING      1  // Hide usermode process
#define ENABLE_THREAD_HIDING       1  // Hide threads
#define ENABLE_DRIVER_CLEANING     1  // Clean driver traces
#define ENABLE_STACK_SPOOFING      1  // Spoof call stacks
#define ENABLE_STRING_ENCRYPTION   1  // Encrypt strings at runtime
#define ENABLE_ANTI_DEBUG          1  // Block kernel debugging

// Performance settings
#define CR3_CACHE_ENABLED          1  // Cache CR3 for performance
#define CR3_CACHE_TIMEOUT_MS       30000  // 30 seconds
#define MODULE_CACHE_ENABLED       1  // Cache module bases
#define MODULE_CACHE_SIZE          128  // Max cached modules

// Memory limits
#define MAX_READ_SIZE              0x100000   // 1 MB
#define MAX_WRITE_SIZE             0x100000   // 1 MB
#define MAX_MODULE_NAME_LENGTH     260
#define MAX_PROCESS_NAME_LENGTH    15

// Logging (DISABLE IN PRODUCTION!)
#ifdef _DEBUG
    #define ENABLE_LOGGING         1
    #define LOG_LEVEL              3  // 0=None, 1=Error, 2=Warning, 3=Info, 4=Debug
#else
    #define ENABLE_LOGGING         0
    #define LOG_LEVEL              0
#endif

// Target Windows versions
#define SUPPORT_WIN10_1803         1
#define SUPPORT_WIN10_1809         1
#define SUPPORT_WIN10_1903         1
#define SUPPORT_WIN10_1909         1
#define SUPPORT_WIN10_2004         1
#define SUPPORT_WIN10_20H2         1
#define SUPPORT_WIN10_21H1         1
#define SUPPORT_WIN10_21H2         1
#define SUPPORT_WIN10_22H2         1
#define SUPPORT_WIN11_21H2         1
#define SUPPORT_WIN11_22H2         1
#define SUPPORT_WIN11_23H2         1

// Anti-cheat compatibility
#define SUPPORT_BATTLEYE           1  // BattlEye support
#define SUPPORT_EAC_CLASSIC        1  // Easy Anti-Cheat (old)
#define SUPPORT_EAC_EOS            0  // EAC with EOS (needs CR3 buffer)
#define SUPPORT_VAC                1  // Valve Anti-Cheat
#define SUPPORT_RICOCHET           0  // Call of Duty Ricochet
#define SUPPORT_VANGUARD           0  // Riot Vanguard (too aggressive)

// Advanced features
#define SUPPORT_LARGE_PAGES        1  // 2MB/1GB page support
#define SUPPORT_PHYSICAL_MEMORY    1  // Direct physical access
#define SUPPORT_MOUSE_INJECTION    1  // Kernel mouse movement
#define SUPPORT_MEMORY_PROTECT     1  // VirtualProtect equivalent
#define SUPPORT_MEMORY_ALLOC       1  // VirtualAlloc equivalent

// Mapper detection bypass
#define CLEAN_KDMAPPER_TRACES      1  // Clean kdmapper artifacts
#define CLEAN_PDFWKRNL_TRACES      1  // Clean PdFwKrnl artifacts
#define CLEAN_VULNERABLE_DRIVER    1  // Remove vulnerable driver

// Vulnerable drivers to clean (if used for mapping)
#define VULN_DRIVER_1              L"DriverKL.sys"
#define VULN_DRIVER_2              L"PdFwKrnl.sys"
#define VULN_DRIVER_3              L"iqvw64e.sys"

// Pool tags (randomize these!)
#define POOL_TAG_CACHE             'cCdU'  // 'UdCc' reversed
#define POOL_TAG_BUFFER            'bBdU'  // 'UdBb' reversed
#define POOL_TAG_MODULE            'mMdU'  // 'UdMm' reversed

// Timing delays (anti-detection)
#define INIT_DELAY_MS              100   // Delay after driver load
#define HOOK_DELAY_MS              50    // Delay before hooking
#define CLEANUP_DELAY_MS           500   // Delay before cleanup

// Obfuscation settings
#if ENABLE_STRING_ENCRYPTION
    #define ENCRYPT_STRING(str)    skCrypt(str)
#else
    #define ENCRYPT_STRING(str)    str
#endif

// Validation macros
#define VALIDATE_SECURITY_CODE(code) ((code) == SECURITY_CODE)
#define VALIDATE_PROCESS_ID(pid)     ((pid) > 4 && (pid) < 100000)
#define VALIDATE_ADDRESS(addr)       ((addr) > 0x1000 && (addr) < 0x7FFFFFFFFFFF)
#define VALIDATE_SIZE(size)          ((size) > 0 && (size) <= MAX_READ_SIZE)

// Target driver paths
#ifdef TARGET_DRIVER_HOOK_PEAUTH
    #define TARGET_DRIVER_PATH       L"\\Driver\\PEAUTH"
    #define TARGET_DEVICE_PATH       L"\\Device\\PEAUTH"
    #define HOOK_IRP_FUNCTION        IRP_MJ_DEVICE_CONTROL
#endif

#ifdef TARGET_DRIVER_HOOK_GPUENERGY
    #define TARGET_DRIVER_PATH       L"\\Driver\\gpuenergydrv"
    #define TARGET_DEVICE_PATH       L"\\Device\\gpuenergydrv"
    #define HOOK_IRP_FUNCTION        IRP_MJ_DEVICE_CONTROL
#endif

#ifdef TARGET_DRIVER_HOOK_IQVW64E
    #define TARGET_DRIVER_PATH       L"\\Driver\\iqvw64e"
    #define TARGET_DEVICE_PATH       L"\\Device\\iqvw64e"
    #define HOOK_IRP_FUNCTION        IRP_MJ_DEVICE_CONTROL
#endif

// Warning messages
#if !defined(TARGET_DRIVER_HOOK_PEAUTH) && !defined(TARGET_DRIVER_HOOK_GPUENERGY) && !defined(TARGET_DRIVER_HOOK_IQVW64E)
    #error "No target driver defined! Please uncomment one TARGET_DRIVER_HOOK_* define"
#endif

#if ENABLE_LOGGING && !defined(_DEBUG)
    #warning "Logging is enabled in release mode! This may cause detection."
#endif

#if !ENABLE_DRIVER_CLEANING
    #warning "Driver cleaning is disabled! Traces will remain in system."
#endif

// ============================================
// BUILD CONFIGURATION
// ============================================

// Compiler optimizations
#ifdef _DEBUG
    #pragma optimize("", off)
    #pragma inline_depth(0)
#else
    #pragma optimize("gsy", on)
    #pragma inline_depth(255)
    #pragma inline_recursion(on)
#endif

// Warning suppressions
#pragma warning(disable: 4100) // Unreferenced parameter
#pragma warning(disable: 4201) // Nameless struct/union
#pragma warning(disable: 4214) // Bit field types other than int
#pragma warning(disable: 4996) // Deprecated functions

// Pool allocation tags
#define ExAllocatePoolTag(Type, Size) ExAllocatePoolWithTag(Type, Size, POOL_TAG_BUFFER)
#define ExFreePoolTag(Ptr) ExFreePoolWithTag(Ptr, POOL_TAG_BUFFER)
