// Owned Vulkan-shaped provider; never calls Windows power or graphics APIs.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>

extern "C" DWORD WINAPI vkEnumerateInstanceVersion(std::uint32_t* value) {
    if (!value) return 0xBAD;
    *value = 0x12345678;
    SetLastError(0xA123);
    return 0xA321;
}
extern "C" FARPROC WINAPI vkGetInstanceProcAddr(void* instance, const char* name) {
    decltype(&vkEnumerateInstanceVersion) function = &vkEnumerateInstanceVersion;
    FARPROC result{};
    static_assert(sizeof(result) == sizeof(function));
    std::memcpy(&result, &function, sizeof(result));
    SetLastError(0xB123);
    return instance == reinterpret_cast<void*>(0x5678) && name && !std::strcmp(name,"vkEnumerateInstanceVersion") ? result : nullptr;
}
extern "C" double ProbeMix(std::uint64_t a, double b, std::uint64_t c, double d,
                            std::uint64_t e, double f, const std::uint64_t* p) {
    SetLastError(0xC123);
    return static_cast<double>(a + 3*c + 5*e + (p ? *p : 0)) + 7*b + 11*d + 13*f;
}
extern "C" std::uint64_t ProbeOrdinal(std::uint64_t value) {
    SetLastError(0xD123);
    return value ^ 0x1234567890ABCDEFULL;
}
