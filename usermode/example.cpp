#include "driver.h"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "[+] UndetectedDriver Usermode Example\n\n";

    // Create driver instance
    Driver driver;

    // Connect to driver
    std::cout << "[*] Connecting to driver...\n";
    if (!driver.Connect()) {
        std::cerr << "[-] Failed to connect to driver. Error: " << driver.GetLastError() << "\n";
        std::cerr << "[-] Make sure the driver is loaded!\n";
        return 1;
    }
    std::cout << "[+] Connected to driver\n\n";

    // Find target process
    std::wstring targetProcess = L"notepad.exe"; // Change this
    std::cout << "[*] Looking for process: " << std::string(targetProcess.begin(), targetProcess.end()) << "\n";
    
    ULONG pid = driver.FindProcessId(targetProcess);
    if (pid == 0) {
        std::cerr << "[-] Process not found!\n";
        return 1;
    }
    std::cout << "[+] Found process. PID: " << pid << "\n\n";

    // Get process base
    std::cout << "[*] Getting process base address...\n";
    ULONGLONG base = driver.GetProcessBase(pid);
    if (base == 0) {
        std::cerr << "[-] Failed to get process base\n";
        return 1;
    }
    std::cout << "[+] Process base: 0x" << std::hex << base << std::dec << "\n\n";

    // Get CR3
    std::cout << "[*] Getting CR3...\n";
    ULONGLONG cr3 = driver.GetCR3(pid);
    if (cr3 == 0) {
        std::cerr << "[-] Failed to get CR3\n";
        return 1;
    }
    std::cout << "[+] CR3: 0x" << std::hex << cr3 << std::dec << "\n\n";

    // Attach to process
    std::cout << "[*] Attaching to process...\n";
    if (!driver.Attach(pid)) {
        std::cerr << "[-] Failed to attach to process\n";
        return 1;
    }
    std::cout << "[+] Attached successfully\n\n";

    // Get module base (example: kernel32.dll)
    std::cout << "[*] Getting kernel32.dll base...\n";
    ULONGLONG kernel32 = driver.GetModuleBase(pid, L"kernel32.dll");
    if (kernel32 != 0) {
        std::cout << "[+] kernel32.dll: 0x" << std::hex << kernel32 << std::dec << "\n\n";
    } else {
        std::cout << "[-] kernel32.dll not found\n\n";
    }

    // Read memory example (read DOS header)
    std::cout << "[*] Reading DOS header...\n";
    WORD dosSignature = driver.Read<WORD>(base);
    if (dosSignature == 0x5A4D) { // 'MZ'
        std::cout << "[+] DOS Signature: MZ (0x5A4D) - Valid!\n";
        
        // Read e_lfanew offset
        DWORD e_lfanew = driver.Read<DWORD>(base + 0x3C);
        std::cout << "[+] e_lfanew: 0x" << std::hex << e_lfanew << std::dec << "\n";
        
        // Read NT signature
        DWORD ntSignature = driver.Read<DWORD>(base + e_lfanew);
        if (ntSignature == 0x4550) { // 'PE'
            std::cout << "[+] NT Signature: PE (0x4550) - Valid!\n\n";
        }
    } else {
        std::cout << "[-] Invalid DOS signature: 0x" << std::hex << dosSignature << std::dec << "\n\n";
    }

    // Write memory example (DANGEROUS - for demo only)
    /*
    std::cout << "[*] Writing memory (demo)...\n";
    ULONGLONG testValue = 0x1337DEADBEEF;
    ULONGLONG testAddress = base + 0x1000; // Some offset
    if (driver.Write<ULONGLONG>(testAddress, testValue)) {
        std::cout << "[+] Write successful\n";
        
        // Read back
        ULONGLONG readBack = driver.Read<ULONGLONG>(testAddress);
        std::cout << "[+] Read back: 0x" << std::hex << readBack << std::dec << "\n\n";
    } else {
        std::cout << "[-] Write failed\n\n";
    }
    */

    // Read array example
    std::cout << "[*] Reading array of bytes...\n";
    auto bytes = driver.ReadArray<BYTE>(base, 16);
    if (!bytes.empty()) {
        std::cout << "[+] First 16 bytes: ";
        for (auto byte : bytes) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
        }
        std::cout << std::dec << "\n\n";
    }

    // Cleanup
    std::cout << "[*] Detaching from process...\n";
    driver.Detach();
    std::cout << "[+] Detached\n\n";

    std::cout << "[+] All operations completed successfully!\n";
    std::cout << "\nPress Enter to exit...";
    std::cin.get();

    return 0;
}
