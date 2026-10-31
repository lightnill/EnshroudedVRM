#include "../src/sha256.hpp"
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
unsigned failed_calls=0;
bool failed_provider(const void*,std::size_t,evrm::detail::Sha256Digest& out)noexcept{++failed_calls;out.fill(0xAA);return false;}
void print(const evrm::detail::Sha256Digest& digest){for(auto b:digest)std::printf("%02x",b);}
}
int main(){
    // Exercise concurrent first use of the system provider as well as normal reuse.
    std::atomic<bool> okay{true};std::vector<std::thread> workers;
    for(unsigned t=0;t<4;++t)workers.emplace_back([&,t]{std::vector<std::uint8_t> bytes(65536,std::uint8_t(t));
        const auto expected=evrm::detail::sha256_portable(bytes.data(),bytes.size());
        for(unsigned i=0;i<40;++i)if(evrm::detail::sha256(bytes.data(),bytes.size())!=expected)okay=false;});
    for(auto& thread:workers)thread.join();
    if(!okay)return 1;
    for(std::size_t size:{0u,1u,3u,55u,56u,63u,64u,65u,1023u,1024u,4096u,65537u,1048589u,8388615u}){
        std::vector<std::uint8_t> bytes(size+7);for(std::size_t i=0;i<size;++i)bytes[i+7]=std::uint8_t(i*17+(i>>7)*31);
        const auto* input=bytes.data()+7;const auto portable=evrm::detail::sha256_portable(input,size);
        const auto normal=evrm::detail::sha256(input,size);
        const auto fallback=evrm::detail::sha256_with_provider(input,size,failed_provider);
        if(normal!=portable||fallback!=portable||evrm::detail::sha256_with_provider(input,size,nullptr)!=portable)return 2;
        evrm::detail::Sha256Digest system{};
#if defined(_WIN32)
        if(!evrm::detail::try_sha256_windows(input,size,system)||system!=portable)return 3;
#endif
        std::printf("%zu ",size);print(normal);std::puts("");
    }
    if(failed_calls!=5)return 4; // <1 KiB uses the portable path without provider overhead.
    if(evrm::detail::sha256(nullptr,0)!=evrm::detail::sha256_portable(nullptr,0))return 5;
#if defined(_WIN32)
    evrm::detail::Sha256Digest untouched{};untouched.fill(7);const auto before=untouched;
    if(evrm::detail::try_sha256_windows(nullptr,std::size_t(UINT32_MAX)+1,untouched)||untouched!=before)return 6;
#endif
    return 0;
}
