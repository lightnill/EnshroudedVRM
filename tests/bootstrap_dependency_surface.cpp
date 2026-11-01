#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>
#include <dbghelp.h>
#include <dinput.h>
#include <oleauto.h>
#include <winldap.h>
#include <bcrypt.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <vulkan/vulkan.h>
// Retain ordinary imports but never invoke these APIs. This exercises system
// dependency initialization before/alongside the power proxy, rather than just
// a child importing the two power functions in isolation.
static std::uintptr_t volatile references[]={
    reinterpret_cast<std::uintptr_t>(&SHGetKnownFolderPath),reinterpret_cast<std::uintptr_t>(&GetOpenFileNameW),
    reinterpret_cast<std::uintptr_t>(&MiniDumpWriteDump),reinterpret_cast<std::uintptr_t>(&DirectInput8Create),
    reinterpret_cast<std::uintptr_t>(&VariantInit),reinterpret_cast<std::uintptr_t>(&ldap_initW),
    reinterpret_cast<std::uintptr_t>(&BCryptOpenAlgorithmProvider),reinterpret_cast<std::uintptr_t>(&GetUserNameW),
    reinterpret_cast<std::uintptr_t>(&GetForegroundWindow),reinterpret_cast<std::uintptr_t>(&GetStockObject),
    reinterpret_cast<std::uintptr_t>(&CoInitializeEx),reinterpret_cast<std::uintptr_t>(&WSAStartup),
    reinterpret_cast<std::uintptr_t>(&vkGetInstanceProcAddr),
};
int wmain() {
    for(const auto address:references)if(!address)return 2;
    wchar_t got[32768]{},expected[32768]{};
    const auto a=GetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH",got,32768);
    const auto b=GetEnvironmentVariableW(L"EVRM_EXPECT_LAYER_PATH",expected,32768);
    if(!a || a>=32768 || !b || b>=32768 || std::wstring(got,a)!=std::wstring(expected,b))return 10;
    std::cout<<"bootstrap ran before main with ordinary system dependency surface; no power API called\n";return 0;
}
