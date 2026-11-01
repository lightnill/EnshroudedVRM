#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

extern "C" __declspec(dllimport) DWORD WINAPI vkEnumerateInstanceVersion(std::uint32_t*);
extern "C" __declspec(dllimport) FARPROC WINAPI vkGetInstanceProcAddr(void*, const char*);
extern "C" __declspec(dllimport) double ProbeMix(std::uint64_t, double, std::uint64_t, double, std::uint64_t, double, const std::uint64_t*);
extern "C" __declspec(dllimport) DWORD WINAPI ProbeAttachResult();

static std::wstring environment(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (!length) return {};
    std::wstring value(length, L'\0');
    const auto got = GetEnvironmentVariableW(name, value.data(), length);
    value.resize(got); return value;
}
static bool calls() {
    std::uint32_t value = 0;
    const auto result = vkEnumerateInstanceVersion(&value);
    const auto error = GetLastError();
    if (result != 0xA321 || error != 0xA123 || value != 0x12345678) return false;
    const auto set = vkGetInstanceProcAddr(reinterpret_cast<void*>(0x5678), "vkEnumerateInstanceVersion");
    const auto set_error = GetLastError();
    const std::uint64_t item = 17;
    const auto mixed = ProbeMix(2, 0.5, 3, 1.5, 5, 2.5, &item);
    const auto mixed_error = GetLastError();
    const auto proxy = GetModuleHandleW(L"vulkan-1.dll");
    const auto address = GetProcAddress(proxy, MAKEINTRESOURCEA(37));
    std::uint64_t(*ordinal)(std::uint64_t) = nullptr;
    static_assert(sizeof(ordinal) == sizeof(address));
    std::memcpy(&ordinal, &address, sizeof(ordinal));
    if (!ordinal) return false;
    const auto ordinal_value = ordinal(0x321);
    return set == GetProcAddress(proxy,"vkEnumerateInstanceVersion") && set_error == 0xB123 && mixed == 105.5 && mixed_error == 0xC123 &&
           ordinal_value == (0x321 ^ 0x1234567890ABCDEFULL) && GetLastError() == 0xD123;
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(GetErrorMode() | 0x8003);
    const auto path = environment(L"VK_ADD_IMPLICIT_LAYER_PATH");
    if (path != environment(L"EVRM_EXPECT_LAYER_PATH")) {
        std::wcerr << L"early path mismatch: " << path << L" expected " << environment(L"EVRM_EXPECT_LAYER_PATH") << L'\n'; return 10;
    }
    if (argc > 1 && std::wstring(argv[1]) == L"none") return 0;
    if (argc > 1 && std::wstring(argv[1]) == L"hold") { std::cout << "holding checked manifest" << std::endl; std::cin.get(); return 0; }
    if (ProbeAttachResult() != 1 || !calls()) { std::cerr << "ABI or dependency attach failed\n"; return 11; }
    std::vector<std::thread> threads;
    LONG failures = 0;
    for (int index = 0; index < 8; ++index) threads.emplace_back([&] { for (int n = 0; n < 100; ++n) if (!calls()) InterlockedIncrement(&failures); });
    for (auto& thread : threads) thread.join();
    if (failures) return 12;
    std::cout << "early path and forwarded ABI/dependent attach/concurrent calls passed\n";
    return 0;
}
