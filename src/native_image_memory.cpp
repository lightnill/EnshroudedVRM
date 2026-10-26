#include "native_image_memory.hpp"
#include <array>
#include <cmath>
#include <limits>

namespace evrm { namespace {
bool power_two(VkDeviceSize n)noexcept{return n&&!(n&(n-1));}
struct AllocationFacts {VkImage image{};VkBuffer buffer{};};
bool ordinary_allocation(const VkMemoryAllocateInfo& info,AllocationFacts& out)noexcept{
    if(info.sType!=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO||!info.allocationSize||info.allocationSize>NativeImageMemory::size_cap)return false;
    std::array<const VkBaseInStructure*,4> visited{};unsigned count=0,kinds=0;
    auto node=static_cast<const VkBaseInStructure*>(info.pNext);
    while(node){
        if(count==visited.size())return false;
        for(unsigned i=0;i<count;++i)if(visited[i]==node)return false;
        visited[count++]=node;unsigned kind=0;
        switch(node->sType){
        case VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO:{
            kind=1;if(kinds&kind)return false;
            const auto& f=*reinterpret_cast<const VkMemoryAllocateFlagsInfo*>(node);
            constexpr VkMemoryAllocateFlags allowed=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT|VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
            if((f.flags&~allowed)||f.deviceMask>1||((f.flags&VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)&&!(f.flags&VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT)))return false;
            break;
        }
        case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO:{
            kind=2;if(kinds&kind)return false;
            const auto& d=*reinterpret_cast<const VkMemoryDedicatedAllocateInfo*>(node);
            if(bool(d.image)==bool(d.buffer))return false;
            out.image=d.image;out.buffer=d.buffer;break;
        }
        case VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT:{
            kind=4;if(kinds&kind)return false;
            const auto p=reinterpret_cast<const VkMemoryPriorityAllocateInfoEXT*>(node)->priority;
            if(!std::isfinite(p)||p<0||p>1)return false;
            break;
        }
        default:return false;
        }
        kinds|=kind;node=node->pNext;
    }
    return true;
}
bool image_chain(const VkImageCreateInfo& info)noexcept{
    if(!info.pNext)return true;
    const auto& list=*static_cast<const VkImageFormatListCreateInfo*>(info.pNext);
    // Requirements do not reinterpret texels. NativeState independently owns
    // and validates the actual format list/view before any sampled use.
    return list.sType==VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO&&!list.pNext&&
        list.viewFormatCount<=16&&(!list.viewFormatCount||list.pViewFormats);
}
bool same(const NativeImageBacking& a,const NativeImageBacking& b)noexcept{
    return a.image==b.image&&a.memory==b.memory&&a.image_generation==b.image_generation&&
        a.memory_generation==b.memory_generation&&a.overlap_revision==b.overlap_revision&&
        a.offset==b.offset&&a.bytes==b.bytes&&a.memory_type_index==b.memory_type_index;
}
bool same(const NativeImageAllocationBinding& a,const NativeImageAllocationBinding& b)noexcept{
    return a.image==b.image&&a.memory==b.memory&&a.image_generation==b.image_generation&&
        a.memory_generation==b.memory_generation&&a.overlap_revision==b.overlap_revision&&
        a.offset==b.offset&&a.bytes==b.bytes&&a.memory_type_index==b.memory_type_index;
}
}
void NativeImageMemory::disable_at(NativeImageDisableReason reason)const noexcept{
    // Rejection precedes diagnostic publication. First recorded is not a
    // wall-clock ordering claim across racing callers; a snapshot may see
    // disabled=true/reason=none transiently. Neither atomic is ever cleared.
    disabled_.store(true);
    auto expected=NativeImageDisableReason::none;
    disable_reason_.compare_exchange_strong(expected,reason);
}
NativeImageMemory::NativeImageMemory(const VkPhysicalDeviceMemoryProperties& properties,VkDeviceSize granularity)noexcept
    :properties_(properties),granularity_(granularity){
    if(!properties.memoryTypeCount||properties.memoryTypeCount>32||!power_two(granularity)||granularity>size_cap)disable_at(NativeImageDisableReason::invalid_configuration);
}
bool NativeImageMemory::next_generation(std::uint64_t& out)noexcept{
    if(generation_==std::numeric_limits<std::uint64_t>::max()){disable_at(NativeImageDisableReason::generation_exhaustion);return false;}
    out=++generation_;return true;
}
bool NativeImageMemory::conflict(const Binding& a,const Binding& b)const noexcept{
    if(!a.attempted||!b.attempted||!a.memory||a.memory!=b.memory||!a.memory_generation||a.memory_generation!=b.memory_generation)return false;
    if(!a.known||!b.known)return true;
    // Known sizes/offsets have already been checked against the allocation.
    // Page separation conservatively covers buffer/linear/optimal mixtures.
    const auto first_a=a.offset/granularity_,last_a=(a.offset+a.bytes-1)/granularity_;
    const auto first_b=b.offset/granularity_,last_b=(b.offset+b.bytes-1)/granularity_;
    return first_a<=last_b&&first_b<=last_a;
}
void NativeImageMemory::changed(const Binding& range,VkImage exclude)noexcept{
    if(!range.attempted||!range.memory)return;
    for(auto& item:images_)if(item.first!=exclude&&conflict(range,item.second.binding)){
        if(item.second.revision==std::numeric_limits<std::uint64_t>::max()){disable_at(NativeImageDisableReason::revision_exhaustion);return;}
        ++item.second.revision;
    }
}
void NativeImageMemory::uncertain_locked(VkDeviceMemory memory)noexcept{
    if(!memory){disable_at(NativeImageDisableReason::unknown_memory_or_binding);return;}
    const auto found=memories_.find(memory);if(found==memories_.end())return;
    found->second.uncertain=true;
    Binding all;all.memory=memory;all.memory_generation=found->second.generation;all.attempted=true;changed(all);
}
void NativeImageMemory::memory_created(VkDeviceMemory handle,const VkMemoryAllocateInfo& info)noexcept{
    try{
        std::lock_guard<std::mutex> lock(mutex_);if(disabled_||!handle)return;
        memories_.erase(handle);
        if(memories_.size()>=record_cap){disable_at(NativeImageDisableReason::allocation_census);return;}
        Memory m;if(!next_generation(m.generation))return;
        m.size=info.allocationSize;m.type=info.memoryTypeIndex;AllocationFacts facts;
        m.ordinary=ordinary_allocation(info,facts)&&m.type<properties_.memoryTypeCount;
        if(m.type<properties_.memoryTypeCount)m.properties=properties_.memoryTypes[m.type].propertyFlags;
        m.dedicated_image=facts.image;m.dedicated_buffer=facts.buffer;
        if(facts.image){const auto i=images_.find(facts.image);if(i==images_.end())m.ordinary=false;else m.dedicated_generation=i->second.generation;}
        if(facts.buffer){const auto i=buffers_.find(facts.buffer);if(i==buffers_.end())m.ordinary=false;else m.dedicated_generation=i->second.generation;}
        memories_.emplace(handle,m);
    }catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::memory_destroyed(VkDeviceMemory handle)noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);memories_.erase(handle);}catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::image_created(VkImage handle,const VkImageCreateInfo& info)noexcept{
    try{
        std::lock_guard<std::mutex> lock(mutex_);if(disabled_||!handle)return;
        const auto old=images_.find(handle);if(old!=images_.end()){changed(old->second.binding,handle);images_.erase(old);}
        if(images_.size()>=image_record_cap){disable_at(NativeImageDisableReason::image_census);return;}
        Object o;if(!next_generation(o.generation))return;
        constexpr VkImageCreateFlags no_ordinary=VK_IMAGE_CREATE_SPARSE_BINDING_BIT|VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT|VK_IMAGE_CREATE_SPARSE_ALIASED_BIT|VK_IMAGE_CREATE_DISJOINT_BIT;
        o.query_safe=info.sType==VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO&&!(info.flags&no_ordinary)&&image_chain(info)&&
            info.extent.width&&info.extent.height&&info.extent.depth&&info.mipLevels&&info.arrayLayers;
        o.target=o.query_safe&&info.imageType==VK_IMAGE_TYPE_3D&&info.tiling==VK_IMAGE_TILING_OPTIMAL&&info.samples==VK_SAMPLE_COUNT_1_BIT&&
            !(info.flags&~VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)&&(info.usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&info.mipLevels==1&&info.arrayLayers==1;
        images_.emplace(handle,o);
    }catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::image_destroyed(VkImage handle)noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);const auto i=images_.find(handle);if(i!=images_.end()){changed(i->second.binding,handle);images_.erase(i);}}catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::buffer_created(VkBuffer handle,const VkBufferCreateInfo& info)noexcept{
    try{
        std::lock_guard<std::mutex> lock(mutex_);if(disabled_||!handle)return;
        const auto old=buffers_.find(handle);if(old!=buffers_.end()){changed(old->second.binding);buffers_.erase(old);}
        if(buffers_.size()>=record_cap){disable_at(NativeImageDisableReason::buffer_census);return;}
        Object o;if(!next_generation(o.generation))return;
        constexpr VkBufferCreateFlags sparse=VK_BUFFER_CREATE_SPARSE_BINDING_BIT|VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT|VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
        o.query_safe=info.sType==VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO&&!info.pNext&&!(info.flags&sparse)&&info.size&&info.size<=size_cap;
        buffers_.emplace(handle,o);
    }catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::buffer_destroyed(VkBuffer handle)noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);const auto i=buffers_.find(handle);if(i!=buffers_.end()){changed(i->second.binding);buffers_.erase(i);}}catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
