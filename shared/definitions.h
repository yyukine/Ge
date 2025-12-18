#pragma once

// Security code for IOCTL validation
#define SECURITY_CODE 0x8F3A92B1

// IOCTL Codes (legacy support, prefer ObCreateObject hook)
#define IOCTL_ATTACH         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_DETACH         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_READ_MEMORY    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_WRITE_MEMORY   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_GET_BASE       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_GET_MODULE     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_GET_CR3        CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_MOUSE_MOVE     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_ENUM_MODULES   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

// Windows build numbers
#define WIN_1803 17134
#define WIN_1809 17763
#define WIN_1903 18362
#define WIN_1909 18363
#define WIN_2004 19041
#define WIN_20H2 19042
#define WIN_21H1 19043
#define WIN_21H2 19044
#define WIN_22H2 19045
#define WIN_11_21H2 22000
#define WIN_11_22H2 22621
#define WIN_11_23H2 22631

// Page sizes
#define PAGE_SIZE_4KB 0x1000
#define PAGE_SIZE_2MB 0x200000
#define PAGE_SIZE_1GB 0x40000000
#define PAGE_OFFSET_SIZE 12

// Physical address mask
#define PMASK (~0xFULL << 8) & 0xFFFFFFFFFULL

// Request types for ObCreateObject hook
enum class RequestType : ULONG32 {
    Attach = 0x1000,
    Detach = 0x1001,
    ReadMemory = 0x1002,
    WriteMemory = 0x1003,
    GetBase = 0x1004,
    GetModule = 0x1005,
    GetCR3 = 0x1006,
    MouseMove = 0x1007,
    EnumModules = 0x1008,
    ProtectMemory = 0x1009,
    AllocMemory = 0x100A,
    FreeMemory = 0x100B
};

// Shared structures between kernel and usermode
#pragma pack(push, 1)

struct AttachRequest {
    ULONG32 SecurityCode;
    ULONG32 ProcessId;
};

struct DetachRequest {
    ULONG32 SecurityCode;
};

struct ReadWriteRequest {
    ULONG32 SecurityCode;
    ULONG32 ProcessId;
    ULONGLONG Address;
    ULONGLONG Buffer;
    ULONGLONG Size;
    BOOLEAN Write;
};

struct GetBaseRequest {
    ULONG32 SecurityCode;
    ULONG32 ProcessId;
    ULONGLONG BaseAddress; // OUT
};

struct GetModuleRequest {
    ULONG32 SecurityCode;
    ULONG32 ProcessId;
    WCHAR ModuleName[260];
    ULONGLONG ModuleBase; // OUT
};

struct GetCR3Request {
    ULONG32 SecurityCode;
    ULONG32 ProcessId;
    ULONGLONG CR3; // OUT
};

struct MouseMoveRequest {
    ULONG32 SecurityCode;
    LONG X;
    LONG Y;
    USHORT ButtonFlags;
};

struct EnumModulesRequest {
    ULONG32 SecurityCode;
    ULONG32 ProcessId;
    ULONG32 MaxCount;
    ULONG32 ReturnedCount; // OUT
    ULONGLONG Modules[256]; // OUT
};

struct ProtectMemoryRequest {
    ULONG32 SecurityCode;
    ULONG32 ProcessId;
    ULONGLONG Address;
    ULONGLONG Size;
    ULONG32 Protection;
    ULONG32 OldProtection; // OUT
};

struct AllocMemoryRequest {
    ULONG32 SecurityCode;
    ULONG32 ProcessId;
    ULONGLONG Size;
    ULONG32 Protection;
    ULONGLONG Address; // OUT
};

struct FreeMemoryRequest {
    ULONG32 SecurityCode;
    ULONG32 ProcessId;
    ULONGLONG Address;
    ULONGLONG Size;
};

// ObCreateObject hook communication structure
struct HookedRequest {
    RequestType Type;
    union {
        AttachRequest Attach;
        DetachRequest Detach;
        ReadWriteRequest ReadWrite;
        GetBaseRequest GetBase;
        GetModuleRequest GetModule;
        GetCR3Request GetCR3;
        MouseMoveRequest MouseMove;
        EnumModulesRequest EnumModules;
        ProtectMemoryRequest ProtectMemory;
        AllocMemoryRequest AllocMemory;
        FreeMemoryRequest FreeMemory;
    };
    NTSTATUS Status; // OUT
};

#pragma pack(pop)

// Virtual address structure for page table walking
typedef union _virt_addr_t {
    void* value;
    struct {
        UINT64 offset : 12;
        UINT64 pt_index : 9;
        UINT64 pd_index : 9;
        UINT64 pdpt_index : 9;
        UINT64 pml4_index : 9;
        UINT64 reserved : 16;
    };
} virt_addr_t;

// MMPTE structure (Page Table Entry)
typedef union _MMPTE {
    struct {
        ULONGLONG Valid : 1;
        ULONGLONG Write : 1;
        ULONGLONG Owner : 1;
        ULONGLONG WriteThrough : 1;
        ULONGLONG CacheDisable : 1;
        ULONGLONG Accessed : 1;
        ULONGLONG Dirty : 1;
        ULONGLONG LargePage : 1;
        ULONGLONG Global : 1;
        ULONGLONG CopyOnWrite : 1;
        ULONGLONG Unused : 1;
        ULONGLONG Write1 : 1;
        ULONGLONG PageFrameNumber : 36;
        ULONGLONG Reserved : 4;
        ULONGLONG SoftwareWsIndex : 11;
        ULONGLONG NoExecute : 1;
    } Hard;
    ULONGLONG Flags;
} MMPTE, *PMMPTE;

// CR3 structure
typedef union _CR3 {
    struct {
        UINT64 Reserved1 : 3;
        UINT64 PageLevelWriteThrough : 1;
        UINT64 PageLevelCacheDisable : 1;
        UINT64 Reserved2 : 7;
        UINT64 AddressOfPageDirectory : 36;
        UINT64 Reserved3 : 16;
    };
    UINT64 Flags;
} CR3, *PCR3;

// MMPFN structure (physical memory frame)
typedef struct _MMPFN {
    ULONGLONG Flags;
    ULONGLONG PteAddress;
    ULONGLONG Unused1;
    ULONGLONG Unused2;
    ULONGLONG Unused3;
    ULONGLONG Unused4;
} MMPFN, *PMMPFN;

// Process offsets (dynamically resolved)
struct ProcessOffsets {
    ULONG ImageFileName;
    ULONG ActiveProcessLinks;
    ULONG ActiveThreads;
    ULONG DirectoryTableBase;
    ULONG Token;
    ULONG UniqueProcessId;
};

// Module information
struct ModuleInfo {
    WCHAR Name[260];
    ULONGLONG Base;
    ULONG Size;
};

// Status codes
namespace Status {
    constexpr NTSTATUS Success = STATUS_SUCCESS;
    constexpr NTSTATUS InvalidParameter = STATUS_INVALID_PARAMETER;
    constexpr NTSTATUS AccessDenied = STATUS_ACCESS_DENIED;
    constexpr NTSTATUS NotFound = STATUS_NOT_FOUND;
    constexpr NTSTATUS ProcessNotFound = (NTSTATUS)0xC0000001L;
    constexpr NTSTATUS ModuleNotFound = (NTSTATUS)0xC0000002L;
    constexpr NTSTATUS InvalidAddress = STATUS_INVALID_ADDRESS;
    constexpr NTSTATUS PartialCopy = STATUS_PARTIAL_COPY;
}
