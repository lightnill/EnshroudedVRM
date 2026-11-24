#include "native_image_memory.hpp"
#include <array>
#include <cmath>
#include <limits>

namespace evrm { namespace {
bool power_two(VkDeviceSize n)noexcept{return n&&!(n&(n-1));}
struct AllocationFacts {VkImage image{};VkBuffer buffer{};
    VkStructureType extension=VK_STRUCTURE_TYPE_MAX_ENUM,last_extension=VK_STRUCTURE_TYPE_MAX_ENUM;
    VkMemoryAllocateFlags flags=0;std::uint32_t device_mask=0;};
bool ordinary_allocation(const VkMemoryAllocateInfo& info,AllocationFacts& out)noexcept{
    if(info.sType!=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO||!info.allocationSize||info.allocationSize>NativeImageMemory::size_cap)return false;
    std::array<const VkBaseInStructure*,4> visited{};unsigned count=0,kinds=0;
    auto node=static_cast<const VkBaseInStructure*>(info.pNext);if(node)out.extension=node->sType;
    while(node){
        if(count==visited.size())return false;
        for(unsigned i=0;i<count;++i)if(visited[i]==node)return false;
        visited[count++]=node;unsigned kind=0;out.last_extension=node->sType;
        switch(node->sType){
        case VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO:{
            kind=1;if(kinds&kind)return false;
            const auto& f=*reinterpret_cast<const VkMemoryAllocateFlagsInfo*>(node);out.flags=f.flags;out.device_mask=f.deviceMask;
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
    out.last_extension=VK_STRUCTURE_TYPE_MAX_ENUM;return true;
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
bool same(const NativeBufferBacking& a,const NativeBufferBacking& b)noexcept{
    return a.buffer==b.buffer&&a.memory==b.memory&&a.buffer_generation==b.buffer_generation&&a.memory_generation==b.memory_generation&&
        a.overlap_revision==b.overlap_revision&&a.offset==b.offset&&a.buffer_offset==b.buffer_offset&&a.bytes==b.bytes&&a.memory_type_index==b.memory_type_index;
}
bool same(const NativeScopedBufferBinding& a,const NativeScopedBufferBinding& b)noexcept{
    return a.buffer==b.buffer&&a.memory==b.memory&&a.buffer_generation==b.buffer_generation&&a.memory_generation==b.memory_generation&&
        a.overlap_revision==b.overlap_revision&&a.offset==b.offset&&a.buffer_offset==b.buffer_offset&&a.bytes==b.bytes&&
        a.memory_type_index==b.memory_type_index&&a.registry_serial==b.registry_serial;
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
std::uint64_t NativeImageMemory::new_registry_serial()noexcept{
    static std::atomic<std::uint64_t> next{1};auto value=next.load(std::memory_order_relaxed);
    while(value&&value!=(std::numeric_limits<std::uint64_t>::max)())
        if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value;
    return 0;
}
NativeImageMemory::NativeImageMemory(const VkPhysicalDeviceMemoryProperties& properties,VkDeviceSize granularity)noexcept
    :properties_(properties),granularity_(granularity){
    if(!registry_serial_)disable_at(NativeImageDisableReason::generation_exhaustion);
    if(!properties.memoryTypeCount||properties.memoryTypeCount>32||!power_two(granularity)||granularity>size_cap)disable_at(NativeImageDisableReason::invalid_configuration);
}
bool NativeImageMemory::next_generation(std::uint64_t& out)noexcept{
    if(generation_==std::numeric_limits<std::uint64_t>::max()){disable_at(NativeImageDisableReason::generation_exhaustion);return false;}
    out=++generation_;return true;
}
bool NativeImageMemory::buffer_conflict(const Binding& a,const Binding& b)const noexcept{
    if(!a.attempted||!b.attempted||!a.memory||a.memory!=b.memory||!a.memory_generation||a.memory_generation!=b.memory_generation)return false;
    if(!a.known||!b.known)return true;
    // Both resources are linear. Only actual footprint intersections alias;
    // buffer-image page padding does not apply to a pair of ordinary buffers.
    return a.offset<b.offset+b.bytes&&b.offset<a.offset+a.bytes;
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
void NativeImageMemory::changed(const Binding& range,VkImage exclude,VkBuffer exclude_buffer)noexcept{
    if(!range.attempted||!range.memory)return;
    for(auto& item:images_)if(item.first!=exclude&&conflict(range,item.second.binding)){
        if(item.second.revision==std::numeric_limits<std::uint64_t>::max()){disable_at(NativeImageDisableReason::revision_exhaustion);return;}
        ++item.second.revision;
    }
    for(auto& item:buffers_)if(item.first!=exclude_buffer&&(exclude_buffer?buffer_conflict(range,item.second.binding):conflict(range,item.second.binding))){
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
        m.extension=facts.extension;m.last_extension=facts.last_extension;m.flags=facts.flags;m.device_mask=facts.device_mask;
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
        const auto old=images_.find(handle);if(old!=images_.end()){changed(old->second.binding,handle);unindex(old->second.binding,true);images_.erase(old);}
        if(images_.size()>=image_record_cap){disable_at(NativeImageDisableReason::image_census);return;}
        Object o;if(!next_generation(o.generation))return;
        constexpr VkImageCreateFlags no_ordinary=VK_IMAGE_CREATE_SPARSE_BINDING_BIT|VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT|VK_IMAGE_CREATE_SPARSE_ALIASED_BIT|VK_IMAGE_CREATE_DISJOINT_BIT;
        o.creation_flags=info.flags;if(info.sType==VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO&&info.pNext)o.creation_extension=static_cast<const VkBaseInStructure*>(info.pNext)->sType;
        o.query_safe=info.sType==VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO&&!(info.flags&no_ordinary)&&image_chain(info)&&
            info.extent.width&&info.extent.height&&info.extent.depth&&info.mipLevels&&info.arrayLayers;
        o.target=o.query_safe&&info.imageType==VK_IMAGE_TYPE_3D&&info.tiling==VK_IMAGE_TILING_OPTIMAL&&info.samples==VK_SAMPLE_COUNT_1_BIT&&
            !(info.flags&~VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)&&(info.usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&info.mipLevels==1&&info.arrayLayers==1;
        images_.emplace(handle,o);
    }catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::image_destroyed(VkImage handle)noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);const auto i=images_.find(handle);if(i!=images_.end()){changed(i->second.binding,handle);unindex(i->second.binding,true);images_.erase(i);}}catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::buffer_created(VkBuffer handle,const VkBufferCreateInfo& info)noexcept{
    try{
        std::lock_guard<std::mutex> lock(mutex_);if(disabled_||!handle)return;
        const auto old=buffers_.find(handle);if(old!=buffers_.end()){changed(old->second.binding,VK_NULL_HANDLE,handle);unindex(old->second.binding,false);buffers_.erase(old);}
        if(buffers_.size()>=record_cap){disable_at(NativeImageDisableReason::buffer_census);return;}
        Object o;if(!next_generation(o.generation))return;o.logical_bytes=info.size;
        constexpr VkBufferCreateFlags sparse=VK_BUFFER_CREATE_SPARSE_BINDING_BIT|VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT|VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
        o.query_safe=info.sType==VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO&&!info.pNext&&!(info.flags&sparse)&&info.size&&info.size<=size_cap;
        buffers_.emplace(handle,o);
    }catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
void NativeImageMemory::buffer_destroyed(VkBuffer handle)noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);const auto i=buffers_.find(handle);if(i!=buffers_.end()){changed(i->second.binding,VK_NULL_HANDLE,handle);unindex(i->second.binding,false);buffers_.erase(i);}}catch(...){disable_at(NativeImageDisableReason::metadata_exception);}
}
bool NativeImageMemory::image_requirements_safe(VkImage handle)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);const auto i=images_.find(handle);return !disabled_&&i!=images_.end()&&i->second.query_safe;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
bool NativeImageMemory::buffer_requirements_safe(VkBuffer handle)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);const auto i=buffers_.find(handle);return !disabled_&&i!=buffers_.end()&&i->second.query_safe;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
void NativeImageMemory::unindex(Binding& binding,bool image)noexcept{
    const auto allocation=memories_.find(binding.memory);
    if(allocation==memories_.end()||allocation->second.generation!=binding.memory_generation)return;
    auto* link=image?&allocation->second.image_bindings:&allocation->second.buffer_bindings;
    while(*link&&*link!=&binding)link=&(*link)->index_next;
    if(*link!=&binding){disable_at(NativeImageDisableReason::metadata_exception);return;}
    *link=binding.index_next;binding.index_next=nullptr;
}
void NativeImageMemory::bind(Object& object,VkImage image,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset,const VkMemoryRequirements* req)noexcept{
    if(object.binding.attempted){
        changed(object.binding,image,buffer);uncertain_locked(object.binding.memory);uncertain_locked(memory);object.binding.known=false;return;
    }
    auto& b=object.binding;b.attempted=true;b.memory=memory;b.offset=offset;b.requirements_observed=req!=nullptr;
    if(req){b.requirement_bytes=req->size;b.requirement_alignment=req->alignment;b.requirement_memory_types=req->memoryTypeBits;}
    const auto allocation=memories_.find(memory);
    if(allocation==memories_.end()){if(!memory)disable_at(NativeImageDisableReason::unknown_memory_or_binding);return;}
    const auto& m=allocation->second;b.memory_generation=m.generation;
    const bool dedicated=(!m.dedicated_image||(m.dedicated_image==image&&m.dedicated_generation==object.generation&&!offset))&&
        (!m.dedicated_buffer||(m.dedicated_buffer==buffer&&m.dedicated_generation==object.generation&&!offset));
    b.known=object.query_safe&&m.ordinary&&dedicated&&req&&req->size&&req->size<=size_cap&&power_two(req->alignment)&&
        req->alignment<=size_cap&&m.type<32&&(req->memoryTypeBits&(1u<<m.type))&&offset%req->alignment==0&&
        offset<=m.size&&req->size<=m.size-offset;
    if(b.known)b.bytes=req->size;
    changed(b,image,buffer);
    if(!dedicated)uncertain_locked(memory);
    // Include unknown footprints too. Intrusive map-node links require no
    // extra allocation or capacity retained after object retirement.
    auto& head=image?allocation->second.image_bindings:allocation->second.buffer_bindings;
    b.index_next=head;head=&b;
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
bool NativeImageMemory::scene_binding_token(VkImage handle,NativeSceneImageBinding& out)const noexcept{
    const auto i=images_.find(handle);if(i==images_.end()||!i->second.query_safe||!i->second.binding.known)return false;
    const auto& object=i->second;const auto& b=object.binding;const auto a=memories_.find(b.memory);
    if(a==memories_.end()||a->second.generation!=b.memory_generation||!a->second.ordinary||a->second.uncertain||
       (a->second.properties&(VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)))return false;
    // Aliased image objects can outlive their disjoint render-graph uses.
    // Keep uncertain footprints fail-closed, and keep the whole object epoch;
    // do not reinterpret alias contents or relax the original3D GI policy.
    if(object.scene_binding_revision!=object.revision){
        // changed() advances this image revision for every competing binding
        // or retirement. Never cache a rejection or reuse an old captured epoch.
        for(const auto& other:images_)if(other.first!=handle&&conflict(b,other.second.binding)&&!other.second.binding.known)return false;
        for(const auto& other:buffers_)if(conflict(b,other.second.binding))return false;
        object.scene_binding_revision=object.revision;
    }
    out={handle,b.memory,object.generation,b.memory_generation,object.revision,b.offset,b.bytes,a->second.type,registry_serial_};return true;
}
bool NativeImageMemory::scene_image_binding(VkImage handle,NativeSceneImageBinding& out)const noexcept{
    out={};try{std::lock_guard<std::mutex> lock(mutex_);const bool ok=!disabled_&&scene_binding_token(handle,out)&&!disabled_;if(!ok)out={};return ok;
    }catch(...){disable_at(NativeImageDisableReason::metadata_exception);out={};return false;}
}
bool NativeImageMemory::current(const NativeSceneImageBinding& prior)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);NativeSceneImageBinding now;
        return !disabled_&&prior&&prior.registry_serial==registry_serial_&&scene_binding_token(prior.image,now)&&
            prior.image==now.image&&prior.memory==now.memory&&prior.image_generation==now.image_generation&&
            prior.memory_generation==now.memory_generation&&prior.overlap_revision==now.overlap_revision&&
            prior.offset==now.offset&&prior.bytes==now.bytes&&prior.memory_type_index==now.memory_type_index&&!disabled_;
    }catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
bool NativeImageMemory::scene_token(VkImage handle,NativeSceneImageBacking& out)const noexcept{
    const auto i=images_.find(handle);if(i==images_.end()||!i->second.query_safe||!i->second.binding.known)return false;
    const auto& object=i->second;const auto& b=object.binding;const auto a=memories_.find(b.memory);
    if(a==memories_.end()||a->second.generation!=b.memory_generation||!a->second.ordinary||a->second.uncertain||
       (a->second.properties&(VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)))return false;
    for(const auto& other:images_)if(other.first!=handle&&conflict(b,other.second.binding))return false;
    for(const auto& other:buffers_)if(conflict(b,other.second.binding))return false;
    out={handle,b.memory,object.generation,b.memory_generation,object.revision,b.offset,b.bytes,a->second.type,registry_serial_};return true;
}
bool NativeImageMemory::unaliased_scene_image(VkImage handle,NativeSceneImageBacking& out)const noexcept{
    out={};try{std::lock_guard<std::mutex> lock(mutex_);const bool ok=!disabled_&&scene_token(handle,out)&&!disabled_;if(!ok)out={};return ok;
    }catch(...){disable_at(NativeImageDisableReason::metadata_exception);out={};return false;}
}
bool NativeImageMemory::current(const NativeSceneImageBacking& prior)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);NativeSceneImageBacking now;
        return !disabled_&&prior&&prior.registry_serial==registry_serial_&&scene_token(prior.image,now)&&
            prior.image==now.image&&prior.memory==now.memory&&prior.image_generation==now.image_generation&&
            prior.memory_generation==now.memory_generation&&prior.overlap_revision==now.overlap_revision&&
            prior.offset==now.offset&&prior.bytes==now.bytes&&prior.memory_type_index==now.memory_type_index&&!disabled_;
    }catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
bool NativeImageMemory::token(VkImage handle,NativeImageBacking& out)const noexcept{
    const auto i=images_.find(handle);if(i==images_.end()||!i->second.target||!i->second.binding.known)return false;
    const auto& object=i->second;const auto& b=object.binding;const auto a=memories_.find(b.memory);
    if(a==memories_.end()||a->second.generation!=b.memory_generation||!a->second.ordinary||a->second.uncertain||
       !(a->second.properties&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)||
       (a->second.properties&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)))return false;
    if(object.strict_alias_revision!=object.revision){
        // conflict() rejects every different allocation/generation. These
        // mutex-protected lists contain all remaining candidates, not a proof.
        for(const auto* other=a->second.image_bindings;other;other=other->index_next)if(other!=&b&&conflict(b,*other))return false;
        for(const auto* other=a->second.buffer_bindings;other;other=other->index_next)if(conflict(b,*other))return false;
        object.strict_alias_revision=object.revision;
    }
    out={handle,b.memory,object.generation,b.memory_generation,object.revision,b.offset,b.bytes,a->second.type};return true;
}
bool NativeImageMemory::exclusive(VkImage handle,NativeImageBacking& out)const noexcept{
    out={};try{std::lock_guard<std::mutex> lock(mutex_);const bool ok=!disabled_&&token(handle,out)&&!disabled_;if(!ok)out={};return ok;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);out={};return false;}
}
bool NativeImageMemory::current(const NativeImageBacking& backing)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);NativeImageBacking now;return !disabled_&&backing&&token(backing.image,now)&&same(backing,now)&&!disabled_;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
bool NativeImageMemory::buffer_token(VkBuffer handle,VkDeviceSize offset,VkDeviceSize bytes,NativeBufferBacking& out)const noexcept{
    const auto i=buffers_.find(handle);if(i==buffers_.end()||!i->second.query_safe||!i->second.binding.known)return false;
    const auto& object=i->second;const auto& b=object.binding;const auto a=memories_.find(b.memory);
    if(!bytes||offset>object.logical_bytes||bytes>object.logical_bytes-offset||offset>b.bytes||bytes>b.bytes-offset||
       a==memories_.end()||a->second.generation!=b.memory_generation||!a->second.ordinary||a->second.uncertain||
       (a->second.properties&(VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)))return false;
    if(object.strict_alias_revision!=object.revision){
        for(const auto& other:images_)if(conflict(b,other.second.binding))return false;
        for(const auto& other:buffers_)if(other.first!=handle&&buffer_conflict(b,other.second.binding))return false;
        object.strict_alias_revision=object.revision;
    }
    out={handle,b.memory,object.generation,b.memory_generation,object.revision,b.offset+offset,offset,bytes,a->second.type};return true;
}
bool NativeImageMemory::exclusive_buffer(VkBuffer handle,VkDeviceSize offset,VkDeviceSize bytes,NativeBufferBacking& out)const noexcept{
    out={};try{std::lock_guard<std::mutex> lock(mutex_);const bool ok=!disabled_&&buffer_token(handle,offset,bytes,out)&&!disabled_;if(!ok)out={};return ok;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);out={};return false;}
}
bool NativeImageMemory::current(const NativeBufferBacking& backing)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);NativeBufferBacking now;return !disabled_&&backing&&buffer_token(backing.buffer,backing.buffer_offset,backing.bytes,now)&&same(backing,now)&&!disabled_;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
}
bool NativeImageMemory::scoped_buffer_token(VkBuffer handle,VkDeviceSize offset,VkDeviceSize bytes,NativeScopedBufferBinding& out)const noexcept{
    const auto i=buffers_.find(handle);if(i==buffers_.end()||!i->second.query_safe||!i->second.binding.known)return false;
    const auto& object=i->second;const auto& b=object.binding;const auto a=memories_.find(b.memory);
    if(!bytes||offset>object.logical_bytes||bytes>object.logical_bytes-offset||offset>b.bytes||bytes>b.bytes-offset||
       a==memories_.end()||a->second.generation!=b.memory_generation||!a->second.ordinary||a->second.uncertain||
       (a->second.properties&(VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)))return false;
    if(object.scoped_buffer_revision!=object.revision){
        for(const auto& other:images_)if(conflict(b,other.second.binding))return false;
        for(const auto& other:buffers_)if(other.first!=handle&&buffer_conflict(b,other.second.binding)&&
            (!other.second.query_safe||!other.second.binding.known))return false;
        object.scoped_buffer_revision=object.revision;
    }
    out={handle,b.memory,object.generation,b.memory_generation,object.revision,b.offset+offset,offset,bytes,a->second.type,registry_serial_};return true;
}
bool NativeImageMemory::scoped_buffer_binding(VkBuffer handle,VkDeviceSize offset,VkDeviceSize bytes,NativeScopedBufferBinding& out)const noexcept{
    out={};try{std::lock_guard<std::mutex> lock(mutex_);const bool ok=!disabled_&&scoped_buffer_token(handle,offset,bytes,out)&&!disabled_;if(!ok)out={};return ok;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);out={};return false;}
}
bool NativeImageMemory::current(const NativeScopedBufferBinding& binding)const noexcept{
    try{std::lock_guard<std::mutex> lock(mutex_);NativeScopedBufferBinding now;return !disabled_&&binding&&binding.registry_serial==registry_serial_&&
        scoped_buffer_token(binding.buffer,binding.buffer_offset,binding.bytes,now)&&same(binding,now)&&!disabled_;}catch(...){disable_at(NativeImageDisableReason::metadata_exception);return false;}
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
        out.creation_flags=object.creation_flags;out.creation_extension=object.creation_extension;
        out.requirements_observed=binding.requirements_observed;out.requirement_bytes=binding.requirement_bytes;out.requirement_alignment=binding.requirement_alignment;out.requirement_memory_types=binding.requirement_memory_types;
        out.memory=binding.memory;out.offset=binding.offset;out.bytes=binding.bytes;out.bound_memory_generation=binding.memory_generation;
        const auto allocation=memories_.find(binding.memory);
        if(allocation!=memories_.end()){const auto& memory=allocation->second;out.allocation_known=true;out.allocation_ordinary=memory.ordinary;out.allocation_uncertain=memory.uncertain;
            out.current_memory_generation=memory.generation;out.allocation_bytes=memory.size;out.memory_type=memory.type;out.memory_properties=memory.properties;
            out.allocation_extension=memory.extension;out.allocation_last_extension=memory.last_extension;out.allocation_flags=memory.flags;out.allocation_device_mask=memory.device_mask;}
        for(const auto& other:images_)if(other.first!=image&&conflict(binding,other.second.binding)){
            if(out.image_conflicts<out.conflicting_images.size())out.conflicting_images[out.image_conflicts]=other.first;
            ++out.image_conflicts;if(!other.second.binding.known)++out.unknown_image_conflicts;}
        for(const auto& other:buffers_)if(conflict(binding,other.second.binding)){
            if(out.buffer_conflicts<out.conflicting_buffers.size())out.conflicting_buffers[out.buffer_conflicts]=other.first;
            ++out.buffer_conflicts;if(!other.second.binding.known)++out.unknown_buffer_conflicts;}
        return out;
    }catch(...){return {};}
}
NativeBufferBackingDiagnostic NativeImageMemory::diagnostic_buffer(VkBuffer buffer,VkDeviceSize offset,VkDeviceSize bytes)const noexcept{
    NativeBufferBackingDiagnostic out;
    try{
        std::lock_guard<std::mutex> lock(mutex_);using Why=NativeBufferBackingRejection;
        out.observed=true;out.disabled=disabled_;out.disable_reason=disable_reason_.load();out.granularity=granularity_;
        out.requested_offset=offset;out.requested_bytes=bytes;
        const auto found=buffers_.find(buffer);
        if(found==buffers_.end()){out.reason=out.disabled?Why::registry_disabled:Why::unknown_buffer;return out;}
        const auto& object=found->second;const auto& b=object.binding;
        out.buffer_known=true;out.query_safe=object.query_safe;out.logical_bytes=object.logical_bytes;out.buffer_generation=object.generation;
        out.footprint_known=b.known;out.memory=b.memory;out.binding_offset=b.offset;out.footprint_bytes=b.bytes;out.bound_memory_generation=b.memory_generation;
        const auto allocation=memories_.find(b.memory);
        if(allocation!=memories_.end()){const auto& m=allocation->second;out.allocation_known=true;out.allocation_ordinary=m.ordinary;out.allocation_uncertain=m.uncertain;
            out.memory_type=m.type;out.memory_properties=m.properties;out.allocation_bytes=m.size;out.current_memory_generation=m.generation;}
        out.range_known=b.known&&bytes&&offset<=object.logical_bytes&&bytes<=object.logical_bytes-offset&&offset<=b.bytes&&bytes<=b.bytes-offset;
        Binding range=b;if(out.range_known){range.offset+=offset;range.bytes=bytes;}
        for(const auto& item:images_){const auto& other=item.second.binding;
            if(conflict(b,other)){++out.image_conflicts;if(!other.known)++out.unknown_image_conflicts;}
            if(out.range_known&&conflict(range,other))++out.range_image_conflicts;}
        for(const auto& item:buffers_)if(item.first!=buffer){const auto& other=item.second.binding;
            if(buffer_conflict(b,other)){++out.buffer_conflicts;if(!other.known)++out.unknown_buffer_conflicts;}
            if(out.range_known&&buffer_conflict(range,other))++out.range_buffer_conflicts;}
        NativeBufferBacking proof;out.eligible=!disabled_&&buffer_token(buffer,offset,bytes,proof)&&!disabled_;
        if(out.eligible)out.reason=Why::none;
        else if(disabled_)out.reason=Why::registry_disabled;
        else if(!out.query_safe)out.reason=Why::unsafe_creation;
        else if(!out.footprint_known)out.reason=Why::unknown_footprint;
        else if(!out.range_known)out.reason=Why::invalid_range;
        else if(!out.allocation_known)out.reason=Why::unknown_allocation;
        else if(out.current_memory_generation!=out.bound_memory_generation)out.reason=Why::stale_allocation;
        else if(!out.allocation_ordinary)out.reason=Why::nonordinary_allocation;
        else if(out.allocation_uncertain)out.reason=Why::uncertain_allocation;
        else if(out.memory_properties&(VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT))out.reason=Why::prohibited_memory;
        else if(out.image_conflicts)out.reason=Why::image_conflict;
        else if(out.buffer_conflicts)out.reason=Why::buffer_conflict;
        else out.reason=Why::metadata_exception;
        return out;
    }catch(...){out.reason=NativeBufferBackingRejection::metadata_exception;return out;}
}
}
