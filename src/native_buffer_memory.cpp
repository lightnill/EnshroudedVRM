#include "native_buffer_memory.hpp"
#include <array>
#include <cmath>
#include <limits>

namespace evrm { namespace {
constexpr std::size_t record_cap=4096;
constexpr VkDeviceSize buffer_size_cap=VkDeviceSize(1)<<40;
// Inputs are the still-live Vulkan creation structs from a successful call.
// Inspect only known node bodies; buffer pNext is rejected without traversal.
bool ordinary_allocation(const VkMemoryAllocateInfo& info) noexcept {
    if(info.sType!=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO||!info.allocationSize)return false;
    std::array<const VkBaseInStructure*,4> visited{};
    std::size_t count=0;unsigned kinds=0;
    auto node=static_cast<const VkBaseInStructure*>(info.pNext);
    while(node){
        if(count==visited.size())return false;
        for(std::size_t i=0;i<count;++i)if(visited[i]==node)return false;
        visited[count++]=node;
        unsigned kind=0;
        switch(node->sType){
        case VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO:{
            kind=1;if(kinds&kind)return false;
            const auto& flags=*reinterpret_cast<const VkMemoryAllocateFlagsInfo*>(node);
            constexpr VkMemoryAllocateFlags allowed=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT|VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
            if((flags.flags&~allowed)||flags.deviceMask>1)return false;
            if((flags.flags&VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)&&!(flags.flags&VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT))return false;
            break;
        }
        case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO:{
            kind=2;if(kinds&kind)return false;
            const auto& dedicated=*reinterpret_cast<const VkMemoryDedicatedAllocateInfo*>(node);
            if(bool(dedicated.buffer)==bool(dedicated.image))return false;
            break;
        }
        case VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT:{
            kind=4;if(kinds&kind)return false;
            const auto& priority=*reinterpret_cast<const VkMemoryPriorityAllocateInfoEXT*>(node);
            if(!std::isfinite(priority.priority)||priority.priority<0||priority.priority>1)return false;
            break;
        }
        default:return false;
        }
        kinds|=kind;node=node->pNext;
    }
    return true;
}
}
void NativeBufferMemory::memory_created(VkDeviceMemory handle,const VkMemoryAllocateInfo& info) noexcept {
    try{
        std::lock_guard<std::mutex> lock(mutex_);if(disabled_.load()||!handle)return;
        // Even an unsupported replacement must retire the old association.
        memories_.erase(handle);
        if(!ordinary_allocation(info))return;
        if(memories_.size()>=record_cap||generation_==std::numeric_limits<std::uint64_t>::max()){disabled_=true;return;}
        memories_.emplace(handle,Memory{info.allocationSize,++generation_});
    }catch(...){disabled_=true;}
}
void NativeBufferMemory::memory_destroyed(VkDeviceMemory handle) noexcept {
    try{std::lock_guard<std::mutex> lock(mutex_);memories_.erase(handle);}catch(...){disabled_=true;}
}
void NativeBufferMemory::buffer_created(VkBuffer handle,const VkBufferCreateInfo& info) noexcept {
    try{
        std::lock_guard<std::mutex> lock(mutex_);if(disabled_.load()||!handle)return;
        buffers_.erase(handle);
        if(info.sType!=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO||info.pNext||info.flags||!info.size||info.size>buffer_size_cap)return;
        if(buffers_.size()>=record_cap||generation_==std::numeric_limits<std::uint64_t>::max()){disabled_=true;return;}
        Buffer buffer;buffer.size=info.size;buffer.generation=++generation_;buffers_.emplace(handle,buffer);
    }catch(...){disabled_=true;}
}
void NativeBufferMemory::buffer_destroyed(VkBuffer handle) noexcept {
    try{std::lock_guard<std::mutex> lock(mutex_);buffers_.erase(handle);}catch(...){disabled_=true;}
}
void NativeBufferMemory::bound(VkBuffer handle,VkDeviceMemory memory,VkDeviceSize offset) noexcept {
    try{
        std::lock_guard<std::mutex> lock(mutex_);if(disabled_.load())return;
        const auto found=buffers_.find(handle);if(found==buffers_.end())return;
        auto& buffer=found->second;buffer.valid=false;
        if(buffer.attempted)return;
        buffer.attempted=true;
        const auto allocation=memories_.find(memory);if(allocation==memories_.end())return;
        if(offset>allocation->second.size||buffer.size>allocation->second.size-offset)return;
        buffer.memory=memory;buffer.memory_generation=allocation->second.generation;buffer.offset=offset;buffer.valid=true;
    }catch(...){disabled_=true;}
}
bool NativeBufferMemory::current_range(VkBuffer handle,VkDeviceSize relative,VkDeviceSize count,Range& out) const noexcept {
    if(!handle||!count)return false;
    const auto found=buffers_.find(handle);if(found==buffers_.end())return false;
    const auto& buffer=found->second;if(!buffer.valid||relative>buffer.size||count>buffer.size-relative)return false;
    const auto allocation=memories_.find(buffer.memory);
    if(allocation==memories_.end()||allocation->second.generation!=buffer.memory_generation)return false;
    // Recheck the complete binding before any addition. Logical range
    // bounds then guarantee begin/end fit in the allocation as well.
    if(buffer.offset>allocation->second.size||buffer.size>allocation->second.size-buffer.offset)return false;
    out={buffer.memory,buffer.memory_generation,buffer.offset+relative,buffer.offset+relative+count};return true;
}
bool NativeBufferMemory::valid(VkBuffer handle,VkDeviceSize offset,VkDeviceSize bytes) const noexcept {
    try{
        std::lock_guard<std::mutex> lock(mutex_);if(disabled_.load())return false;
        Range range;return current_range(handle,offset,bytes,range)&&!disabled_.load();
    }catch(...){disabled_=true;return false;}
}
bool NativeBufferMemory::disjoint(VkBuffer a,VkDeviceSize offset_a,VkDeviceSize bytes_a,
                                 VkBuffer b,VkDeviceSize offset_b,VkDeviceSize bytes_b) const noexcept {
    try{
        std::lock_guard<std::mutex> lock(mutex_);if(disabled_.load())return false;
        Range left,right;if(!current_range(a,offset_a,bytes_a,left)||!current_range(b,offset_b,bytes_b,right))return false;
        return !disabled_.load()&&((left.memory!=right.memory)||(left.generation==right.generation&&(left.end<=right.begin||right.end<=left.begin)));
    }catch(...){disabled_=true;return false;}
}
}
