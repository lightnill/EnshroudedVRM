#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "sha256.hpp"
#include <vector>

namespace evrm {
namespace detail {
// A trusted, build-specific factual location, rather than a search for a
// brightness value in arbitrary shaders. The override can only write float 0.
struct SpirvFloatZeroProfile {
    std::size_t bytes=0,instruction_word=0;
    std::uint32_t float_type=0,constant_id=0,literal=0;
    Sha256Digest digest{};
};
inline constexpr SpirvFloatZeroProfile native_player_light_profile{
    17532,1499,167,210,0x3c75c28fu,
    {0x03,0x66,0xb1,0xa5,0x84,0x2e,0x96,0x05,0x07,0x58,0x5a,0x54,0xf5,0xd5,0x84,0xbc,
     0x74,0x91,0x3a,0x35,0xab,0xf0,0x98,0x60,0xe5,0xbc,0xf3,0xb5,0x8d,0x34,0x92,0xc3}};
}

// Owns an effective create request for exactly this call. Caller memory and
// extension chains are never edited. Unknown inputs/allocation failure retain
// the original request. No game shader payload is distributed with this code.
class NativePlayerLightShader {
public:
    explicit NativePlayerLightShader(const VkShaderModuleCreateInfo* original,bool enabled=true,
        const detail::SpirvFloatZeroProfile& profile=detail::native_player_light_profile)noexcept
        :original_(original){
        if(!enabled||!original||original->sType!=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO||
           original->pNext||original->flags||!original->pCode||original->codeSize!=profile.bytes||
           profile.bytes<20||profile.bytes>1024*1024||profile.bytes%4||!profile.literal)return;
        const auto* words=original->pCode;const auto count=profile.bytes/4;
        if(words[0]!=0x07230203u||words[4]!=0||words[3]<=profile.float_type||words[3]<=profile.constant_id||
           !profile.float_type||!profile.constant_id||profile.instruction_word<5||
           profile.instruction_word>=count||count-profile.instruction_word<4||
           detail::sha256(words,profile.bytes)!=profile.digest)return;
        bool float32=false,constant=false;
        for(std::size_t at=5;at<count;){
            const auto length=words[at]>>16,operation=words[at]&65535u;
            if(!length||length>count-at)return;
            if(operation==22u&&length==3u&&words[at+1]==profile.float_type)
                float32=words[at+2]==32u;
            if(at==profile.instruction_word){
                if(!float32||operation!=43u||length!=4u||words[at+1]!=profile.float_type||
                   words[at+2]!=profile.constant_id||words[at+3]!=profile.literal)return;
                constant=true;
            }
            at+=length;
        }
        if(!constant)return;
        try{
            owned_.assign(words,words+count);
            owned_[profile.instruction_word+3]=0;
            effective_=*original;effective_.pCode=owned_.data();
        }catch(...){owned_.clear();}
    }
    NativePlayerLightShader(const NativePlayerLightShader&)=delete;
    NativePlayerLightShader& operator=(const NativePlayerLightShader&)=delete;
    NativePlayerLightShader(NativePlayerLightShader&&)=delete;
    NativePlayerLightShader& operator=(NativePlayerLightShader&&)=delete;
    bool changed()const noexcept{return !owned_.empty();}
    const VkShaderModuleCreateInfo* info()const noexcept{return changed()?&effective_:original_;}
private:
    const VkShaderModuleCreateInfo* original_;
    VkShaderModuleCreateInfo effective_{};
    std::vector<std::uint32_t> owned_;
};
}
