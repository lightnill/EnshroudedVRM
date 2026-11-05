#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <cstring>
struct IUnknown;
extern "C" __declspec(dllimport) HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
extern "C" __declspec(dllimport) int InputDependencyProbe();
extern "C" LPWSTR(WINAPI* __imp_GetCommandLineW)();

extern "C" __declspec(dllexport) __declspec(noinline) LPWSTR FixtureTrustedStartup() {
    auto result=GetCommandLineW();
    volatile unsigned first=result?static_cast<unsigned>(result[0])+7:0;
    (void)first; return result;
}
extern "C" __declspec(dllexport) __declspec(noinline) LPWSTR FixtureEarlyCall() {
    auto result=GetCommandLineW();
    volatile unsigned first=result?static_cast<unsigned>(result[0])+13:0;
    (void)first; return result;
}
static DWORD WINAPI wrong_thread(void*) { return FixtureTrustedStartup()?0:1; }
static const GUID* expected_guid=nullptr;
static DWORD main_thread=0;
extern "C" __declspec(dllexport) const GUID* WINAPI FixtureGuidAddress() {
    return GetCurrentThreadId()==main_thread?expected_guid:nullptr;
}
static DWORD WINAPI input_worker(void*) {
    for(unsigned i=0;i<64;++i) {
        const GUID id{};void* output=nullptr;SetLastError(0x66778899);
        if(DirectInput8Create(GetModuleHandleW(nullptr),0,id,&output,nullptr)!=0x11223344||
           output!=reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345678))||GetLastError()!=0x11110001)return 1;
    }
    return 0;
}
static std::wstring current_log_basename() {
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return {};
    const auto ticks=(static_cast<unsigned long long>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    std::wstring name=L"input-bootstrap-"+std::to_wstring(GetCurrentProcessId())+L"-";
    for(int shift=60;shift>=0;shift-=4)name+=L"0123456789abcdef"[(ticks>>shift)&15];
    return name+L".log";
}
static bool log_controls() {
    wchar_t target[32768],local[32768];
    const bool alias=GetEnvironmentVariableW(L"EVRM_CHAIN_LOG_ALIAS",target,32768)!=0;
    const bool stale=GetEnvironmentVariableW(L"EVRM_CHAIN_STALE_PID_LOG",nullptr,0)!=0;
    if(!alias&&!stale)return true;
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768))return false;
    const auto directory=std::wstring(local)+L"\\EnshroudedVRM\\State\\Logs";
    CreateDirectoryW(local,nullptr);CreateDirectoryW((std::wstring(local)+L"\\EnshroudedVRM").c_str(),nullptr);
    CreateDirectoryW((std::wstring(local)+L"\\EnshroudedVRM\\State").c_str(),nullptr);CreateDirectoryW(directory.c_str(),nullptr);
    if(stale) {
        const auto path=directory+L"\\input-bootstrap-"+std::to_wstring(GetCurrentProcessId())+L".log";
        const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        constexpr char old[]="input-bootstrap v1\nchain_active\nwin32=0\n";DWORD written=0;
        const bool saved=WriteFile(file,old,sizeof(old)-1,&written,nullptr)&&written==sizeof(old)-1;
        CloseHandle(file);if(!saved)return false;
    }
    if(alias) {
        const auto name=current_log_basename();if(name.empty())return false;
        return CreateHardLinkW((directory+L"\\"+name).c_str(),target,nullptr)!=FALSE;
    }
    return true;
}

