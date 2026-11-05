#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct IUnknown;
extern "C" __declspec(dllimport) HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
static int valid=0;
static int tls_valid=0;
static void marker(char value) {
    wchar_t path[2048];
    const auto count=GetEnvironmentVariableW(L"EVRM_CHAIN_MARKER",path,2048);
    if(!count || count>=2048)return;
    const auto file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;WriteFile(file,&value,1,&written,nullptr);CloseHandle(file);
}
static bool early_input() {
        const auto main=GetModuleHandleW(nullptr);
        const auto early=reinterpret_cast<LPWSTR(WINAPI*)()>(GetProcAddress(main,"FixtureEarlyCall"));
        if(!early || !early() || GetModuleHandleW(L"dinput8.chainload.dll"))return false;
        const GUID id{};void* output=nullptr;
        const auto result=DirectInput8Create(main,0,id,&output,nullptr);
        return result!=0x11223344;
}
static void NTAPI fixture_tls(void*,DWORD reason,void*) {
    if(reason==DLL_PROCESS_ATTACH && early_input()) {tls_valid=1;marker('T');}
}
#pragma section(".CRT$XLB",long,read)
extern "C" __declspec(allocate(".CRT$XLB")) const PIMAGE_TLS_CALLBACK fixture_tls_callback=fixture_tls;
#pragma comment(linker,"/INCLUDE:_tls_used")
#pragma comment(linker,"/INCLUDE:fixture_tls_callback")
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,void*) {
    if(reason==DLL_PROCESS_ATTACH && tls_valid && early_input()) {
        valid=1;marker('E');
    }
    return TRUE;
}
extern "C" __declspec(dllexport) int InputDependencyProbe() {return valid;}
