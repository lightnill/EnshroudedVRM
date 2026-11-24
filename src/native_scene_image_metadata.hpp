#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

namespace evrm {

// Observed object identity only. This is not descriptor-at-write provenance,
// backing, source initialization, GPU completion, ownership or a lifetime lease.
struct NativeSceneImageToken {
    VkImage image{};
    VkImageView view{};
    std::uint64_t image_generation=0,view_generation=0;
    std::uint64_t registry_serial=0;
    explicit operator bool()const noexcept{return image&&view&&image_generation&&view_generation&&registry_serial;}
};
struct NativeSceneImageFacts {
    NativeSceneImageToken identity;
    VkImageType image_type=VK_IMAGE_TYPE_MAX_ENUM;
    VkImageViewType view_type=VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    VkFormat image_format=VK_FORMAT_UNDEFINED,format=VK_FORMAT_UNDEFINED;
    VkExtent3D image_extent{},extent{};
    std::uint32_t image_mips=0,image_layers=0;
    VkImageCreateFlags image_flags=0;
    VkImageUsageFlags image_usage=0,view_usage=0;
    VkSampleCountFlagBits samples=VkSampleCountFlagBits(0);
    VkImageTiling tiling=VK_IMAGE_TILING_MAX_ENUM;
    VkSharingMode sharing_mode=VK_SHARING_MODE_MAX_ENUM;
    std::uint32_t queue_family_count=0,format_count=0;
    std::array<std::uint32_t,8> queue_families{};
    std::array<VkFormat,16> formats{};
    VkComponentMapping components{};
    VkImageSubresourceRange range{};
    explicit operator bool()const noexcept{return bool(identity);}
};

// Caller serializes successful native create/destroy observations. No driver
// calls or payload reads. Independent bounds/failure state keep this research
// census from changing legacy sampled-image or avatar-rendering eligibility.
class NativeSceneImageMetadata {
public:
    static constexpr std::size_t image_capacity=65536,view_capacity=131072;
    explicit NativeSceneImageMetadata(std::size_t images=image_capacity,std::size_t views=view_capacity)noexcept
        : image_limit_(images),view_limit_(views),available_(images&&views&&images<=image_capacity&&views<=view_capacity&&registry_serial_!=0){}
    NativeSceneImageMetadata(const NativeSceneImageMetadata&)=delete;
    NativeSceneImageMetadata& operator=(const NativeSceneImageMetadata&)=delete;
    NativeSceneImageMetadata(NativeSceneImageMetadata&&)=delete;
    NativeSceneImageMetadata& operator=(NativeSceneImageMetadata&&)=delete;
    bool available()const noexcept{return available_;}
    void image_created(VkImage handle,const VkImageCreateInfo& info)noexcept{
        try{
            images_.erase(handle);if(!available_||!handle)return;
            constexpr VkImageCreateFlags supported=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT|VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            if(info.sType!=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO||(info.flags&~supported)||
               (info.imageType!=VK_IMAGE_TYPE_2D&&info.imageType!=VK_IMAGE_TYPE_3D)||info.format==VK_FORMAT_UNDEFINED||
               !info.extent.width||!info.extent.height||!info.extent.depth||info.extent.width>32768||info.extent.height>32768||info.extent.depth>32768||
               !info.mipLevels||info.mipLevels>16||!info.arrayLayers||info.arrayLayers>2048||!info.usage||info.samples!=VK_SAMPLE_COUNT_1_BIT||
               (info.tiling!=VK_IMAGE_TILING_OPTIMAL&&info.tiling!=VK_IMAGE_TILING_LINEAR)||
               (info.imageType==VK_IMAGE_TYPE_2D&&info.extent.depth!=1)||(info.imageType==VK_IMAGE_TYPE_3D&&info.arrayLayers!=1))return;
            std::uint32_t maximum_mips=1;
            for(auto extent=(std::max)({info.extent.width,info.extent.height,info.extent.depth});extent>1;extent>>=1)++maximum_mips;
            if(info.mipLevels>maximum_mips)return;
            if((info.flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)&&
               (info.imageType!=VK_IMAGE_TYPE_2D||info.extent.width!=info.extent.height||info.arrayLayers<6))return;
            NativeSceneImageFacts facts;
            facts.identity.image=handle;facts.identity.registry_serial=registry_serial_;
            facts.image_type=info.imageType;facts.image_format=info.format;facts.image_extent=info.extent;
            facts.image_mips=info.mipLevels;facts.image_layers=info.arrayLayers;facts.image_flags=info.flags;
            facts.image_usage=info.usage;facts.samples=info.samples;facts.tiling=info.tiling;facts.sharing_mode=info.sharingMode;
            if(info.sharingMode==VK_SHARING_MODE_CONCURRENT){
                if(info.queueFamilyIndexCount<2||info.queueFamilyIndexCount>facts.queue_families.size()||!info.pQueueFamilyIndices)return;
                for(std::uint32_t i=0;i<info.queueFamilyIndexCount;++i){const auto family=info.pQueueFamilyIndices[i];if(family>=65536)return;
                    if(std::find(facts.queue_families.begin(),facts.queue_families.begin()+i,family)!=facts.queue_families.begin()+i)return;
                    facts.queue_families[i]=family;}
                facts.queue_family_count=info.queueFamilyIndexCount;
            }else if(info.sharingMode!=VK_SHARING_MODE_EXCLUSIVE)return;
            if(info.pNext){const auto& list=*static_cast<const VkImageFormatListCreateInfo*>(info.pNext);
                if(list.sType!=VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO||list.pNext||list.viewFormatCount>facts.formats.size()||
                   (list.viewFormatCount&&!list.pViewFormats))return;
                for(std::uint32_t i=0;i<list.viewFormatCount;++i){if(list.pViewFormats[i]==VK_FORMAT_UNDEFINED)return;facts.formats[i]=list.pViewFormats[i];}
                facts.format_count=list.viewFormatCount;
                if(facts.format_count&&std::find(facts.formats.begin(),facts.formats.begin()+facts.format_count,info.format)==facts.formats.begin()+facts.format_count)return;
            }
            if(images_.size()>=image_limit_){disable();return;}
            facts.identity.image_generation=generation();if(!available_)return;
            images_.emplace(handle,facts);
        }catch(...){disable();}
    }
    void image_destroyed(VkImage handle)noexcept{images_.erase(handle);}
    void view_created(VkImageView handle,const VkImageViewCreateInfo& info)noexcept{
        try{
            views_.erase(handle);if(!available_||!handle)return;
            if(info.sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO||info.flags||info.format==VK_FORMAT_UNDEFINED)return;
            const auto image=images_.find(info.image);if(image==images_.end())return;
            auto facts=image->second;
            if(!(facts.image_flags&VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)&&info.format!=facts.image_format)return;
            if(facts.format_count&&std::find(facts.formats.begin(),facts.formats.begin()+facts.format_count,info.format)==facts.formats.begin()+facts.format_count)return;
            auto range=info.subresourceRange;
            if(range.baseMipLevel>=facts.image_mips||range.baseArrayLayer>=facts.image_layers)return;
            if(range.levelCount==VK_REMAINING_MIP_LEVELS)range.levelCount=facts.image_mips-range.baseMipLevel;
            if(range.layerCount==VK_REMAINING_ARRAY_LAYERS)range.layerCount=facts.image_layers-range.baseArrayLayer;
            if(!range.levelCount||range.levelCount>facts.image_mips-range.baseMipLevel||!range.layerCount||range.layerCount>facts.image_layers-range.baseArrayLayer)return;
            if(range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT&&range.aspectMask!=VK_IMAGE_ASPECT_DEPTH_BIT&&range.aspectMask!=VK_IMAGE_ASPECT_STENCIL_BIT&&
               range.aspectMask!=(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT))return;
            const bool ordinary=info.viewType==VK_IMAGE_VIEW_TYPE_2D||info.viewType==VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            const bool cube=info.viewType==VK_IMAGE_VIEW_TYPE_CUBE||info.viewType==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            if(ordinary){if(facts.image_type!=VK_IMAGE_TYPE_2D||(info.viewType==VK_IMAGE_VIEW_TYPE_2D&&range.layerCount!=1))return;}
            else if(cube){if(facts.image_type!=VK_IMAGE_TYPE_2D||!(facts.image_flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)||
                (info.viewType==VK_IMAGE_VIEW_TYPE_CUBE?range.layerCount!=6:range.layerCount%6!=0))return;}
            else if(info.viewType!=VK_IMAGE_VIEW_TYPE_3D||facts.image_type!=VK_IMAGE_TYPE_3D||range.baseArrayLayer||range.layerCount!=1)return;
            for(const auto component:{info.components.r,info.components.g,info.components.b,info.components.a})
                if(component<VK_COMPONENT_SWIZZLE_IDENTITY||component>VK_COMPONENT_SWIZZLE_A)return;
            facts.view_usage=facts.image_usage;
            if(info.pNext){const auto& usage=*static_cast<const VkImageViewUsageCreateInfo*>(info.pNext);
                if(usage.sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO||usage.pNext||!usage.usage||(usage.usage&~facts.image_usage))return;
                facts.view_usage=usage.usage;}
            if(views_.size()>=view_limit_){disable();return;}
            facts.identity.view=handle;facts.identity.view_generation=generation();if(!available_)return;
            facts.view_type=info.viewType;facts.format=info.format;facts.components=info.components;facts.range=range;
            facts.extent={(std::max)(1u,facts.image_extent.width>>range.baseMipLevel),(std::max)(1u,facts.image_extent.height>>range.baseMipLevel),
                          (std::max)(1u,facts.image_extent.depth>>range.baseMipLevel)};
            views_.emplace(handle,facts);
        }catch(...){disable();}
    }
    void view_destroyed(VkImageView handle)noexcept{views_.erase(handle);}
    NativeSceneImageFacts image(VkImage handle)const noexcept{
        if(!available_)return {};const auto found=images_.find(handle);
        return found==images_.end()?NativeSceneImageFacts{}:found->second;
    }
    NativeSceneImageFacts view(VkImageView handle)const noexcept{
        if(!available_)return {};
        const auto found=views_.find(handle);if(found==views_.end())return {};
        const auto image=images_.find(found->second.identity.image);
        return image!=images_.end()&&image->second.identity.image_generation==found->second.identity.image_generation?found->second:NativeSceneImageFacts{};
    }
    NativeSceneImageToken token(VkImageView handle)const noexcept{return view(handle).identity;}
    bool current(const NativeSceneImageToken& prior)const noexcept{
        if(!prior||prior.registry_serial!=registry_serial_)return false;const auto now=token(prior.view);
        return now&&now.image==prior.image&&now.image_generation==prior.image_generation&&now.view_generation==prior.view_generation;
    }
private:
    static std::uint64_t new_registry_serial()noexcept{
        static std::atomic<std::uint64_t> next{1};
        auto prior=next.load(std::memory_order_relaxed);
        while(prior&&prior!=(std::numeric_limits<std::uint64_t>::max)())
            if(next.compare_exchange_weak(prior,prior+1,std::memory_order_relaxed))return prior;
        return 0;
    }
    void disable()noexcept{available_=false;images_.clear();views_.clear();}
    std::uint64_t generation()noexcept{
        if(next_generation_==(std::numeric_limits<std::uint64_t>::max)()){disable();return 0;}
        return next_generation_++;
    }
    std::size_t image_limit_,view_limit_;
    const std::uint64_t registry_serial_=new_registry_serial();
    bool available_=false;
    std::uint64_t next_generation_=1;
    std::map<VkImage,NativeSceneImageFacts> images_;
    std::map<VkImageView,NativeSceneImageFacts> views_;
};
} // namespace evrm
