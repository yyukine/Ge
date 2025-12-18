# 🔧 Installation & Build Guide

## Prerequisites

### Required Software
1. **Visual Studio 2022** (Community/Professional/Enterprise)
   - C++ Desktop Development workload
   - Windows SDK (latest)
   
2. **Windows Driver Kit (WDK)**
   - Download from: https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk
   - Must match your Visual Studio version
   - Install WDK extensions for VS2022

3. **Test Mode / Test Signing**
   ```cmd
   bcdedit /set testsigning on
   bcdedit /set nointegritychecks on
   ```
   Reboot after running these commands.

### Optional (for production)
- **VMProtect** or **Themida** - Code obfuscation
- **PdFwKrnl Mapper** - Kernel driver mapper
- **EFIGuard** or **DSEFix** - PatchGuard bypass

## Building the Driver

### Step 1: Configure the Project

1. Open `kernel/Core/config.h`
2. Modify these settings:

```cpp
// Choose target driver for hijacking
#define TARGET_DRIVER_HOOK_PEAUTH    // or GPUENERGY, IQVW64E

// Change these GUIDs to random values!
#define DEVICE_NAME_STRING     L"\\Device\\{YOUR-GUID-HERE}"
#define SYMBOLIC_LINK_STRING   L"\\??\\{YOUR-GUID-HERE}"

// Set your usermode executable name
#define PROCESS_TO_HIDE        L"your_client.exe"

// Security features
#define ENABLE_PROCESS_HIDING      1
#define ENABLE_DRIVER_CLEANING     1
#define ENABLE_STACK_SPOOFING      1
```

### Step 2: Build Kernel Driver

1. Open Visual Studio 2022
2. File → Open → Project/Solution
3. Create new WDK project or use existing
4. Add all kernel files to project:
   - `kernel/Core/*.cpp`
   - `kernel/Memory/*.cpp`
   - `kernel/Process/*.cpp`
   - `kernel/Communication/*.cpp`
   - `kernel/Utils/*.h`

5. Project Properties:
   ```
   Configuration: Release
   Platform: x64
   
   C/C++ → General:
   - Warning Level: Level3 (/W3)
   
   C/C++ → Optimization:
   - Optimization: Maximum Optimization (/O2)
   - Inline Function Expansion: Any Suitable (/Ob2)
   
   C/C++ → Code Generation:
   - Runtime Library: Multi-threaded (/MT)
   
   Linker → Input:
   - Additional Dependencies: ntoskrnl.lib;hal.lib;wdmsec.lib
   
   Driver Settings → General:
   - Target OS Version: Windows 10
   - Target Platform: Desktop
   ```

6. Build → Build Solution (Ctrl+Shift+B)

7. Output: `x64/Release/UndetectedDriver.sys`

### Step 3: Sign the Driver (Test Mode)

```cmd
cd x64\Release

:: Create self-signed certificate
makecert -r -pe -ss PrivateCertStore -n "CN=TestDriverCert" TestCert.cer

:: Sign the driver
signtool sign /v /s PrivateCertStore /n "TestDriverCert" /t http://timestamp.digicert.com UndetectedDriver.sys

:: Verify signature
signtool verify /v /pa UndetectedDriver.sys
```

### Step 4: Build Usermode Library

1. Create new C++ Console Application project
2. Add files:
   - `usermode/driver.h`
   - `usermode/driver.cpp`
   - `usermode/example.cpp`
   - `shared/definitions.h`

3. Project Properties:
   ```
   Configuration: Release
   Platform: x64
   
   C/C++ → General:
   - Additional Include Directories: ../shared
   
   C/C++ → Preprocessor:
   - Preprocessor Definitions: _UNICODE;UNICODE
   
   Linker → General:
   - SubSystem: Console
   ```

4. Build → Build Solution

5. Output: `x64/Release/client.exe`

## Loading the Driver

### Method 1: Service Control Manager (Test Mode)

```cmd
:: Create service
sc create UndetectedDriver binPath= "C:\path\to\UndetectedDriver.sys" type= kernel

:: Start driver
sc start UndetectedDriver

:: Check status
sc query UndetectedDriver

:: Stop driver
sc stop UndetectedDriver

:: Delete service
sc delete UndetectedDriver
```

