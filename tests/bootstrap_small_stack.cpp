// Owned, headless regression: Windows calls loaded DLLs on small worker stacks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

struct Work {
    const wchar_t* path{};
    HMODULE module{};
    volatile LONG entered=0;
    const char* export_name="vkGetInstanceProcAddr";
};
static DWORD WINAPI worker(void* opaque) {
    auto& work=*static_cast<Work*>(opaque);
    const HMODULE module=work.module?work.module:LoadLibraryW(work.path);
    if(!module)return 11;
    const bool valid=GetProcAddress(module,work.export_name)!=nullptr;
    if(!work.module)FreeLibrary(module);
    if(!valid)return 12;
    InterlockedIncrement(&work.entered);
    return 0;
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(GetErrorMode()|0x8003);
    if(argc!=5 && argc!=6)return 2;
    const bool notify=std::wcscmp(argv[1],L"notify")==0;
    if(!notify&&std::wcscmp(argv[1],L"load")!=0)return 3;
    const SIZE_T reserve=static_cast<SIZE_T>(std::wcstoull(argv[3],nullptr,10));
    const unsigned count=static_cast<unsigned>(std::wcstoul(argv[4],nullptr,10));
    if(reserve<65536||reserve>1048576||!count||count>32)return 4;
    Work work;work.path=argv[2];
    char export_name[64]{};
    if(argc==6) {
        if(!WideCharToMultiByte(CP_UTF8,0,argv[5],-1,export_name,64,nullptr,nullptr))return 10;
        work.export_name=export_name;
    }
    if(notify){work.module=LoadLibraryW(work.path);if(!work.module)return 5;}
    HANDLE threads[32]{};
    for(unsigned index=0;index<count;++index){
        threads[index]=CreateThread(nullptr,reserve,worker,&work,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
        if(!threads[index])return 6;
    }
    if(WaitForMultipleObjects(count,threads,TRUE,15000)!=WAIT_OBJECT_0)return 7;
    for(unsigned index=0;index<count;++index){
        DWORD exit_code=99;
        if(!GetExitCodeThread(threads[index],&exit_code)||exit_code)return 8;
        CloseHandle(threads[index]);
    }
    if(work.entered!=static_cast<LONG>(count))return 9;
    if(work.module)FreeLibrary(work.module);
    std::printf("small-stack %ls: %u threads, %llu-byte reservation, attach/detach and export lookup passed\n",
                argv[1],count,static_cast<unsigned long long>(reserve));
    return 0;
}
