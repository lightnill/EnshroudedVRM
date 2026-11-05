#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
extern "C" __declspec(dllimport) HRESULT WINAPI DirectInput8Create(HINSTANCE,DWORD,REFIID,void**,void*);
extern "C" HRESULT(WINAPI* __imp_DirectInput8Create)(HINSTANCE,DWORD,REFIID,void**,void*);
extern "C" __declspec(dllexport) HMODULE ImportedInputModule() {
    MEMORY_BASIC_INFORMATION memory{};const void* address=nullptr;
    static_assert(sizeof(address)==sizeof(__imp_DirectInput8Create),"Windows x64 pointer layout");
    std::memcpy(&address,&__imp_DirectInput8Create,sizeof(address));
    return VirtualQuery(address,&memory,sizeof(memory))?static_cast<HMODULE>(memory.AllocationBase):nullptr;
}
