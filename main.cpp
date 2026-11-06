#include <windows.h>
#include <iostream>
#include <vector>

class MemoryEngine {
private:
    HANDLE hProcess;
    DWORD targetPid;

public:
    MemoryEngine() : hProcess(nullptr), targetPid(0) {}

    ~MemoryEngine() {
        if (hProcess) {
            CloseHandle(hProcess);
        }
    }

    bool attach(DWORD pid) {
        targetPid = pid;
        // Request read, write, and query permissions
        hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, targetPid);
        return (hProcess != nullptr);
    }

    std::vector<uintptr_t> scanInitial(double targetValue) {
        std::vector<uintptr_t> results;
        if (!hProcess) return results;

        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);

        uintptr_t currentAddress = (uintptr_t)sysInfo.lpMinimumApplicationAddress;
        uintptr_t maxAddress = (uintptr_t)sysInfo.lpMaximumApplicationAddress;

        MEMORY_BASIC_INFORMATION mbi;

        std::cout << "[*] Starting initial double memory scan for value: " << targetValue << "...\n";

        while (currentAddress < maxAddress) {
            if (VirtualQueryEx(hProcess, (LPCVOID)currentAddress, &mbi, sizeof(mbi)) == sizeof(mbi)) {
                // Look for committed, readable/writable private or mapped memory
                if (mbi.State == MEM_COMMIT &&
                    (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {

                    std::vector<char> buffer(mbi.RegionSize);
                    SIZE_T bytesRead = 0;

                    if (ReadProcessMemory(hProcess, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead)) {
                        // Scan the buffer for our 8-byte double value (stepping by 8, or 1 for unaligned)
                        for (size_t i = 0; i <= bytesRead - sizeof(double); i += 8) {
                            double val = *reinterpret_cast<double*>(&buffer[i]);
                            if (val == targetValue) {
                                uintptr_t foundAddress = currentAddress + i;
                                results.push_back(foundAddress);
                            }
                        }
                    }
                }
                currentAddress = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            } else {
                break;
            }
        }

        std::cout << "[*] Scan complete. Found " << results.size() << " matching addresses.\n";
        return results;
    }

    bool writeValue(uintptr_t address, double newValue) {
        if (!hProcess) return false;
        SIZE_T bytesWritten = 0;
        return WriteProcessMemory(hProcess, (LPVOID)address, &newValue, sizeof(double), &bytesWritten) && (bytesWritten == sizeof(double));
    }

    template <typename T>
    T readValue(uintptr_t address) {
        T value = {};
        if (!hProcess) return value;

        SIZE_T bytesRead = 0;
        ReadProcessMemory(hProcess, (LPCVOID)address, &value, sizeof(T), &bytesRead);
        return value;
    }
};

int main() {
    MemoryEngine engine;
    DWORD pid = 0;

    std::cout << "Enter Target Process ID (PID): ";
    std::cin >> pid;

    if (!engine.attach(pid)) {
        std::cerr << "[-] Failed to open process. Ensure you have proper privileges (run as administrator if needed).\n";
        return 1;
    }

    std::cout << "[+] Successfully attached to PID: " << pid << "\n";

    double searchVal = 0.0;
    std::cout << "Enter initial value to search for (double): ";
    std::cin >> searchVal;

    std::vector<uintptr_t> addresses = engine.scanInitial(searchVal);
    size_t lastSize = 0;

    while (addresses.size() > 1 && addresses.size() != lastSize) {
        if (addresses.empty()) {
            std::cout << "[-] No addresses remaining.\n";
            return -1;
        }

        std::cout << "\nFound " << addresses.size() << " potential addresses.\n";
        std::cout << "Enter new value to filter by: ";
        std::cin >> searchVal;

        // Safely filter addresses using the fixed iterator-erasure loop
        for (auto it = addresses.begin(); it != addresses.end(); ) {
            if (engine.readValue<double>(*it) != searchVal) {
                it = addresses.erase(it);
            } else {
                ++it;
            }
        }

        lastSize = addresses.size();
    }

    if (addresses.empty()) {
        std::cout << "[-] No addresses found matching that value.\n";
        return 0;
    }

    double newVal = 0.0;
    std::cout << "Enter new double value: ";
    std::cin >> newVal;
    for (const auto it : addresses) {
        if (engine.writeValue(it, newVal)) {
            std::cout << "[+] Value modified successfully at 0x" << std::hex << addresses[0] << std::dec << "!\n";
        } else {
            std::cerr << "[-] Failed to write to memory address.\n";
        }
    }

    return 0;
}