int wmain(int argc,wchar_t** argv) {
    const auto mode=argc>1?std::wstring(argv[1]):L"absent";
    const bool expect_chain=mode==L"chain"||mode==L"partial";
    const bool expect_mapped=expect_chain||mode==L"declined"||mode==L"non-create";
    main_thread=GetCurrentThreadId();
    if(!log_controls())return 10;
    wchar_t repair[32768];
    if(GetEnvironmentVariableW(L"EVRM_CHAIN_REPAIR_MANIFEST",repair,32768)&&
       !CopyFileW(repair,L"EnshroudedVRM\\Runtime\\enshrouded_vrm_layer.json",FALSE))return 22;
    if(InputDependencyProbe()!=1) { std::cerr<<"dependent initialization was unsafe\n";return 1; }
    if(GetModuleHandleW(L"dinput8.chainload.dll")) { std::cerr<<"chain loaded before startup\n";return 2; }
    const auto thread=CreateThread(nullptr,0,wrong_thread,nullptr,0,nullptr);
    if(!thread || WaitForSingleObject(thread,5000)!=WAIT_OBJECT_0)return 3;
    CloseHandle(thread);
    if(GetModuleHandleW(L"dinput8.chainload.dll")) { std::cerr<<"wrong thread activated chain\n";return 4; }
    const auto actual=__imp_GetCommandLineW;
    FARPROC address=nullptr;static_assert(sizeof(address)==sizeof(actual));std::memcpy(&address,&actual,sizeof(address));
    if(expect_chain && address==GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetCommandLineW")) {
        std::cerr<<"trusted gate is absent\n";return 11;
    }
    SetLastError(0x77889900);
    if(!FixtureTrustedStartup() || GetLastError()!=0x77889900) { std::cerr<<"startup result/last error changed\n";return 5; }
    if(expect_mapped && !GetModuleHandleW(L"dinput8.chainload.dll")) {
        const auto after=__imp_GetCommandLineW;std::memcpy(&address,&after,sizeof(address));
        std::cerr<<"chain was not loaded at trusted startup; gate "
                 <<(address==GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetCommandLineW")?"restored":"still installed")<<"\n";return 6;
    }
    if(!expect_mapped && GetModuleHandleW(L"dinput8.chainload.dll"))return 7;
    const GUID id{};void* output=nullptr;
    expected_guid=&id;
    SetLastError(0x77889900);
    const auto result=DirectInput8Create(GetModuleHandleW(nullptr),0,id,&output,nullptr);
    if(expect_chain && (result!=0x11223344 || output!=reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345678)) || GetLastError()!=0x11110001)) {
        std::cerr<<"input was not routed through the downstream provider\n";return 8;
    }
    if(!expect_chain && result==0x11223344)return 9;
    if(mode==L"non-create") {
        wchar_t system[32768];if(!GetSystemDirectoryW(system,32768))return 23;
        const auto provider=GetModuleHandleW((std::wstring(system)+L"\\dinput8.dll").c_str());
        using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
        const auto create=reinterpret_cast<Create>(GetProcAddress(provider,"DirectInput8Create"));
        if(!create)return 24;
        void* expected=nullptr;SetLastError(0x77889900);
        const auto native=create(GetModuleHandleW(nullptr),0,id,&expected,nullptr);const auto error=GetLastError();
        output=nullptr;SetLastError(0x77889900);
        if(DirectInput8Create(GetModuleHandleW(nullptr),0,id,&output,nullptr)!=native||output!=expected||GetLastError()!=error)return 25;
        wchar_t exe[32768];const auto count=GetModuleFileNameW(nullptr,exe,32768);
        auto path=std::wstring(exe,count);path.resize(path.find_last_of(L'\\')+1);path+=L"dinput8.dll";
        const auto root=GetModuleHandleW(path.c_str());
        const auto unload=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(root,"DllCanUnloadNow"));
        const auto object=reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(root,"DllGetClassObject"));
        const auto joystick=reinterpret_cast<const void*(WINAPI*)()>(GetProcAddress(root,"GetdfDIJoystick"));
        if(unload()!=0x11223345||GetLastError()!=0x11110002){std::cerr<<"usable non-create export bypassed\n";return 26;}
        if(object(id,id,&output)!=0x11223346||output!=reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345679))||GetLastError()!=0x11110003)return 27;
        if(joystick()!=reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345680))||GetLastError()!=0x11110006)return 28;
    }
    if(expect_chain) {
        HANDLE workers[8];
        for(auto& worker:workers) {worker=CreateThread(nullptr,0,input_worker,nullptr,0,nullptr);if(!worker)return 12;}
        if(WaitForMultipleObjects(8,workers,TRUE,10000)!=WAIT_OBJECT_0)return 13;
        for(auto worker:workers) {DWORD code=1;GetExitCodeThread(worker,&code);CloseHandle(worker);if(code)return 14;}
        wchar_t path[32768];const auto count=GetModuleFileNameW(nullptr,path,32768);
        unsigned slash=count;while(slash&&path[slash-1]!=L'\\')--slash;
        const wchar_t name[]=L"dinput8.dll";
        if(!slash||slash+sizeof(name)/sizeof(wchar_t)>32768)return 15;
        std::memcpy(path+slash,name,sizeof(name));
        const auto root=GetModuleHandleW(path);
        const auto unload=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(root,"DllCanUnloadNow"));
        const auto object=reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(root,"DllGetClassObject"));
        const auto reg=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(root,"DllRegisterServer"));
        const auto unreg=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(root,"DllUnregisterServer"));
        const auto joystick=reinterpret_cast<const void*(WINAPI*)()>(GetProcAddress(root,"GetdfDIJoystick"));
        if(mode==L"partial") {
            if(unload()==0x11223345||joystick()==reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345680)))return 16;
        } else {
            // Registration exports execute only this owned provider, never the
            // real Windows registration functions.
            if(unload()!=0x11223345||GetLastError()!=0x11110002)return 17;
            if(object(id,id,&output)!=0x11223346||GetLastError()!=0x11110003||output!=reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345679)))return 18;
            if(reg()!=0x11223347||GetLastError()!=0x11110004||unreg()!=0x11223348||GetLastError()!=0x11110005)return 19;
            if(joystick()!=reinterpret_cast<void*>(static_cast<ULONG_PTR>(0x12345680))||GetLastError()!=0x11110006)return 20;
        }
    }
    FixtureTrustedStartup();
    std::cout<<"dependent/TLS-safe startup, trusted thread, once-only chain and input routing passed\n";
}
