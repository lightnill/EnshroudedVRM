#pragma once
#include "native_image_memory.hpp"
#include <array>
#include <vector>
namespace evrm {
// Necessary same-recording layout/ownership observations only. Caller supplies
// exact producer identity, current source metadata/backing and a valid native
// primary/queue/lifetime scope. Descriptors alone never establish this state.
class NativeMToonProbeUse {
public:
    struct Diagnostic {
        bool failed=false,produced=false,registry_current=false,found=false,whole=false,owned=false,current_backing=false,discard_after_producer=false;
        VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;std::uint32_t events=0;std::uint64_t barriers=0;
    };
    void producer()noexcept{if(!produced_){produced_=true;producer_serial_=serial_;}}
    void legacy(const NativeImageMemory& memory,std::uint32_t buffer_count,const VkBufferMemoryBarrier* buffers,
                std::uint32_t image_count,const VkImageMemoryBarrier* images)noexcept{
        if(failed_)return;
        if(buffer_count>512||image_count>512||(buffer_count&&!buffers)||(image_count&&!images)){failed_=true;return;}
        for(unsigned i=0;i<buffer_count;++i){const auto& b=buffers[i];
            if(b.sType!=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER||b.pNext||b.srcQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED||b.dstQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED){failed_=true;return;}}
        for(unsigned i=0;i<image_count;++i){const auto& b=images[i];
            if(b.sType!=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER||b.pNext){failed_=true;return;}
            image(memory,b.image,b.oldLayout,b.newLayout,b.srcQueueFamilyIndex,b.dstQueueFamilyIndex,b.subresourceRange);
        }
    }
    void sync2(const NativeImageMemory& memory,const VkDependencyInfo* info)noexcept{
        if(failed_)return;
        if(!info||info->sType!=VK_STRUCTURE_TYPE_DEPENDENCY_INFO||info->pNext||info->bufferMemoryBarrierCount>512||info->imageMemoryBarrierCount>512||
           (info->bufferMemoryBarrierCount&&!info->pBufferMemoryBarriers)||(info->imageMemoryBarrierCount&&!info->pImageMemoryBarriers)){failed_=true;return;}
        for(unsigned i=0;i<info->bufferMemoryBarrierCount;++i){const auto& b=info->pBufferMemoryBarriers[i];
            if(b.sType!=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2||b.pNext||b.srcQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED||b.dstQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED){failed_=true;return;}}
        for(unsigned i=0;i<info->imageMemoryBarrierCount;++i){const auto& b=info->pImageMemoryBarriers[i];
            if(b.sType!=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2||b.pNext){failed_=true;return;}
            image(memory,b.image,b.oldLayout,b.newLayout,b.srcQueueFamilyIndex,b.dstQueueFamilyIndex,b.subresourceRange);
        }
    }
    bool general(const std::array<VkImage,3>& sources,const NativeImageMemory& memory)const noexcept{
        if(failed_||!produced_||registry_!=&memory)return false;
        for(auto source:sources){bool found=false;
            for(const auto& e:events_)if(e.backing.image==source){found=e.whole&&e.owned&&e.layout==VK_IMAGE_LAYOUT_GENERAL&&
                e.discard_serial<=producer_serial_&&memory.current(e.backing);break;}
            if(!found)return false;
        }return true;
    }
    // Read-only explanation of existing observations, never a layout proof.
    Diagnostic diagnostic(VkImage source,const NativeImageMemory& memory)const noexcept{
        Diagnostic d;d.failed=failed_;d.produced=produced_;d.registry_current=registry_==&memory;
        d.events=static_cast<std::uint32_t>(events_.size());d.barriers=serial_;
        for(const auto& e:events_)if(e.backing.image==source){d.found=true;d.whole=e.whole;d.owned=e.owned;d.layout=e.layout;
            d.discard_after_producer=e.discard_serial>producer_serial_;d.current_backing=d.registry_current&&memory.current(e.backing);break;}
        return d;
    }
    void unobserved()noexcept{failed_=true;}
private:
    struct Event {NativeImageBacking backing;VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;std::uint64_t discard_serial=0;bool whole=false,owned=false;};
    void image(const NativeImageMemory& memory,VkImage handle,VkImageLayout old,VkImageLayout next,std::uint32_t src,std::uint32_t dst,const VkImageSubresourceRange& range)noexcept{
        try{
            if(registry_&&registry_!=&memory){failed_=true;return;}registry_=&memory;
            if(++serial_>4096){failed_=true;return;}
            NativeImageBacking backing;if(!memory.exclusive(handle,backing))return;
            Event* event=nullptr;for(auto& e:events_)if(e.backing.image==handle){event=&e;break;}
            if(!event){if(events_.size()>=64){failed_=true;return;}events_.push_back({});event=&events_.back();}
            event->backing=backing;event->layout=next;event->owned=src==VK_QUEUE_FAMILY_IGNORED&&dst==VK_QUEUE_FAMILY_IGNORED;
            event->whole=range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&!range.baseMipLevel&&
                (range.levelCount==1||range.levelCount==VK_REMAINING_MIP_LEVELS)&&!range.baseArrayLayer&&
                (range.layerCount==1||range.layerCount==VK_REMAINING_ARRAY_LAYERS);
            if(old==VK_IMAGE_LAYOUT_UNDEFINED)event->discard_serial=serial_;
        }catch(...){failed_=true;}
    }
    const NativeImageMemory* registry_=nullptr;std::vector<Event> events_;
    std::uint64_t serial_=0,producer_serial_=0;bool produced_=false,failed_=false;
};
} // namespace evrm
