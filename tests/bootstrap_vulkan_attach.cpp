// Owned fixture: exercise one global lookup during ordinary dependency attach.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cwchar>

static unsigned observed = 0;
extern "C" __declspec(dllexport) unsigned ProbeEarlyState() { return observed; }
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t actual[32768]{}, expected[32768]{};
    const DWORD a = GetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH", actual, 32768);
    const DWORD b = GetEnvironmentVariableW(L"EVRM_EXPECT_LAYER_PATH", expected, 32768);
    if (a < 32768 && b < 32768 && a == b && std::wcscmp(actual, expected) == 0) observed |= 1;
    if (vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance")) observed |= 2;
    return TRUE;
}
