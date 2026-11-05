#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" void __cdecl __security_init_cookie();
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID);

// This bootstrap has only constant-initialized storage and Win32 operations.
// It needs neither the general CRT process setup nor CRT TLS destructors.
// Initialize MSVC's genuine /GS cookie before entering any protected function.
// The entry itself owns no buffers and must not check a pre-initialization cookie.
extern "C" __declspec(safebuffers) BOOL WINAPI EvrmInputEntry(HINSTANCE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) __security_init_cookie();
    return DllMain(module, reason, reserved);
}
