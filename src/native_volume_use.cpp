#include "native_volume_use.hpp"

namespace evrm { namespace {
using U=std::uint64_t;
bool compute(U stage)noexcept{return (stage&(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT))!=0;}
bool fragment(U stage)noexcept{return (stage&(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT|VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT))!=0;}
bool write(U access)noexcept{return (access&(VK_ACCESS_2_SHADER_WRITE_BIT|VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT))!=0;}
bool read(U access)noexcept{return (access&(VK_ACCESS_2_SHADER_READ_BIT|VK_ACCESS_2_SHADER_SAMPLED_READ_BIT|VK_ACCESS_2_MEMORY_READ_BIT))!=0;}
bool full(const VkImageSubresourceRange& r)noexcept{return r.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&!r.baseMipLevel&&r.levelCount==1&&!r.baseArrayLayer&&r.layerCount==1;}
bool equal(VkExtent3D a,VkExtent3D b)noexcept{return a.width==b.width&&a.height==b.height&&a.depth==b.depth;}
bool bounded(VkExtent3D e)noexcept{return e.width&&e.height&&e.depth&&e.width<=256&&e.height<=256&&e.depth<=128&&U(e.width)*e.height*e.depth<=U(256)*256*128;}
}
void NativeVolumeUse::reset()noexcept{const auto last=last_id_;*this={};last_id_=last;}
bool NativeVolumeUse::begin(U id,VkCommandBuffer command)noexcept{
    reset();if(!id||id<=last_id_){failed_=true;return false;}
    last_id_=id;if(!command){failed_=true;return false;}command_=command;begun_=true;return true;
}
bool NativeVolumeUse::coverage(std::uint32_t x,std::uint32_t y,std::uint32_t z)const noexcept{
    const auto e=images_[0].target.extent;return x==(e.width+3)/4&&y==(e.height+3)/4&&z==(e.depth+3)/4;
}
bool NativeVolumeUse::populate(const std::array<NativeVolumeTarget,2>& target,std::uint32_t x,std::uint32_t y,std::uint32_t z)noexcept{
    if(!begun_||failed_||selected_||ended_||!target[0].image||!target[1].image||target[0].image==target[1].image||
       !target[0].image_generation||!target[1].image_generation||!bounded(target[0].extent)||!equal(target[0].extent,target[1].extent)){
        failed_=true;return false;}
    images_[0].target=target[0];images_[1].target=target[1];selected_=true;
    if(!coverage(x,y,z)){failed_=true;return false;}return true;
}
bool NativeVolumeUse::in_scatter(std::uint32_t x,std::uint32_t y,std::uint32_t z)noexcept{
    if(!begun_||!selected_||failed_||ended_||inserted_||scattered_||!coverage(x,y,z)||
       !images_[0].read_only||!images_[1].read_only||!images_[0].compute_ready||!images_[1].compute_ready){failed_=true;return false;}
    scattered_=true;return true;
}
bool NativeVolumeUse::observing()noexcept{
    if(!selected_||failed_)return false;
    if(ended_){failed_=true;return false;}return true;
}
void NativeVolumeUse::memory(U src,U dst,U sa,U da)noexcept{
    if(compute(src)&&write(sa)&&fragment(dst)&&read(da))for(auto& i:images_)if(i.read_only)i.fragment_ready=true;
}
void NativeVolumeUse::image(VkImage handle,VkImageLayout old_layout,VkImageLayout new_layout,std::uint32_t src_queue,std::uint32_t dst_queue,
    const VkImageSubresourceRange& range,U src,U dst,U sa,U da)noexcept{
    for(auto& i:images_)if(i.target.image==handle){
        if(inserted_||src_queue!=VK_QUEUE_FAMILY_IGNORED||dst_queue!=VK_QUEUE_FAMILY_IGNORED||!full(range)||
           old_layout!=(i.read_only?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL)||
           new_layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL){failed_=true;return;}
        const bool dependency=compute(src)&&write(sa)&&read(da);
        if(!i.read_only){
            // The transition itself must publish the full storage write for
            // the ordinary compute witness; its own scope may include FS too.
            if(!dependency||!compute(dst)){failed_=true;return;}
            i.read_only=true;i.compute_ready=true;i.fragment_ready=fragment(dst);
        }else if(dependency){
            i.compute_ready=i.compute_ready||compute(dst);i.fragment_ready=i.fragment_ready||fragment(dst);
        }
    }
}
void NativeVolumeUse::legacy(VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,
    std::uint32_t mc,const VkMemoryBarrier* memories,std::uint32_t bc,const VkBufferMemoryBarrier* buffers,
    std::uint32_t ic,const VkImageMemoryBarrier* images)noexcept{
    (void)bc;(void)buffers;if(!observing())return;
    if(flags||mc>memory_limit||ic>image_limit||(mc&&!memories)||(ic&&!images)){failed_=true;return;}
    // Validate all shells before effects. No pNext chain is traversed, and
    // buffer-only arrays are intentionally never read under exclusive backing.
    for(std::uint32_t n=0;n<mc;++n)if(memories[n].sType!=VK_STRUCTURE_TYPE_MEMORY_BARRIER||memories[n].pNext){failed_=true;return;}
    std::array<bool,2> seen{};
    for(std::uint32_t n=0;n<ic;++n){const auto& b=images[n];if(b.sType!=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER||b.pNext||!b.image){failed_=true;return;}
        for(unsigned j=0;j<2;++j)if(b.image==images_[j].target.image){if(seen[j]){failed_=true;return;}seen[j]=true;}}
    // Global dependencies see only layouts at call entry. Image barriers in a
    // dependency call are not an ordered sequence after its memory barriers.
    for(std::uint32_t n=0;n<mc;++n)memory(src,dst,memories[n].srcAccessMask,memories[n].dstAccessMask);
    for(std::uint32_t n=0;n<ic&&!failed_;++n){const auto& b=images[n];image(b.image,b.oldLayout,b.newLayout,b.srcQueueFamilyIndex,b.dstQueueFamilyIndex,b.subresourceRange,src,dst,b.srcAccessMask,b.dstAccessMask);}
}
void NativeVolumeUse::sync2(const VkDependencyInfo* info)noexcept{
    if(!observing())return;
    if(!info||info->sType!=VK_STRUCTURE_TYPE_DEPENDENCY_INFO||info->pNext||info->dependencyFlags){failed_=true;return;}
    const auto mc=info->memoryBarrierCount,ic=info->imageMemoryBarrierCount;
    if(mc>memory_limit||ic>image_limit||(mc&&!info->pMemoryBarriers)||(ic&&!info->pImageMemoryBarriers)){failed_=true;return;}
    for(std::uint32_t n=0;n<mc;++n)if(info->pMemoryBarriers[n].sType!=VK_STRUCTURE_TYPE_MEMORY_BARRIER_2||info->pMemoryBarriers[n].pNext){failed_=true;return;}
    std::array<bool,2> seen{};
    for(std::uint32_t n=0;n<ic;++n){const auto& b=info->pImageMemoryBarriers[n];if(b.sType!=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2||b.pNext||!b.image){failed_=true;return;}
        for(unsigned j=0;j<2;++j)if(b.image==images_[j].target.image){if(seen[j]){failed_=true;return;}seen[j]=true;}}
    for(std::uint32_t n=0;n<mc;++n){const auto& b=info->pMemoryBarriers[n];memory(b.srcStageMask,b.dstStageMask,b.srcAccessMask,b.dstAccessMask);}
    for(std::uint32_t n=0;n<ic&&!failed_;++n){const auto& b=info->pImageMemoryBarriers[n];image(b.image,b.oldLayout,b.newLayout,b.srcQueueFamilyIndex,b.dstQueueFamilyIndex,b.subresourceRange,b.srcStageMask,b.dstStageMask,b.srcAccessMask,b.dstAccessMask);}
}
void NativeVolumeUse::unobserved()noexcept{if(selected_)failed_=true;}
bool NativeVolumeUse::can_sample()const noexcept{
    return begun_&&selected_&&!failed_&&!ended_&&!inserted_&&scattered_&&images_[0].read_only&&images_[1].read_only&&images_[0].compute_ready&&images_[1].compute_ready&&images_[0].fragment_ready&&images_[1].fragment_ready;
}
bool NativeVolumeUse::insert(bool succeeded)noexcept{
    if(!succeeded||!can_sample()){failed_=true;return false;}inserted_=true;return true;
}
void NativeVolumeUse::end(VkResult result)noexcept{
    if(!begun_||!selected_||!inserted_||ended_||result!=VK_SUCCESS)failed_=true;
    ended_=true;
}
bool NativeVolumeUse::complete()const noexcept{return begun_&&selected_&&!failed_&&inserted_&&ended_&&scattered_;}
}