bool NativeImageMemory::image_requirements_safe(VkImage handle)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);const auto i=images_.find(handle);return !disabled_&&i!=images_.end()&&i->second.query_safe;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
bool NativeImageMemory::buffer_requirements_safe(VkBuffer handle)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);const auto i=buffers_.find(handle);return !disabled_&&i!=buffers_.end()&&i->second.query_safe;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
void NativeImageMemory::bind(Object& object,VkImage image,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset,const VkMemoryRequirements* req)noexcept{
    if(object.binding.attempted){
        changed(object.binding,image);uncertain_locked(object.binding.memory);uncertain_locked(memory);object.binding.known=false;return;
    }
    auto& b=object.binding;b.attempted=true;b.memory=memory;b.offset=offset;
    const auto allocation=memories_.find(memory);
    if(allocation==memories_.end()){if(!memory)disable_at(NativeImageDisableReason::unknown_memory_or_binding);return;}
    const auto& m=allocation->second;b.memory_generation=m.generation;
    const bool dedicated=(!m.dedicated_image||(m.dedicated_image==image&&m.dedicated_generation==object.generation&&!offset))&&
        (!m.dedicated_buffer||(m.dedicated_buffer==buffer&&m.dedicated_generation==object.generation&&!offset));
    b.known=object.query_safe&&m.ordinary&&dedicated&&req&&req->size&&req->size<=size_cap&&power_two(req->alignment)&&
        req->alignment<=size_cap&&m.type<32&&(req->memoryTypeBits&(1u<<m.type))&&offset%req->alignment==0&&
        offset<=m.size&&req->size<=m.size-offset;
    if(b.known)b.bytes=req->size;
    changed(b,image);
    if(!dedicated)uncertain_locked(memory);
}
void NativeImageMemory::image_bound(VkImage handle,VkDeviceMemory memory,VkDeviceSize offset,const VkMemoryRequirements* req)noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);if(disabled_)return;const auto i=images_.find(handle);if(i==images_.end()){uncertain_locked(memory);return;}bind(i->second,handle,VK_NULL_HANDLE,memory,offset,req);}catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::buffer_bound(VkBuffer handle,VkDeviceMemory memory,VkDeviceSize offset,const VkMemoryRequirements* req)noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);if(disabled_)return;const auto i=buffers_.find(handle);if(i==buffers_.end()){uncertain_locked(memory);return;}bind(i->second,VK_NULL_HANDLE,handle,memory,offset,req);}catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::uncertain_memory(VkDeviceMemory memory)noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);uncertain_locked(memory);}catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::disable()noexcept{disable_at(NativeImageDisableReason::explicit_unobserved);}
bool NativeImageMemory::token(VkImage handle,NativeImageBacking& out)const noexcept{
    const auto i=images_.find(handle);if(i==images_.end()||!i->second.target||!i->second.binding.known)return false;
    const auto& object=i->second;const auto& b=object.binding;const auto a=memories_.find(b.memory);
    if(a==memories_.end()||a->second.generation!=b.memory_generation||!a->second.ordinary||a->second.uncertain||
       !(a->second.properties&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)||
       (a->second.properties&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)))return false;
    for(const auto& other:images_)if(other.first!=handle&&conflict(b,other.second.binding))return false;
    for(const auto& other:buffers_)if(conflict(b,other.second.binding))return false;
    out={handle,b.memory,object.generation,b.memory_generation,object.revision,b.offset,b.bytes,a->second.type};return true;
}
bool NativeImageMemory::exclusive(VkImage handle,NativeImageBacking& out)const noexcept{
    out={};try{std::lock_guard<std::mutex> lock(mutex_);const bool ok=!disabled_&&token(handle,out)&&!disabled_;if(!ok)out={};return ok;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);out={};return false;}
}
bool NativeImageMemory::current(const NativeImageBacking& backing)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);NativeImageBacking now;return !disabled_&&backing&&token(backing.image,now)&&same(backing,now)&&!disabled_;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
bool NativeImageMemory::allocation_token(VkImage handle,NativeImageAllocationBinding& out)const noexcept{
    const auto i=images_.find(handle);if(i==images_.end()||!i->second.target||!i->second.binding.known)return false;
    const auto& object=i->second;const auto& b=object.binding;const auto a=memories_.find(b.memory);
    if(a==memories_.end()||a->second.generation!=b.memory_generation||!a->second.ordinary||a->second.uncertain||
       !(a->second.properties&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)||
       (a->second.properties&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)))return false;
    // This deliberately does not exclude any competing object binding. The
    // caller still owes the covering-producer/immediate-copy proof.
    out={handle,b.memory,object.generation,b.memory_generation,object.revision,b.offset,b.bytes,a->second.type};return true;
}
bool NativeImageMemory::ordinary_binding(VkImage handle,NativeImageAllocationBinding& out)const noexcept{
    out={};try{std::lock_guard<std::mutex> lock(mutex_);const bool ok=!disabled_&&allocation_token(handle,out)&&!disabled_;if(!ok)out={};return ok;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);out={};return false;}
}
bool NativeImageMemory::current(const NativeImageAllocationBinding& binding)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);NativeImageAllocationBinding now;return !disabled_&&binding&&allocation_token(binding.image,now)&&same(binding,now)&&!disabled_;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
NativeImageMemoryDiagnostic NativeImageMemory::diagnostic(VkImage image)const noexcept{
    try{
        std::lock_guard<std::mutex> lock(mutex_);NativeImageMemoryDiagnostic out;
        out.observed=true;out.disabled=disabled_;out.disable_reason=disable_reason_.load();out.granularity=granularity_;
        out.allocations=static_cast<std::uint32_t>(memories_.size());out.images=static_cast<std::uint32_t>(images_.size());out.buffers=static_cast<std::uint32_t>(buffers_.size());
        const auto found=images_.find(image);if(found==images_.end())return out;
        const auto& object=found->second;const auto& binding=object.binding;
        out.image_known=true;out.target=object.target;out.query_safe=object.query_safe;out.bind_attempted=binding.attempted;out.footprint_known=binding.known;
        out.memory=binding.memory;out.offset=binding.offset;out.bytes=binding.bytes;out.bound_memory_generation=binding.memory_generation;
        const auto allocation=memories_.find(binding.memory);
        if(allocation!=memories_.end()){const auto& memory=allocation->second;out.allocation_known=true;out.allocation_ordinary=memory.ordinary;out.allocation_uncertain=memory.uncertain;
            out.current_memory_generation=memory.generation;out.allocation_bytes=memory.size;out.memory_type=memory.type;out.memory_properties=memory.properties;}
        for(const auto& other:images_)if(other.first!=image&&conflict(binding,other.second.binding))++out.image_conflicts;
        for(const auto& other:buffers_)if(conflict(binding,other.second.binding))++out.buffer_conflicts;
        return out;
    }catch(...){return {};}
}
}