### Method 2: OSR Driver Loader

1. Download OSR Driver Loader: https://www.osronline.com/article.cfm%5Earticle=157.htm
2. Run as Administrator
3. Browse to `.sys` file
4. Click "Register Service"
5. Click "Start Service"

### Method 3: Kernel Mapper (Production)

For undetected loading without test signing:

```cpp
// Using PdFwKrnl or similar mapper
mapper.exe UndetectedDriver.sys
```

**Note:** Requires vulnerable driver to be present (e.g., iqvw64e.sys)

## Testing

### Quick Test

```cmd
:: Load driver
sc start UndetectedDriver

:: Run usermode client
cd x64\Release
client.exe

:: Check if it works
:: Should see process information and memory reads
```

### Advanced Test

1. Start target process (e.g., notepad.exe)
2. Run your usermode application
3. Attach to process
4. Perform memory operations
5. Check DbgView for debug output (if enabled)

## Troubleshooting

### Driver Won't Load

**Error: Code 52 (Windows cannot verify the digital signature)**
```cmd
:: Enable test signing
bcdedit /set testsigning on
bcdedit /set nointegritychecks on
:: Reboot
```

**Error: Code 39 (Driver failed to load)**
- Check dependencies (make sure all kernel functions are resolved)
- Verify target OS version matches driver
- Check DebugView for error messages

**Blue Screen on Load**
- Disable PatchGuard checks or use bypass
- Check for null pointer dereferences
- Verify all memory allocations
- Enable Driver Verifier to catch issues:
  ```cmd
  verifier /standard /driver UndetectedDriver.sys
  ```

### Connection Fails

**Client can't connect to driver**
```cpp
// Check symbolic link name matches
// In config.h: L"\\??\\{GUID}"
// In client: L"\\\\.\\{GUID}" (note the format difference!)
```

**Access Denied**
- Run client as Administrator
- Check driver loaded: `sc query UndetectedDriver`
- Verify device created: `WinObj` → \Device\ and \??\ folders

### Memory Operations Fail

**Read/Write returns failure**
- Ensure process attached first: `driver.Attach(pid)`
- Check CR3 is valid (not 0)
- Verify address is valid in target process
- Check process is not protected (PPL)

**Invalid CR3**
- Process might be terminating
- CR3 cache expired (EAC shuffling)
- Try invalidating cache and re-getting CR3

## Production Deployment

### Step 1: Code Obfuscation

Use VMProtect to protect the driver:

```
1. Load UndetectedDriver.sys in VMProtect
2. Add markers around sensitive code
3. Enable:
   - Mutation
   - Virtualization (for critical functions)
   - Memory protection
4. Pack
```

### Step 2: String Encryption

All strings in code are already prepared for encryption:
```cpp
#define ENABLE_STRING_ENCRYPTION   1
```

Use skCrypter or similar library for compile-time string encryption.

### Step 3: PatchGuard Bypass

Load EFIGuard or use another PatchGuard bypass before loading your driver.

### Step 4: Clean Traces

Enable in config.h:
```cpp
#define ENABLE_DRIVER_CLEANING     1
```

This will clean:
- Registry entries
- Loaded module lists
- Vulnerable driver artifacts

### Step 5: Final Build

```
1. Disable all logging:
   #define ENABLE_LOGGING 0

2. Enable all anti-detection:
   #define ENABLE_PROCESS_HIDING      1
   #define ENABLE_THREAD_HIDING       1
   #define ENABLE_STACK_SPOOFING      1

3. Build in Release x64

4. Obfuscate with VMProtect

5. Sign with valid certificate (or map unsigned)

6. Test on clean VM

7. Deploy
```

## Security Warnings

⚠️ **IMPORTANT:**
- This driver provides kernel-level access
- Can be detected by anti-cheat systems
- Use only on authorized systems
- Misuse can result in system instability
- May violate ToS of online games
- For educational/research purposes only

## Support

For issues, check:
- Windows Event Viewer (System logs)
- DebugView (kernel debug output)
- Driver Verifier logs
- Memory dumps (if BSOD occurs)

## License

MIT License - Use responsibly

---

**Last Updated:** 2024
**Author:** Advanced Driver Research Team
