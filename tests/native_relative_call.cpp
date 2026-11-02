#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/native_relative_call.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
using U=std::uintptr_t;using Function=U(*)(U,U,U);
unsigned native_calls=0,observed_calls=0;U caller=0;
U original(U a,U b,U c){++native_calls;return a+3*b+7*c;}
U observed(U a,U b,U c){++observed_calls;caller=reinterpret_cast<U>(__builtin_extract_return_addr(__builtin_return_address(0)));return original(a,b,c);}
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try{
    auto* page=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));require(page,"owned code allocation failed");
    const unsigned char start[]={0x48,0x83,0xec,0x28};std::memcpy(page,start,sizeof start);
    std::array<unsigned char,5> before{0xe8,0,0,0,0};std::int32_t relative=128-9;std::memcpy(before.data()+1,&relative,4);std::memcpy(page+4,before.data(),5);
    const unsigned char end[]={0x48,0x83,0xc4,0x28,0xc3};std::memcpy(page+9,end,sizeof end);
    const unsigned char jump[]={0xff,0x25,0,0,0,0};std::memcpy(page+128,jump,sizeof jump);const U target=reinterpret_cast<U>(&original);std::memcpy(page+134,&target,sizeof target);
    DWORD old=0;require(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old),"owned code protection failed");FlushInstructionCache(GetCurrentProcess(),page,4096);
    const auto function=reinterpret_cast<Function>(page);require(function(1,2,3)==28&&native_calls==1&&!observed_calls,"original fixture failed");
    evrm::NativeRelativeCall patch;
    auto bad=before;bad[0]=0xe9;require(!patch.install(reinterpret_cast<U>(page+4),bad,reinterpret_cast<U>(page+128),reinterpret_cast<U>(&observed)),"non-CALL block accepted");
    require(patch.install(reinterpret_cast<U>(page+4),before,reinterpret_cast<U>(page+128),reinterpret_cast<U>(&observed)),"complete original CALL observer failed admission");
    require(function(4,5,6)==61&&native_calls==2&&observed_calls==1,"observer changed arguments, return or original call count");
    require(caller==reinterpret_cast<U>(page+9),"relay lost the original CALL return address");
    require(patch.restore()&&function(1,2,3)==28&&native_calls==3&&observed_calls==1,"restore changed original forwarding");
    require(patch.install(reinterpret_cast<U>(page+4),before,reinterpret_cast<U>(page+128),reinterpret_cast<U>(&observed))&&function(1,2,3)==28&&observed_calls==2,"same relay could not be reused");
    require(patch.restore(),"final restore failed");VirtualFree(page,0,MEM_RELEASE);
    std::cout<<"PASS complete owned CALL patch: three arguments, original once, return value/address, mismatch, restore/reuse\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
