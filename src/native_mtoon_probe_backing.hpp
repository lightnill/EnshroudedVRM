#pragma once
#include "native_mtoon_probe_source.hpp"
#include "native_buffer_memory.hpp"
#include "native_image_memory.hpp"

namespace evrm {
// Necessary host backing facts only. Caller separately revalidates the candidate
// against NativeState under the source guard, including buffer generations.
// No source lifetime lease, image layout, execution or GI readiness is granted.
class NativeMToonProbeBacking {
public:
    explicit operator bool()const noexcept{return bool(candidate_);}
private:
    friend NativeMToonProbeBacking native_mtoon_probe_backing(const NativeMToonProbeCandidate&,
        const NativeBufferMemory&,const NativeImageMemory&)noexcept;
    friend bool native_mtoon_probe_backing_current(const NativeMToonProbeBacking&,
        const NativeBufferMemory&,const NativeImageMemory&)noexcept;
    NativeMToonProbeCandidate candidate_;
    const NativeBufferMemory* buffers_=nullptr;
    const NativeImageMemory* images_=nullptr;
    std::array<NativeImageBacking,3> coefficients_{};
};
namespace mtoon_probe_backing_detail {
inline bool buffers_current(const NativeMToonProbeCandidate& candidate,const NativeBufferMemory& buffers)noexcept{
    if(!candidate)return false;
    const auto& facts=candidate.metadata();
    for(const auto& b:facts.buffers)if(!buffers.valid(b.info.buffer,b.info.offset,b.info.range))return false;
    return facts.dispatch.kind!=NativeDispatchKind::Indirect||
        buffers.valid(facts.dispatch.indirect_buffer.info.buffer,facts.dispatch.indirect_offset,sizeof(VkDispatchIndirectCommand));
}
}
inline NativeMToonProbeBacking native_mtoon_probe_backing(const NativeMToonProbeCandidate& candidate,
    const NativeBufferMemory& buffers,const NativeImageMemory& images)noexcept{
    try{
        if(!mtoon_probe_backing_detail::buffers_current(candidate,buffers))return {};
        NativeMToonProbeBacking result;
        for(std::size_t i=0;i<result.coefficients_.size();++i)
            if(!images.exclusive(candidate.metadata().coefficients[i].image.image,result.coefficients_[i]))return {};
        result.candidate_=candidate;result.buffers_=&buffers;result.images_=&images;
        return result;
    }catch(...){return {};}
}
// Retain registry identities as well as image allocation generations/revisions.
// Removal and recreation of a conflicting binding must not refresh an old token.
inline bool native_mtoon_probe_backing_current(const NativeMToonProbeBacking& prior,
    const NativeBufferMemory& buffers,const NativeImageMemory& images)noexcept{
    if(!prior||prior.buffers_!=&buffers||prior.images_!=&images||
        !mtoon_probe_backing_detail::buffers_current(prior.candidate_,buffers))return false;
    for(const auto& image:prior.coefficients_)if(!images.current(image))return false;
    return true;
}
}
