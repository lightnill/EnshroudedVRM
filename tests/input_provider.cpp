#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct IUnknown;
#ifdef EVRM_CHAIN_PROVIDER
static void marker(char value) {
    wchar_t path[2048];
    const auto count=GetEnvironmentVariableW(L"EVRM_CHAIN_MARKER",path,2048);
    if(!count || count>=2048)return;
    const auto file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;WriteFile(file,&value,1,&written,nullptr);CloseHandle(file);
}
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,void*) {
    if(reason==DLL_PROCESS_ATTACH) {
        marker('L');
        const auto root=GetModuleHandleW(L"dinput8.dll");
        const auto get=reinterpret_cast<const void*(WINAPI*)()>(GetProcAddress(root,"GetdfDIJoystick"));
        if(get && get()!=reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345680)))marker('R');
    }
    return TRUE;
}
#endif
extern "C" {
HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID id, void** output, IUnknown* outer) {
    if (!instance || version != 0 || id.Data1 != 0 || outer) return E_INVALIDARG;
#ifdef EVRM_CHAIN_PROVIDER
    const auto expected=reinterpret_cast<const GUID*(WINAPI*)()>(GetProcAddress(instance,"FixtureGuidAddress"));
    if(expected && expected() && expected()!=&id)return E_POINTER;
#endif
#ifdef EVRM_CHAIN_REENTER
    wchar_t path[32768];const auto count=GetModuleFileNameW(nullptr,path,32768);
    unsigned slash=count;while(slash&&path[slash-1]!=L'\\')--slash;
    const wchar_t name[]=L"dinput8.dll";
    if(!slash||slash+sizeof(name)/sizeof(wchar_t)>32768)return E_FAIL;
    for(unsigned i=0;i<sizeof(name)/sizeof(wchar_t);++i)path[slash+i]=name[i];
    const auto root=GetModuleHandleW(path);
    const auto create=reinterpret_cast<HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*)>(GetProcAddress(root,"DirectInput8Create"));
    void* nested=nullptr;
    if(!create || create(instance,version,id,&nested,outer)==0x11223344)return E_UNEXPECTED;
#endif
    *output = reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345678));
    SetLastError(0x11110001); return 0x11223344;
}
HRESULT WINAPI DllCanUnloadNow() { SetLastError(0x11110002); return 0x11223345; }
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** output) {
    if (clsid.Data1 || iid.Data1) return E_INVALIDARG;
    *output = reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345679));
    SetLastError(0x11110003); return 0x11223346;
}
HRESULT WINAPI DllRegisterServer() { SetLastError(0x11110004); return 0x11223347; }
HRESULT WINAPI DllUnregisterServer() { SetLastError(0x11110005); return 0x11223348; }
const void* WINAPI GetdfDIJoystick() { SetLastError(0x11110006); return reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345680)); }
}
