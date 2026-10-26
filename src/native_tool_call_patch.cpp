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
#include "native_tool_call_patch.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
namespace evrm { namespace {
using U=std::uintptr_t;
bool read(U address,void*out,size_t size){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&got)&&got==size;}
bool relative(U site,U target,std::int32_t&out){const auto diff=std::int64_t(target)-std::int64_t(site)-5;if(diff<INT32_MIN||diff>INT32_MAX)return false;out=static_cast<std::int32_t>(diff);return true;}
bool same(U address,const std::array<unsigned char,14>&expected){std::array<unsigned char,14> bytes{};return read(address,bytes.data(),bytes.size())&&bytes==expected;}
bool write_guarded(U address,const std::array<unsigned char,14>&expected,const std::array<unsigned char,14>&replacement)noexcept {
 // Fixed stack storage: no allocator or C++ locks while native threads pause.
 std::array<HANDLE,2048> threads{};size_t count=0,suspended=0;bool safe=true,result=false;
 const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
 THREADENTRY32 entry{};entry.dwSize=sizeof entry;
 if(!Thread32First(snapshot,&entry))safe=false;
 else do{if(entry.th32OwnerProcessID!=GetCurrentProcessId()||entry.th32ThreadID==GetCurrentThreadId())continue;
  if(count==threads.size()){safe=false;break;}
  const auto handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
  if(!handle){safe=false;break;}threads[count++]=handle;
 }while(Thread32Next(snapshot,&entry));
 CloseHandle(snapshot);
 if(safe)for(size_t i=0;i<count;++i){if(SuspendThread(threads[i])==DWORD(-1)){safe=false;break;}++suspended;
  CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
  if(!GetThreadContext(threads[i],&context)||(context.Rip>=address&&context.Rip<address+replacement.size())){safe=false;break;}}
 DWORD previous=0;
 if(safe&&same(address,expected)&&VirtualProtect(reinterpret_cast<void*>(address),replacement.size(),PAGE_EXECUTE_READWRITE,&previous)){
  std::memcpy(reinterpret_cast<void*>(address),replacement.data(),replacement.size());FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),replacement.size());
  DWORD ignored=0;result=VirtualProtect(reinterpret_cast<void*>(address),replacement.size(),previous,&ignored)!=FALSE;
  if(!result){std::memcpy(reinterpret_cast<void*>(address),expected.data(),expected.size());FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),expected.size());VirtualProtect(reinterpret_cast<void*>(address),expected.size(),previous,&ignored);}
 }
 for(size_t i=0;i<suspended;++i)ResumeThread(threads[i]);
 for(size_t i=0;i<count;++i)CloseHandle(threads[i]);
 return result;
}
bool replace(U address,const std::array<unsigned char,14>&before,const std::array<unsigned char,14>&after)noexcept{
 for(unsigned attempt=0;attempt<8;++attempt){if(write_guarded(address,before,after))return true;if(!same(address,before))return false;Sleep(2);}return false;
}
void* near_page(U site){SYSTEM_INFO system{};GetSystemInfo(&system);const U granularity=system.dwAllocationGranularity,range=0x7fff0000;
 const auto minimum=std::max<U>(reinterpret_cast<U>(system.lpMinimumApplicationAddress),site>range?site-range:0),maximum=std::min<U>(reinterpret_cast<U>(system.lpMaximumApplicationAddress),site<=UINTPTR_MAX-range?site+range:site);
 for(U cursor=minimum;cursor<maximum;){MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(reinterpret_cast<void*>(cursor),&info,sizeof info))break;
  const U region=reinterpret_cast<U>(info.BaseAddress),end=region+info.RegionSize,candidate=(region+granularity-1)&~(granularity-1);
  if(info.State==MEM_FREE&&candidate>=minimum&&candidate+4096<=end&&candidate+4096<=maximum){if(auto page=VirtualAlloc(reinterpret_cast<void*>(candidate),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return page;}
  if(end<=cursor)break;
  cursor=end;
 }return nullptr;
}
}
bool NativeToolCallPatch::install(U site,const std::array<unsigned char,14>&before,U wrapper)noexcept {
 if(installed_)return site==site_&&before==before_&&wrapper==wrapper_&&same(site_,after_);
 if(!site||!wrapper||!same(site,before))return false;
 // Reuse a relay already published for this exact block; never leak per-toggle.
 if(site==site_&&before==before_&&wrapper==wrapper_){if(!replace(site_,before_,after_))return false;installed_=true;return true;}
 HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(wrapper),&pinned))return false;
 auto page=static_cast<unsigned char*>(near_page(site));if(!page)return false;
 auto cursor=page;
 const auto emit=[&](const void*data,size_t size){std::memcpy(cursor,data,size);cursor+=size;};
 emit(before.data(),before.size());
 const unsigned char save[]={0x9c,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,0x48,0x81,0xec,0x80,0,0,0};emit(save,sizeof save);
 for(unsigned i=0;i<6;++i){const unsigned char op[]={0xf3,0x0f,0x7f,static_cast<unsigned char>(0x44+i*8),0x24,static_cast<unsigned char>(0x20+i*16)};emit(op,sizeof op);}
 const unsigned char argument[]={0x49,0x8b,0xcf,0x48,0xb8};emit(argument,sizeof argument);emit(&wrapper,sizeof wrapper);
 const unsigned char call[]={0xff,0xd0};emit(call,sizeof call);
 for(unsigned i=0;i<6;++i){const unsigned char op[]={0xf3,0x0f,0x6f,static_cast<unsigned char>(0x44+i*8),0x24,static_cast<unsigned char>(0x20+i*16)};emit(op,sizeof op);}
 const unsigned char restore[]={0x48,0x81,0xc4,0x80,0,0,0,0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58,0x9d};emit(restore,sizeof restore);
 const unsigned char jump[]={0xff,0x25,0,0,0,0};emit(jump,sizeof jump);const U resume=site+before.size();emit(&resume,sizeof resume);
 DWORD old=0;if(!VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old)){VirtualFree(page,0,MEM_RELEASE);return false;}FlushInstructionCache(GetCurrentProcess(),page,cursor-page);
 std::int32_t relay=0;if(!relative(site,reinterpret_cast<U>(page),relay)){VirtualFree(page,0,MEM_RELEASE);return false;}
 std::array<unsigned char,14> after{};after.fill(0x90);after[0]=0xe9;std::memcpy(after.data()+1,&relay,4);
 if(!replace(site,before,after)){VirtualFree(page,0,MEM_RELEASE);return false;}
 site_=site;wrapper_=wrapper;before_=before;after_=after;installed_=true;return true;
}
bool NativeToolCallPatch::restore()noexcept {
 if(!installed_)return true;
 if(!replace(site_,after_,before_))return false;
 installed_=false;return true;
}
}
