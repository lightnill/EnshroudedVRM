#pragma once
#include "native_mtoon_probe_contract.hpp"
#include "native_state.hpp"

namespace evrm {
// Necessary metadata only. Matching this tuple grants no source lease, actual
// image layout, initialized cells, execution, GPU content stability or readiness.
// Buffer order is parameters, state, primary/backup list, primary/backup counter.
struct NativeMToonProbeMetadata {
    NativeBoundState bound;
    NativeDescriptorSetLayoutObservation layout;
    NativeComputeRootValue root;
    NativeDispatchObservation dispatch;
    std::array<NativeBufferBinding,6> buffers;
    std::array<NativeSceneImageDescriptor,3> coefficients;
    std::array<NativeSceneImageFacts,3> coefficient_views;
};
class NativeMToonProbeCandidate {
public:
    explicit operator bool()const noexcept{return matched_;}
    const NativeMToonProbeMetadata& metadata()const noexcept{return metadata_;}
    const std::array<std::uint32_t,2>& list_capacity()const noexcept{return list_capacity_;}
private:
    friend NativeMToonProbeCandidate native_mtoon_probe_candidate(const NativeMToonProbeMetadata&)noexcept;
    NativeMToonProbeMetadata metadata_;
    std::array<std::uint32_t,2> list_capacity_{};
    bool matched_=false;
};
namespace mtoon_probe_detail {
// Observed native DSL visibility for the shared parameter UBO. The pinned
// producer accesses it only from compute; catalog use is not DSL visibility.
// The borrowed native layout retains this exact declaration unchanged.
inline constexpr VkShaderStageFlags native_parameter_stages=VK_SHADER_STAGE_VERTEX_BIT|
    VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT|VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT|
    VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_MESH_BIT_EXT|
    VK_SHADER_STAGE_RAYGEN_BIT_KHR|VK_SHADER_STAGE_ANY_HIT_BIT_KHR|VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR|
    VK_SHADER_STAGE_MISS_BIT_KHR|VK_SHADER_STAGE_INTERSECTION_BIT_KHR|VK_SHADER_STAGE_CALLABLE_BIT_KHR;
static_assert(native_parameter_stages==0x3fb7);
inline bool same(const NativeSceneImageToken& a,const NativeSceneImageToken& b)noexcept{
    return a.image==b.image&&a.view==b.view&&a.image_generation==b.image_generation&&
        a.view_generation==b.view_generation&&a.registry_serial==b.registry_serial;
}
inline bool pipeline(const NativePipelineState& p)noexcept{
    return p.pipeline&&p.layout&&p.generation&&p.set_count==1&&!p.graphics_stage_count&&!p.vertex_bytes&&!p.fragment_bytes&&
        !p.mesh_bytes&&!p.tess_control_bytes&&!p.tess_evaluation_bytes&&
        native_mtoon_probe_producer(p.compute_bytes,p.compute_digest)==NativeMToonProbeProducer::AmbientConverge;
}
inline bool domain(const NativeDispatchObservation& d)noexcept{
    if(!d)return false;
    if(d.kind==NativeDispatchKind::Direct)return d.groups[0]&&d.groups[0]<=8192&&d.groups[1]==1&&d.groups[2]==1;
    const auto& b=d.indirect_buffer;
    return d.kind==NativeDispatchKind::Indirect&&b&&!b.set&&!b.set_generation&&!b.set_version&&!b.declaration_flags&&
        !b.info.offset&&b.info.range==b.size&&(b.usage&VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)&&
        b.size>=sizeof(VkDispatchIndirectCommand)&&d.indirect_offset%4==0&&d.indirect_offset<=b.size-sizeof(VkDispatchIndirectCommand);
}
inline bool component(VkComponentSwizzle value,VkComponentSwizzle channel)noexcept{
    return value==VK_COMPONENT_SWIZZLE_IDENTITY||value==channel;
}
inline bool image(const NativeSceneImageDescriptor& d,const NativeSceneImageFacts& v)noexcept{
    constexpr auto allowed=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
    const auto extent=[](const VkExtent3D& e){return e.width==128&&e.height==16&&e.depth==16;};
    return d&&v&&same(d.image,v.identity)&&d.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE&&!d.declaration_flags&&
        d.info.imageLayout==VK_IMAGE_LAYOUT_GENERAL&&v.image_type==VK_IMAGE_TYPE_3D&&v.view_type==VK_IMAGE_VIEW_TYPE_3D&&
        v.image_format==VK_FORMAT_R16G16B16A16_SFLOAT&&v.format==VK_FORMAT_R16G16B16A16_SFLOAT&&extent(v.image_extent)&&extent(v.extent)&&
        v.image_mips==1&&v.image_layers==1&&!v.image_flags&&v.samples==VK_SAMPLE_COUNT_1_BIT&&v.tiling==VK_IMAGE_TILING_OPTIMAL&&
        v.sharing_mode==VK_SHARING_MODE_EXCLUSIVE&&!v.queue_family_count&&!v.format_count&&
        (v.image_usage&VK_IMAGE_USAGE_STORAGE_BIT)&&!(v.image_usage&~allowed)&&(v.view_usage&VK_IMAGE_USAGE_STORAGE_BIT)&&!(v.view_usage&~v.image_usage)&&
        v.range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&!v.range.baseMipLevel&&v.range.levelCount==1&&!v.range.baseArrayLayer&&v.range.layerCount==1&&
        component(v.components.r,VK_COMPONENT_SWIZZLE_R)&&component(v.components.g,VK_COMPONENT_SWIZZLE_G)&&
        component(v.components.b,VK_COMPONENT_SWIZZLE_B)&&component(v.components.a,VK_COMPONENT_SWIZZLE_A);
}
inline bool match(const NativeMToonProbeMetadata& f)noexcept{
    const auto& b=f.bound;const auto& l=f.layout;
    if(!b.valid||!pipeline(b.pipeline)||b.sets.size()!=1||b.set_generations.size()!=1||b.set_versions.size()!=1||!l||
        l.set!=b.sets[0]||l.set_generation!=b.set_generations[0]||l.set_version!=b.set_versions[0]||l.bindings.size()!=10||
        !f.root.valid||f.root.layout!=b.pipeline.layout||!f.root.layout_generation||f.root.value>1||!domain(f.dispatch))return false;
    std::array<bool,10> seen{};
    for(const auto& d:l.bindings){
        const bool stages=d.stages==VK_SHADER_STAGE_COMPUTE_BIT||(d.binding==0&&d.stages==native_parameter_stages);
        if(d.binding>=seen.size()||seen[d.binding]||d.count!=1||!stages||d.flags)return false;
        const auto type=d.binding==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:d.binding==1?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            d.binding>=3&&d.binding<=5?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if(d.type!=type)return false;seen[d.binding]=true;
    }
    constexpr std::array<std::uint32_t,6> bindings{0,2,6,7,8,9};
    for(std::size_t i=0;i<f.buffers.size();++i){const auto& r=f.buffers[i];
        if(!r||r.set!=l.set||r.set_generation!=l.set_generation||r.set_version!=l.set_version||r.binding!=bindings[i]||
            r.declaration_flags||r.info.offset%4||!(r.usage&(i?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)))return false;
        if(i==0&&r.info.range!=480)return false;
        if(i==1&&r.info.range!=131072)return false;
        if((i==2||i==3)&&(!r.info.range||r.info.range>131072||r.info.range%4))return false;
        if(i>=4&&r.info.range!=4)return false;
    }
    for(std::size_t i=0;i<f.coefficients.size();++i){const auto& d=f.coefficients[i];
        if(!image(d,f.coefficient_views[i])||d.set!=l.set||d.set_generation!=l.set_generation||d.set_version!=l.set_version||d.binding!=3+i)return false;
        for(std::size_t j=0;j<i;++j)if(d.image.registry_serial!=f.coefficients[j].image.registry_serial||
            d.image.image==f.coefficients[j].image.image||d.image.view==f.coefficients[j].image.view)return false;
    }
    return true;
}
inline bool same(const NativeBufferBinding& a,const NativeBufferBinding& b)noexcept{
    return a.info.buffer==b.info.buffer&&a.info.offset==b.info.offset&&a.info.range==b.info.range&&a.size==b.size&&a.usage==b.usage&&
        a.generation==b.generation&&a.set_generation==b.set_generation&&a.set_version==b.set_version&&a.set==b.set&&
        a.binding==b.binding&&a.declaration_flags==b.declaration_flags;
}
}
inline NativeMToonProbeCandidate native_mtoon_probe_candidate(const NativeMToonProbeMetadata& f)noexcept{
    try{
        if(!mtoon_probe_detail::match(f))return {};
        NativeMToonProbeCandidate c;c.metadata_=f;c.list_capacity_={std::uint32_t(f.buffers[2].info.range/4),std::uint32_t(f.buffers[3].info.range/4)};
        c.matched_=true;return c;
    }catch(...){return {};}
}
// Caller holds the native source guard and invokes this in the immediate
// forwarded-dispatch callback. It does not establish those placement/guard facts.
inline NativeMToonProbeCandidate native_mtoon_observe_probe_candidate(const NativeState& state,VkCommandBuffer command)noexcept{
    try{
        NativeMToonProbeMetadata f;f.bound=state.observed_bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);
        if(!f.bound.valid||!mtoon_probe_detail::pipeline(f.bound.pipeline))return {};
        f.layout=state.observed_set_layout(f.bound,0);f.root=state.observed_compute_root(command,f.bound.pipeline.layout);f.dispatch=state.observed_dispatch(command);
        constexpr std::array<std::uint32_t,6> bindings{0,2,6,7,8,9};
        for(std::size_t i=0;i<bindings.size();++i)f.buffers[i]=state.observed_buffer_binding(f.bound,0,bindings[i]);
        for(std::size_t i=0;i<3;++i){f.coefficients[i]=state.observed_scene_image_descriptor(f.bound,0,std::uint32_t(3+i));
            f.coefficient_views[i]=state.scene_image_view(f.coefficients[i].image);}
        return native_mtoon_probe_candidate(f);
    }catch(...){return {};}
}
// Revalidation is a host metadata comparison, not an execution/source permit.
// It cannot establish indirect argument byte stability across GPU queues.
inline bool native_mtoon_probe_candidate_current(const NativeMToonProbeCandidate& prior,const NativeState& state,VkCommandBuffer command)noexcept{
    if(!prior)return false;const auto next=native_mtoon_observe_probe_candidate(state,command);if(!next)return false;
    const auto& a=prior.metadata();const auto& b=next.metadata();
    if(a.bound.pipeline.pipeline!=b.bound.pipeline.pipeline||a.bound.pipeline.generation!=b.bound.pipeline.generation||
        a.bound.pipeline.layout!=b.bound.pipeline.layout||a.layout.layout!=b.layout.layout||a.layout.generation!=b.layout.generation||
        a.layout.set!=b.layout.set||a.layout.set_generation!=b.layout.set_generation||a.layout.set_version!=b.layout.set_version||
        a.root.layout_generation!=b.root.layout_generation||a.root.value!=b.root.value||a.dispatch.serial!=b.dispatch.serial||
        a.dispatch.kind!=b.dispatch.kind||a.dispatch.groups!=b.dispatch.groups||a.dispatch.indirect_offset!=b.dispatch.indirect_offset||
        !mtoon_probe_detail::same(a.dispatch.indirect_buffer,b.dispatch.indirect_buffer)||prior.list_capacity()!=next.list_capacity())return false;
    for(std::size_t i=0;i<a.buffers.size();++i)if(!mtoon_probe_detail::same(a.buffers[i],b.buffers[i]))return false;
    for(std::size_t i=0;i<3;++i)if(!mtoon_probe_detail::same(a.coefficients[i].image,b.coefficients[i].image))return false;
    return true;
}
}
