#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

extern "C" __declspec(dllimport) DWORD WINAPI vkEnumerateInstanceVersion(std::uint32_t*);
static DWORD attach_result = 0;
extern "C" __declspec(dllexport) DWORD WINAPI ProbeAttachResult() { return attach_result; }
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        std::uint32_t value = 0;
        const auto result = vkEnumerateInstanceVersion(&value);
        const auto error = GetLastError();
        attach_result = result == 0xA321 && error == 0xA123 && value == 0x12345678;
    }
    return TRUE;
}
