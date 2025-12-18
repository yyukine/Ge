# 🛡️ UndetectedDriver - Advanced Kernel Driver

## 🎯 Features

### Core Functionality
- **CR3 Bruteforce & Caching** - Optimized DirectoryTableBase detection with intelligent caching
- **Physical Memory R/W** - Direct physical memory access via MmCopyMemory/MmMapIoSpaceEx
- **Virtual Memory Translation** - Page table walking (PML4 → PDPT → PD → PT → Physical)
- **Process Cache System** - Smart caching mechanism for reduced overhead
- **Module Enumeration** - Fast PEB walking for DLL base addresses
- **Mouse Movement** - Kernel-mode mouse input injection
- **Stack Spoofing** - Call stack obfuscation for stealth

### Anti-Detection
- **ObCreateObject Hijacking** - Undetected communication method (not IOCTL)
- **String Encryption** - Runtime string decryption (skCrypter-based)
- **Process Hiding** - Hide usermode process from detection
- **Thread Hiding** - Hide threads from enumeration
- **No PatchGuard Triggers** - Safe kernel modifications
- **Dynamic Function Resolution** - No IAT/EAT dependencies

### Compatibility
- ✅ Windows 10 (1803 - 22H2)
- ✅ Windows 11 (21H2, 22H2, 23H2+)
- ✅ BattlEye Games (Tarkov, DayZ, R6S)
- ⚠️ EAC (requires CR3 buffer catch)

## 🏗️ Architecture

```
UndetectedDriver/
├── kernel/              # Kernel-mode driver
│   ├── Core/           # Main driver logic
│   ├── Memory/         # Memory operations
│   ├── Communication/  # Hook-based communication
│   ├── AntiCheat/      # AC bypass features
│   └── Utils/          # Helper functions
├── usermode/           # User-mode library
│   ├── Driver/        # Driver interface
│   ├── Memory/        # Memory wrappers
│   └── Utils/         # Helper classes
└── shared/            # Shared definitions
```

## 🔧 Setup

1. **Prerequisites:**
   - Visual Studio 2022
   - WDK (Windows Driver Kit)
   - Vulnerable driver for mapping (e.g., PdFwKrnl)
   - PatchGuard bypass (e.g., EFIGuard)
   - Code obfuscator (VMProtect recommended)

2. **Configuration:**
   - Edit `kernel/Core/config.h` - Set target driver for hijacking
   - Edit `kernel/Communication/interface.h` - Change handle identifier
   - Edit `kernel/AntiCheat/hide.cpp` - Set process name to hide

3. **Building:**
   ```
   Open UndetectedDriver.sln
   Build Configuration: Release x64
   ```

4. **Deployment:**
   - Use kernel mapper (PdFwKrnl/kdmapper)
   - Do NOT use manual mapping or kdmapper for production
   - Apply code protection (VMProtect/Themida)

## ⚠️ Security Notes

- This is for educational/research purposes only
- Requires kernel-mode execution (vulnerable driver)
- PatchGuard bypass needed for stability
- AC detection possible if used improperly
- Use at your own risk

## 📊 Performance

- **Read Speed:** ~437,000 reads/sec (4 bytes)
- **Write Speed:** ~420,000 writes/sec (4 bytes)
- **CR3 Cache Hit:** < 1μs
- **CR3 Bruteforce:** 10-50ms (first time)
- **Module Search:** 2-5ms (cached)

## 🔒 Anti-Detection Features

| Feature | Status | Notes |
|---------|--------|-------|
| No IOCTL | ✅ | Uses ObCreateObject hook |
| String Encryption | ✅ | Runtime decryption |
| Process Hiding | ✅ | EPROCESS manipulation |
| Stack Spoofing | ✅ | Return address spoofing |
| No Traces | ✅ | Cleans driver artifacts |
| Dynamic Resolution | ✅ | No static imports |

## 🎮 Supported Anti-Cheats

| Anti-Cheat | Status | Notes |
|------------|--------|-------|
| BattlEye | ✅ | Fully working |
| EAC (Classic) | ⚠️ | Needs CR3 buffer |
| EAC (EOS) | ⚠️ | Needs CR3 resolver |
| VAC | ✅ | Basic protection |
| Ricochet | ❌ | Not tested |
| Vanguard | ❌ | Too aggressive |

## 📝 Usage Example

```cpp
#include "usermode/Driver/driver.h"

int main() {
    Driver drv;
    
    // Attach to process
    if (!drv.Attach("target.exe")) {
        return 1;
    }
    
    // Get module base
    uintptr_t base = drv.GetModuleBase("client.dll");
    
    // Read memory
    int health = drv.Read<int>(base + 0x1000);
    
    // Write memory
    drv.Write<float>(base + 0x2000, 999.0f);
    
    // Move mouse
    drv.MoveMouse(10, -5);
    
    drv.Detach();
    return 0;
}
```

## 🚀 Advanced Features

### CR3 Caching
```cpp
// Automatically caches CR3 on first access
// Subsequent operations use cached value
// ~1000x faster than bruteforce
```

### Smart Memory Operations
```cpp
// Handles page boundaries automatically
// Supports large pages (2MB/1GB)
// Validates translations before R/W
```

### Module Fast Search
```cpp
// Uses PEB walking with StackAttach
// Case-insensitive comparison
// Length-based filtering
```

## 🐛 Troubleshooting

**Blue Screen on Load:**
- Check PatchGuard bypass is active
- Verify driver signature is correct
- Check target driver exists

**Can't Find Process:**
- Process might be protected
- Check process name is correct
- Try elevated privileges

**Read/Write Fails:**
- CR3 might be invalid (EAC shuffling)
- Memory might be paged out
- Address might be invalid

## 📚 Credits

Based on research from:
- BEKernelDriverUpdated (i32-Sudo)
- CHEESE DRV (various contributors)
- CR3 research (community)
- Physical memory techniques (various)

## ⚖️ License

MIT License - Educational purposes only
Use responsibly and legally

## 🔗 Links

- [Report Issues](https://github.com/yourusername/UndetectedDriver/issues)
- [Documentation](https://github.com/yourusername/UndetectedDriver/wiki)
- [Discord](https://discord.gg/yourserver)

---
**⚠️ DISCLAIMER:** This software is for educational and research purposes only. Unauthorized use of this software to gain unfair advantages in online games or to bypass security systems may violate terms of service and local laws. Use at your own risk.
