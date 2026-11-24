#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

namespace evrm {
// Observed object identity only; neither a sampler clone nor a lifetime lease.
struct NativeSamplerToken {
    VkSampler sampler{};
    std::uint64_t generation=0,registry_serial=0;
    explicit operator bool()const noexcept{return sampler&&generation&&registry_serial;}
};
inline bool same_native_sampler(const NativeSamplerToken& a,const NativeSamplerToken& b)noexcept{
    return a.sampler==b.sampler&&a.generation==b.generation&&a.registry_serial==b.registry_serial;
}
// Caller serializes successful native lifecycle observations. The exact native
// object supplies all filtering/addressing state; no create-info pointer survives.
class NativeSamplerMetadata {
public:
    static constexpr std::size_t capacity=16384;
    explicit NativeSamplerMetadata(std::size_t limit=capacity)noexcept
        : limit_(limit),available_(limit&&limit<=capacity&&serial_!=0){}
    NativeSamplerMetadata(const NativeSamplerMetadata&)=delete;
    NativeSamplerMetadata& operator=(const NativeSamplerMetadata&)=delete;
    bool available()const noexcept{return available_;}
    void created(VkSampler handle,const VkSamplerCreateInfo& info)noexcept{
        try{samplers_.erase(handle);if(!available_||!handle)return;
            if(info.sType!=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO||info.pNext||info.flags||info.compareEnable||info.unnormalizedCoordinates)return;
            if(samplers_.size()>=limit_||next_==(std::numeric_limits<std::uint64_t>::max)()){disable();return;}
            samplers_.emplace(handle,NativeSamplerToken{handle,next_++,serial_});
        }catch(...){disable();}
    }
    void destroyed(VkSampler handle)noexcept{samplers_.erase(handle);}
    NativeSamplerToken token(VkSampler handle)const noexcept{
        if(!available_)return {};const auto found=samplers_.find(handle);return found==samplers_.end()?NativeSamplerToken{}:found->second;
    }
    bool current(const NativeSamplerToken& prior)const noexcept{return prior&&same_native_sampler(prior,token(prior.sampler));}
private:
    static std::uint64_t serial()noexcept{
        static std::atomic<std::uint64_t> next{1};auto value=next.load(std::memory_order_relaxed);
        while(value&&value!=(std::numeric_limits<std::uint64_t>::max)())
            if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value;
        return 0;
    }
    void disable()noexcept{available_=false;samplers_.clear();}
    std::size_t limit_;
    const std::uint64_t serial_=serial();
    bool available_=false;
    std::uint64_t next_=1;
    std::map<VkSampler,NativeSamplerToken> samplers_;
};
struct NativeSamplerDescriptor {
    NativeSamplerToken identity;
    VkDescriptorSet set{};
    std::uint32_t binding=0;
    std::uint64_t set_generation=0,set_version=0;
    VkDescriptorBindingFlags declaration_flags=0;
    bool immutable=false;
    explicit operator bool()const noexcept{return identity&&set&&set_generation&&set_version;}
};
} // namespace evrm
