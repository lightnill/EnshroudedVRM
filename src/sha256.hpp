#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace evrm::detail {
using Sha256Digest=std::array<std::uint8_t,32>;
// Private byte fingerprint helper. SHA-256 operates on big-endian message
// words; all input access is byte-wise so module alignment is not required.
inline Sha256Digest sha256_portable(const void* data,std::size_t size) noexcept {
    static constexpr std::uint32_t constants[]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::uint32_t state[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const auto rotate=[](std::uint32_t value,unsigned count){return(value>>count)|(value<<(32-count));};
    const auto block=[&](const std::uint8_t* bytes){
        std::uint32_t words[64];
        for(unsigned i=0;i<16;++i)words[i]=(std::uint32_t(bytes[i*4])<<24)|(std::uint32_t(bytes[i*4+1])<<16)|(std::uint32_t(bytes[i*4+2])<<8)|bytes[i*4+3];
        for(unsigned i=16;i<64;++i){
            const auto a=words[i-15],b=words[i-2];
            words[i]=words[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+words[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
        }
        auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for(unsigned i=0;i<64;++i){
            const auto first=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+constants[i]+words[i];
            const auto second=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+first;d=c;c=b;b=a;a=first+second;
        }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    };
    const auto* bytes=static_cast<const std::uint8_t*>(data);
    std::size_t at=0;for(;size-at>=64;at+=64)block(bytes+at);
    std::uint8_t tail[128]{};const auto remaining=size-at;
    if(remaining)std::memcpy(tail,bytes+at,remaining);
    tail[remaining]=0x80;const unsigned padded=remaining<56?64:128;
    const auto bits=static_cast<std::uint64_t>(size)*8;
    for(unsigned i=0;i<8;++i)tail[padded-1-i]=static_cast<std::uint8_t>(bits>>(i*8));
    block(tail);if(padded==128)block(tail+64);
    Sha256Digest result{};for(unsigned i=0;i<32;++i)result[i]=static_cast<std::uint8_t>(state[i/4]>>(24-(i%4)*8));return result;
}
#if defined(_WIN32)
inline bool try_sha256_windows(const void* data,std::size_t size,Sha256Digest& out)noexcept {
    if(size>UINT32_MAX)return false;
    using Hash=NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE,PUCHAR,ULONG,PUCHAR,ULONG,PUCHAR,ULONG);
    static const Hash hash=[]()noexcept{
        // Keep this one module reference for the lifetime of the cached function.
        // Never search the working directory or PATH, or change global policy.
        const auto module=LoadLibraryExW(L"bcrypt.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        Hash result=nullptr;if(module){const auto raw=GetProcAddress(module,"BCryptHash");
            static_assert(sizeof raw==sizeof result);std::memcpy(&result,&raw,sizeof result);}
        return result;
    }();
    if(!hash)return false;
    Sha256Digest result{};
    const auto status=hash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
        const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),static_cast<ULONG>(size),result.data(),static_cast<ULONG>(result.size()));
    if(status<0)return false;
    out=result;return true;
}
#endif
using Sha256Provider=bool(*)(const void*,std::size_t,Sha256Digest&)noexcept;
inline Sha256Digest sha256_with_provider(const void* data,std::size_t size,Sha256Provider provider)noexcept {
    Sha256Digest result{};
    if(size>=1024&&size<=UINT32_MAX&&provider&&provider(data,size,result))return result;
    return sha256_portable(data,size);
}
inline Sha256Digest sha256(const void* data,std::size_t size)noexcept {
#if defined(_WIN32)
    return sha256_with_provider(data,size,&try_sha256_windows);
#else
    return sha256_portable(data,size);
#endif
}
}
