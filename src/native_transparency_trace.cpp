#include "native_transparency_trace.hpp"
#include <algorithm>
#include <limits>
#include <sstream>

namespace evrm { namespace {
using Recording=NativeTransparencyRecording;
using Trace=NativeTransparencyTrace;
using U=std::uint64_t;
void incomplete(Recording& r,bool overflow=false)noexcept{r.incomplete=true;r.overflow=r.overflow||overflow;}
void dropped_call(Recording& r)noexcept{incomplete(r,true);if(r.dropped!=std::numeric_limits<U>::max())++r.dropped;}
bool call(Recording& r)noexcept{
    if(r.calls==std::numeric_limits<U>::max()){dropped_call(r);return false;}
    ++r.calls;if(r.entries>=Trace::entry_limit){dropped_call(r);return false;}return true;
}
template<class Body>void line(Recording& r,const Trace::Output& output,Body&& body)noexcept{
    if(!output){r.output_failed=true;incomplete(r);return;}
    try{
        std::ostringstream out;out.exceptions(std::ios::badbit|std::ios::failbit);
        out<<"native-transparency.trace recording="<<r.recording_id<<" command=0x"<<std::hex<<r.command<<std::dec;
        body(out);auto text=out.str();
        if(text.size()>Trace::line_limit){incomplete(r);constexpr char suffix[]=" truncated=1";
            text.resize(Trace::line_limit-(sizeof(suffix)-1));text+=suffix;}
        output(text);
    }catch(...){r.output_failed=true;incomplete(r);}
}
void unsupported(Recording& r,const Trace::Output& output,const char* mode,const char* reason,std::int32_t type=0)noexcept{
    incomplete(r);if(r.entries>=Trace::entry_limit){dropped_call(r);return;}const auto ordinal=r.entries++;
    line(r,output,[&](auto& out){out<<" event=unsupported mode="<<mode<<" call="<<r.calls<<" entry="<<ordinal
        <<" reason="<<reason<<" observed-type="<<type<<" buffer-payloads=omitted incomplete=1";});
}
struct ImageFields {
    U image=0;VkImageLayout old_layout{},new_layout{};
    std::uint32_t src_queue=0,dst_queue=0;VkImageSubresourceRange range{};
};
void barrier(Recording& r,const Trace::Output& output,const char* mode,const char* kind,VkDependencyFlags flags,
             U src_stage,U dst_stage,U src_access,U dst_access,std::uint32_t buffers,bool chain,const ImageFields* image=nullptr)noexcept{
    if(chain||buffers)incomplete(r);
    if(r.entries>=Trace::entry_limit){dropped_call(r);return;}const auto ordinal=r.entries++;
    line(r,output,[&](auto& out){out<<" event=barrier mode="<<mode<<" call="<<r.calls<<" entry="<<ordinal<<" kind="<<kind
        <<" dependency-flags=0x"<<std::hex<<flags<<" src-stage=0x"<<src_stage<<" dst-stage=0x"<<dst_stage
        <<" src-access=0x"<<src_access<<" dst-access=0x"<<dst_access<<std::dec;
        if(image)out<<" image=0x"<<std::hex<<image->image<<std::dec<<" old-layout="<<static_cast<std::int32_t>(image->old_layout)
            <<" new-layout="<<static_cast<std::int32_t>(image->new_layout)<<" src-queue="<<image->src_queue<<" dst-queue="<<image->dst_queue
            <<" aspect=0x"<<std::hex<<image->range.aspectMask<<std::dec<<" base-mip="<<image->range.baseMipLevel
            <<" level-count="<<image->range.levelCount<<" base-layer="<<image->range.baseArrayLayer<<" layer-count="<<image->range.layerCount;
        out<<" buffer-omitted="<<buffers<<" unsupported-chain="<<chain;});
}
bool counts(std::uint32_t memory,std::uint32_t buffers,std::uint32_t images)noexcept{
    return U(memory)+buffers+images<=Trace::call_limit;
}
} // namespace
bool NativeTransparencyTrace::set_output(const Output& output)noexcept{try{output_=output;return true;}catch(...){return false;}}
bool NativeTransparencyTrace::active(const NativeTransparencyRecording& r)const noexcept{
    return r.selected&&!r.ended&&r.recording_id&&r.command&&
        std::find(selected_ids_.begin(),selected_ids_.begin()+selected_count_,r.recording_id)!=selected_ids_.begin()+selected_count_;
}
void NativeTransparencyTrace::begin(NativeTransparencyRecording& r,VkCommandBuffer command)noexcept{
    reset(r);r.command=static_cast<U>(reinterpret_cast<std::uintptr_t>(command));
    if(last_id_==std::numeric_limits<U>::max()){incomplete(r,true);return;}
    r.recording_id=++last_id_;if(!command)incomplete(r);
}
void NativeTransparencyTrace::reset(NativeTransparencyRecording& r)noexcept{
    if(active(r)){incomplete(r);finish(r,VK_NOT_READY,"reset");}r={};
}
bool NativeTransparencyTrace::select(NativeTransparencyRecording& r)noexcept{
    if(r.selected)return active(r);
    if(!r.recording_id||!r.command||r.ended||selected_count_>=selection_limit)return false;
    selected_ids_[selected_count_++]=r.recording_id;r.selected=true;
    line(r,output_,[&](auto& out){out<<" event=selected buffer-payloads=omitted entry-limit="<<entry_limit
        <<" call-limit="<<call_limit<<" diagnostic-only=1";});return true;
}
void NativeTransparencyTrace::legacy(NativeTransparencyRecording& r,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,
    std::uint32_t memory_count,const VkMemoryBarrier* memory,std::uint32_t buffer_count,const VkBufferMemoryBarrier* buffers,
    std::uint32_t image_count,const VkImageMemoryBarrier* images)noexcept{
    (void)buffers;if(!active(r)||!call(r))return;
    if(buffer_count)incomplete(r);
    if(!counts(memory_count,buffer_count,image_count)){incomplete(r,true);unsupported(r,output_,"legacy","count");return;}
    if((memory_count&&!memory)||(image_count&&!images)){unsupported(r,output_,"legacy","missing-array");return;}
    if(!memory_count&&!image_count){barrier(r,output_,"legacy","empty",flags,src,dst,0,0,buffer_count,false);return;}
    for(std::uint32_t i=0;i<memory_count;++i){
        if(r.entries>=entry_limit){dropped_call(r);return;}const auto& b=memory[i];
        if(b.sType!=VK_STRUCTURE_TYPE_MEMORY_BARRIER){unsupported(r,output_,"legacy","barrier-type",b.sType);continue;}
        barrier(r,output_,"legacy","memory",flags,src,dst,b.srcAccessMask,b.dstAccessMask,buffer_count,b.pNext!=nullptr);
    }
    for(std::uint32_t i=0;i<image_count;++i){
        if(r.entries>=entry_limit){dropped_call(r);return;}const auto& b=images[i];
        if(b.sType!=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER){unsupported(r,output_,"legacy","barrier-type",b.sType);continue;}
        const ImageFields fields{static_cast<U>(reinterpret_cast<std::uintptr_t>(b.image)),b.oldLayout,b.newLayout,b.srcQueueFamilyIndex,b.dstQueueFamilyIndex,b.subresourceRange};
        barrier(r,output_,"legacy","image",flags,src,dst,b.srcAccessMask,b.dstAccessMask,buffer_count,b.pNext!=nullptr,&fields);
    }
}
void NativeTransparencyTrace::sync2(NativeTransparencyRecording& r,const VkDependencyInfo* info)noexcept{
    if(!active(r)||!call(r))return;
    if(!info){unsupported(r,output_,"sync2","null-dependency");return;}
    if(info->sType!=VK_STRUCTURE_TYPE_DEPENDENCY_INFO){unsupported(r,output_,"sync2","dependency-type",info->sType);return;}
    if(info->pNext){unsupported(r,output_,"sync2","dependency-chain",info->sType);return;}
    const auto memory_count=info->memoryBarrierCount,buffer_count=info->bufferMemoryBarrierCount,image_count=info->imageMemoryBarrierCount;
    if(buffer_count)incomplete(r);
    if(!counts(memory_count,buffer_count,image_count)){incomplete(r,true);unsupported(r,output_,"sync2","count");return;}
    if((memory_count&&!info->pMemoryBarriers)||(image_count&&!info->pImageMemoryBarriers)){unsupported(r,output_,"sync2","missing-array");return;}
    if(!memory_count&&!image_count){barrier(r,output_,"sync2","empty",info->dependencyFlags,0,0,0,0,buffer_count,false);return;}
    for(std::uint32_t i=0;i<memory_count;++i){
        if(r.entries>=entry_limit){dropped_call(r);return;}const auto& b=info->pMemoryBarriers[i];
        if(b.sType!=VK_STRUCTURE_TYPE_MEMORY_BARRIER_2){unsupported(r,output_,"sync2","barrier-type",b.sType);continue;}
        barrier(r,output_,"sync2","memory",info->dependencyFlags,b.srcStageMask,b.dstStageMask,b.srcAccessMask,b.dstAccessMask,buffer_count,b.pNext!=nullptr);
    }
    for(std::uint32_t i=0;i<image_count;++i){
        if(r.entries>=entry_limit){dropped_call(r);return;}const auto& b=info->pImageMemoryBarriers[i];
        if(b.sType!=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2){unsupported(r,output_,"sync2","barrier-type",b.sType);continue;}
        const ImageFields fields{static_cast<U>(reinterpret_cast<std::uintptr_t>(b.image)),b.oldLayout,b.newLayout,b.srcQueueFamilyIndex,b.dstQueueFamilyIndex,b.subresourceRange};
        barrier(r,output_,"sync2","image",info->dependencyFlags,b.srcStageMask,b.dstStageMask,b.srcAccessMask,b.dstAccessMask,buffer_count,b.pNext!=nullptr,&fields);
    }
}
void NativeTransparencyTrace::finish(NativeTransparencyRecording& r,VkResult result,const char* reason)noexcept{
    if(!active(r)){r.ended=true;return;}if(result!=VK_SUCCESS)incomplete(r);r.ended=true;
    line(r,output_,[&](auto& out){out<<" event=summary reason="<<reason<<" result="<<static_cast<std::int32_t>(result)
        <<" calls="<<r.calls<<" entries="<<r.entries<<" dropped-calls="<<r.dropped<<" incomplete="<<r.incomplete
        <<" overflow="<<r.overflow<<" output-failed="<<r.output_failed<<" buffer-payloads=omitted";});
}
void NativeTransparencyTrace::end(NativeTransparencyRecording& r,VkResult result)noexcept{finish(r,result,"end");}
}
