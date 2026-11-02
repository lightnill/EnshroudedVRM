// Scoped executable-write/near-allocation approach adapted from ShroudForge
// 46b59da1b3642f91fee3808c513efb996ec33f23, engine-hooks/*.cpp.
// MIT License, Copyright (c) 2026 bonsaibauer
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include "native_relative_call.hpp"
#include <algorithm>
#include <cstring>
namespace evrm {namespace {
using U=std::uintptr_t;using Code=std::array<unsigned char,5>;
bool read(U p,void* out,std::size_t n)noexcept {SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),out,n,&got)&&got==n;}
bool same(U p,const Code& bytes)noexcept {Code actual{};return read(p,actual.data(),actual.size())&&actual==bytes;}
bool relative(U site,U target,std::int32_t& out)noexcept {const auto delta=std::int64_t(target)-std::int64_t(site)-5;if(delta<INT32_MIN||delta>INT32_MAX)return false;out=static_cast<std::int32_t>(delta);return true;}
bool guarded(U site,const Code& before,const Code& after)noexcept {
    // No allocator or C++ lock while other native threads are suspended.
    std::array<HANDLE,2048> threads{};std::size_t count=0,suspended=0;bool safe=true,result=false;
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
    THREADENTRY32 entry{};entry.dwSize=sizeof entry;
    if(!Thread32First(snapshot,&entry))safe=false;
    else do{if(entry.th32OwnerProcessID!=GetCurrentProcessId()||entry.th32ThreadID==GetCurrentThreadId())continue;
        if(count==threads.size()){safe=false;break;}
        const auto handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if(!handle){safe=false;break;}threads[count++]=handle;
    }while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);
    if(safe)for(std::size_t i=0;i<count;++i){if(SuspendThread(threads[i])==DWORD(-1)){safe=false;break;}++suspended;
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(threads[i],&context)||(context.Rip>=site&&context.Rip<site+after.size())){safe=false;break;}}
    DWORD previous=0;
    if(safe&&same(site,before)&&VirtualProtect(reinterpret_cast<void*>(site),after.size(),PAGE_EXECUTE_READWRITE,&previous)){
        std::memcpy(reinterpret_cast<void*>(site),after.data(),after.size());FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site),after.size());
        DWORD ignored=0;result=VirtualProtect(reinterpret_cast<void*>(site),after.size(),previous,&ignored)!=FALSE;
        if(!result){std::memcpy(reinterpret_cast<void*>(site),before.data(),before.size());FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site),before.size());VirtualProtect(reinterpret_cast<void*>(site),before.size(),previous,&ignored);}
    }
    for(std::size_t i=0;i<suspended;++i)ResumeThread(threads[i]);
    for(std::size_t i=0;i<count;++i)CloseHandle(threads[i]);
    return result;
}
bool replace(U site,const Code& before,const Code& after)noexcept {
    for(unsigned i=0;i<8;++i){if(guarded(site,before,after))return true;if(!same(site,before))return false;Sleep(2);}return false;
}
void* near_page(U site)noexcept {
    SYSTEM_INFO system{};GetSystemInfo(&system);const U granularity=system.dwAllocationGranularity,range=0x7fff0000;
    const auto minimum=std::max<U>(reinterpret_cast<U>(system.lpMinimumApplicationAddress),site>range?site-range:0);
    const auto maximum=std::min<U>(reinterpret_cast<U>(system.lpMaximumApplicationAddress),site<=UINTPTR_MAX-range?site+range:site);
    for(U cursor=minimum;cursor<maximum;){MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(reinterpret_cast<void*>(cursor),&info,sizeof info))break;
        const U region=reinterpret_cast<U>(info.BaseAddress),end=region+info.RegionSize,candidate=(region+granularity-1)&~(granularity-1);
        if(info.State==MEM_FREE&&candidate>=minimum&&candidate+4096<=end&&candidate+4096<=maximum)
            if(auto page=VirtualAlloc(reinterpret_cast<void*>(candidate),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return page;
        if(end<=cursor)break;
        cursor=end;
    }return nullptr;
}
}
bool NativeRelativeCall::install(U site,const Code& before,U original,U wrapper)noexcept {
    if(installed_)return site==site_&&original==original_&&wrapper==wrapper_&&before==before_&&same(site_,after_);
    std::int32_t original_delta=0,actual_delta=0;
    if(!site||!original||!wrapper||before[0]!=0xe8||!relative(site,original,original_delta))return false;
    std::memcpy(&actual_delta,before.data()+1,4);if(actual_delta!=original_delta||!same(site,before))return false;
    if(site==site_&&original==original_&&wrapper==wrapper_&&before==before_){if(!replace(site_,before_,after_))return false;installed_=true;return true;}
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(wrapper),&pinned))return false;
    auto* page=static_cast<unsigned char*>(near_page(site));if(!page)return false;
    const unsigned char jump[]={0xff,0x25,0,0,0,0};std::memcpy(page,jump,sizeof jump);std::memcpy(page+6,&wrapper,sizeof wrapper);
    DWORD old=0;std::int32_t relay=0;
    if(!relative(site,reinterpret_cast<U>(page),relay)||!VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old)){VirtualFree(page,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),page,14);Code after{0xe8,0,0,0,0};std::memcpy(after.data()+1,&relay,4);
    if(!replace(site,before,after)){VirtualFree(page,0,MEM_RELEASE);return false;}
    site_=site;original_=original;wrapper_=wrapper;before_=before;after_=after;installed_=true;return true;
}
bool NativeRelativeCall::restore()noexcept {
    if(!installed_)return true;
    if(!replace(site_,after_,before_))return false;
    installed_=false;return true;
}
}
