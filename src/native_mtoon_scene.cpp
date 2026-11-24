#include "native_mtoon_scene.hpp"
#include <tuple>
namespace evrm {namespace {
constexpr std::array<unsigned,7> sampler_sets{1,2,2,2,2,2,2},sampler_bindings{3,40,42,47,49,43,50};
constexpr std::array<unsigned,7> buffer_sets{1,1,2,2,2,2,2},buffer_bindings{0,2,15,16,17,22,24};
constexpr std::array<unsigned,14> image_sets{1,2,2,2,2,2,2,2,2,2,2,2,2,2},image_bindings{1,0,12,27,29,30,4,5,6,7,33,34,35,36};
constexpr std::array<VkFormat,8> core_formats{VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R8G8B8A8_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_D32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16_SFLOAT,VK_FORMAT_R8_UNORM};
bool same(const NativeSceneImageToken& a,const NativeSceneImageToken& b){return a.image==b.image&&a.view==b.view&&a.image_generation==b.image_generation&&a.view_generation==b.view_generation&&a.registry_serial==b.registry_serial;}
bool same_image(const NativeSceneImageToken& a,const NativeSceneImageToken& b){return a&&b&&a.image==b.image&&a.image_generation==b.image_generation&&a.registry_serial==b.registry_serial;}
bool declaration(const NativeMToonSceneMetadata& f,unsigned set,unsigned binding,VkDescriptorType type,VkDescriptorBindingFlags flags){
    if(set<1||set>2||set>=f.bound.sets.size()||(flags&~VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT))return false;
    const auto& l=f.layouts[set-1];if(!l||l.set!=f.bound.sets[set]||l.set_generation!=f.bound.set_generations[set]||l.set_version!=f.bound.set_versions[set])return false;
    unsigned matches=0;for(const auto& d:l.bindings)if(d.binding==binding){if(d.type!=type||d.count!=1||d.flags!=flags||!(d.stages&VK_SHADER_STAGE_COMPUTE_BIT))return false;++matches;}return matches==1;
}
bool buffer(const NativeMToonSceneMetadata& f,unsigned n){const auto& b=f.buffers[n];const auto set=buffer_sets[n],binding=buffer_bindings[n];
    const bool uniform=n==0||n>=5;const auto type=uniform?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    if(!b||b.set!=f.bound.sets[set]||b.binding!=binding||b.set_generation!=f.bound.set_generations[set]||b.set_version!=f.bound.set_versions[set]||
       !declaration(f,set,binding,type,b.declaration_flags)||b.info.offset%4||b.info.range%4||!(b.usage&(uniform?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)))return false;
    if(uniform)return !b.declaration_flags&&b.info.range==(n==0?96u:n==5?64u:2048u);
    // The pinned native consumer reads2048 uint2 entries (16KiB).
    // Higher quality may pad the descriptor to128KiB; retain the full
    // descriptor's existing backing/generation/alignment/disjointness checks.
    if(n==1)return b.info.range>=16384u&&b.info.range<=131072u;
    if(n==4)return b.info.range>=52u&&b.info.range<=65536u*52u+256u;
    return b.info.range>=4u&&b.info.range<=64u*1024u*1024u;
}
bool sharing(const NativeSceneImageFacts& v,bool allowConcurrent){
    if(v.sharing_mode==VK_SHARING_MODE_EXCLUSIVE)return !v.queue_family_count;
    if(!allowConcurrent||v.sharing_mode!=VK_SHARING_MODE_CONCURRENT||v.queue_family_count<2||v.queue_family_count>v.queue_families.size())return false;
    for(unsigned n=0;n<v.queue_family_count;++n){if(v.queue_families[n]>=65536)return false;
        for(unsigned previous=0;previous<n;++previous)if(v.queue_families[previous]==v.queue_families[n])return false;}
    return true;
}
bool plain(const NativeSceneImageFacts& v,bool allowConcurrent=false){
    const auto component=[](VkComponentSwizzle s,VkComponentSwizzle channel){return s==VK_COMPONENT_SWIZZLE_IDENTITY||s==channel;};
    return v&&v.samples==VK_SAMPLE_COUNT_1_BIT&&v.tiling==VK_IMAGE_TILING_OPTIMAL&&sharing(v,allowConcurrent)&&
        !(v.image_flags&~(VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT|VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT))&&
        v.extent.width&&v.extent.height&&v.extent.depth&&v.extent.width<=32768&&v.extent.height<=32768&&v.extent.depth<=32768&&
        v.image_mips&&v.image_layers&&!v.range.baseMipLevel&&v.range.levelCount&&v.range.levelCount<=v.image_mips&&!v.range.baseArrayLayer&&
        v.range.layerCount&&v.range.layerCount<=v.image_layers&&!(v.view_usage&~v.image_usage)&&
        component(v.components.r,VK_COMPONENT_SWIZZLE_R)&&component(v.components.g,VK_COMPONENT_SWIZZLE_G)&&component(v.components.b,VK_COMPONENT_SWIZZLE_B)&&component(v.components.a,VK_COMPONENT_SWIZZLE_A);
}
bool image(const NativeMToonSceneMetadata& f,unsigned n){const auto& d=f.images[n];const auto& v=f.views[n];const auto set=image_sets[n],binding=image_bindings[n];
    const bool storage=n>=11;const auto type=storage?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    if(!d||!plain(v,n==3)||!same(d.image,v.identity)||d.set!=f.bound.sets[set]||d.binding!=binding||d.set_generation!=f.bound.set_generations[set]||d.set_version!=f.bound.set_versions[set]||
       d.type!=type||!declaration(f,set,binding,type,d.declaration_flags)||!(v.view_usage&(storage?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT))||
       (storage?d.info.imageLayout!=VK_IMAGE_LAYOUT_GENERAL:(d.info.imageLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&d.info.imageLayout!=VK_IMAGE_LAYOUT_GENERAL)))return false;
    if(n>=6){const auto& extent=f.raster.extent;
        if(v.image_type!=VK_IMAGE_TYPE_2D||v.view_type!=VK_IMAGE_VIEW_TYPE_2D||v.extent.width!=extent.width||v.extent.height!=extent.height||v.extent.depth!=1||
           v.image_extent.width!=extent.width||v.image_extent.height!=extent.height||v.image_extent.depth!=1||v.image_mips!=1||v.image_layers!=1||v.range.levelCount!=1||v.range.layerCount!=1||
           v.range.aspectMask!=VkImageAspectFlags(n==10?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT)||v.format!=core_formats[n-6])return false;
        if(n<=10&&!same_image(v.identity,f.raster.attachments[n-6].identity))return false;
        return true;
    }
    if(v.range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT)return false;
    if(n==2)return v.image_type==VK_IMAGE_TYPE_3D&&v.view_type==VK_IMAGE_VIEW_TYPE_3D&&v.image_layers==1&&v.range.layerCount==1&&v.range.levelCount==1&&
        (v.format==VK_FORMAT_R16_UNORM||v.format==VK_FORMAT_R16_SFLOAT||v.format==VK_FORMAT_R16G16B16A16_SFLOAT||v.format==VK_FORMAT_R8_UNORM);
    if(n==3)return v.image_type==VK_IMAGE_TYPE_2D&&v.view_type==VK_IMAGE_VIEW_TYPE_CUBE&&v.image_layers==6&&v.range.layerCount==6&&v.range.levelCount==1&&v.extent.width==v.extent.height&&
        (v.image_flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)&&(v.format==VK_FORMAT_BC6H_UFLOAT_BLOCK||v.format==VK_FORMAT_BC6H_SFLOAT_BLOCK||v.format==VK_FORMAT_R16G16B16A16_SFLOAT);
    return v.image_type==VK_IMAGE_TYPE_2D&&v.view_type==VK_IMAGE_VIEW_TYPE_2D&&v.extent.depth==1&&v.image_layers==1&&v.range.layerCount==1&&v.range.levelCount==1&&
        v.format==(n>=4?VK_FORMAT_R8_UNORM:VK_FORMAT_R16G16B16A16_SFLOAT)&&
        (n<4||(v.extent.width==f.raster.extent.width&&v.extent.height==f.raster.extent.height));
}
bool water_parameter(const NativeMToonSceneMetadata& f){const auto& b=f.water.parameters;
    return b&&b.set==f.bound.sets[2]&&b.binding==13&&b.set_generation==f.bound.set_generations[2]&&b.set_version==f.bound.set_versions[2]&&
        !b.declaration_flags&&declaration(f,2,13,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0)&&b.info.offset%4==0&&b.info.range==640&&
        (b.usage&VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}
bool water_image(const NativeMToonSceneMetadata& f,unsigned n){
    constexpr std::array<unsigned,4> bindings{14,37,38,39};const auto& d=f.water.images[n];const auto& v=f.water.views[n];
    if(!d||!plain(v,n==3)||!same(d.image,v.identity)||d.set!=f.bound.sets[2]||d.binding!=bindings[n]||
       d.set_generation!=f.bound.set_generations[2]||d.set_version!=f.bound.set_versions[2]||d.type!=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||
       !declaration(f,2,d.binding,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,d.declaration_flags)||!(v.view_usage&VK_IMAGE_USAGE_SAMPLED_BIT)||
       (d.info.imageLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&d.info.imageLayout!=VK_IMAGE_LAYOUT_GENERAL)||
       v.image_type!=VK_IMAGE_TYPE_2D||(v.image_flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)||v.extent.depth!=1||v.image_extent.depth!=1||
       v.extent.width!=v.image_extent.width||v.extent.height!=v.image_extent.height)return false;
    // A later composition writes these destinations. Water read roles cannot
    // name one of them, including through a different view of the same image.
    for(unsigned output=11;output<14;++output)if(same_image(v.identity,f.views[output].identity))return false;
    // Raw scalar reads accept either a colour scalar or the depth component
    // of an explicit depth format. Stencil/combined/incorrect aspects remain
    // unavailable; surface classification and caustics are colour-only.
    const bool scalar=v.range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&(v.format==VK_FORMAT_R16_UNORM||v.format==VK_FORMAT_R16_SFLOAT||v.format==VK_FORMAT_R32_SFLOAT);
    const bool depth=v.range.aspectMask==VK_IMAGE_ASPECT_DEPTH_BIT&&(v.format==VK_FORMAT_D16_UNORM||v.format==VK_FORMAT_X8_D24_UNORM_PACK32||
        v.format==VK_FORMAT_D32_SFLOAT||v.format==VK_FORMAT_D16_UNORM_S8_UINT||v.format==VK_FORMAT_D24_UNORM_S8_UINT||v.format==VK_FORMAT_D32_SFLOAT_S8_UINT);
    if(n==0)return (scalar||depth)&&v.view_type==VK_IMAGE_VIEW_TYPE_2D_ARRAY&&v.image_layers>=9&&v.image_layers<=16&&v.range.layerCount>=9&&v.range.levelCount==1;
    if(v.view_type!=VK_IMAGE_VIEW_TYPE_2D||v.image_layers!=1||v.range.layerCount!=1)return false;
    if(n==2)return (scalar||depth)&&v.image_mips==1&&v.range.levelCount==1;
    if(v.range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT)return false;
    const bool rgba=v.format==VK_FORMAT_R8G8B8A8_UNORM||v.format==VK_FORMAT_R16G16B16A16_SFLOAT;
    if(n==1)return (rgba||v.format==VK_FORMAT_A2B10G10R10_UNORM_PACK32)&&v.image_mips==1&&v.range.levelCount==1;
    return (rgba||v.format==VK_FORMAT_R8G8B8A8_SRGB||v.format==VK_FORMAT_B10G11R11_UFLOAT_PACK32)&&v.range.levelCount==v.image_mips;
}
std::uint32_t sampler_available(const NativeMToonSceneMetadata& f){
    std::uint32_t mask=0;
    for(unsigned n=0;n<7;++n){const auto& s=f.samplers[n];const auto set=sampler_sets[n],binding=sampler_bindings[n];
        if(s&&s.set==f.bound.sets[set]&&s.binding==binding&&s.set_generation==f.bound.set_generations[set]&&s.set_version==f.bound.set_versions[set]&&
           declaration(f,set,binding,VK_DESCRIPTOR_TYPE_SAMPLER,s.declaration_flags))mask|=1u<<n;
    }
    return mask;
}
std::uint32_t water_available(const NativeMToonSceneMetadata& f){
    std::uint32_t mask=water_parameter(f)?16u:0u;
    for(unsigned n=0;n<4;++n)if(water_image(f,n))mask|=1u<<n;
    if((mask&6u)==6u){const auto& surface=f.water.views[1].extent;const auto& depth=f.water.views[2].extent;
        if(surface.width!=depth.width||surface.height!=depth.height)mask&=~6u;}
    return mask;
}
}
NativeMToonSceneCandidate native_mtoon_scene_candidate(const NativeMToonSceneMetadata& f)noexcept{
    try{const auto& b=f.bound;const auto& raster=f.raster;const auto extent=raster.extent;
        if(!f.command||f.command!=raster.command||!raster.completed||(raster.preview!=raster.frontend)||!extent.width||!extent.height||extent.width>32768||extent.height>32768||
           !b.valid||b.sets.size()<3||b.sets.size()>8||b.set_generations.size()!=b.sets.size()||b.set_versions.size()!=b.sets.size()||b.pipeline.set_count!=b.sets.size()||
           native_mtoon_probe_stage(b.pipeline)!=NativeMToonProbeStage::deferred||
           (raster.frontend?bool(f.probes):(!f.probes||!f.probes->complete()||f.probes->command()!=f.command))||
           !f.dispatch||f.dispatch.kind!=NativeDispatchKind::Direct||f.dispatch.groups!=std::array<std::uint32_t,3>{(extent.width+7u)/8u,(extent.height+7u)/8u,1}||
           (!raster.frontend&&f.dispatch.serial<=f.probes->update().dispatch.serial))return {};
        for(const auto& a:raster.attachments)if(!plain(a))return {};
        std::uint32_t buffers=0;for(unsigned n=0;n<7;++n){if(buffer(f,n))buffers|=1u<<n;else if(!raster.frontend||n!=1)return {};}
        std::uint32_t available=0;for(unsigned n=0;n<14;++n){const bool valid=image(f,n);if(!valid&&n>=6)return {};if(valid)available|=1u<<n;}
        for(unsigned n=6;n<14;++n)for(unsigned previous=6;previous<n;++previous)if(same_image(f.views[n].identity,f.views[previous].identity))return {};
        NativeMToonSceneCandidate out;out.data_=std::make_shared<const NativeMToonSceneMetadata>(f);out.available_=available;out.buffers_available_=buffers;out.water_available_=water_available(f);out.samplers_available_=sampler_available(f);return out;
    }catch(...){return {};}
}
const NativeMToonSceneMetadata& NativeMToonSceneCandidate::metadata()const noexcept{static const NativeMToonSceneMetadata empty;return data_?*data_:empty;}
bool NativeMToonSceneCandidate::current(const NativeState& state)const noexcept{
    try{if(!data_||(!data_->raster.frontend&&(!data_->probes||!data_->probes->current(state)))||(data_->raster.frontend&&data_->probes))return false;const auto& f=*data_;
        for(unsigned n=0;n<2;++n){const auto now=state.observed_set_layout(f.bound,n+1);const auto& before=f.layouts[n];if(!now||now.layout!=before.layout||now.generation!=before.generation||now.set!=before.set||now.set_generation!=before.set_generation||now.set_version!=before.set_version)return false;}
        for(unsigned n=0;n<7;++n)if((buffers_available_&(1u<<n))&&!state.live(f.buffers[n]))return false;
        for(unsigned n=0;n<14;++n)if((available_&(1u<<n))&&!state.scene_image_descriptor_current(f.images[n]))return false;
        for(const auto& a:f.raster.attachments)if(!state.scene_image_metadata_current(a.identity))return false;
        for(unsigned n=0;n<7;++n)if((samplers_available_&(1u<<n))&&!state.sampler_descriptor_current(f.samplers[n]))return false;
        if((water_available_&16u)&&!state.live(f.water.parameters))return false;
        for(unsigned n=0;n<4;++n)if((water_available_&(1u<<n))&&!state.scene_image_descriptor_current(f.water.images[n]))return false;
        return true;
    }catch(...){return false;}
}
NativeMToonSceneCandidate native_mtoon_observe_scene(const NativeState& state,VkCommandBuffer command,
    const NativeMToonRasterView& raster,std::shared_ptr<const NativeMToonProbeGraph> probes)noexcept{
    try{NativeMToonSceneMetadata f;f.command=command;f.raster=raster;f.probes=std::move(probes);f.bound=state.observed_bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);
        f.dispatch=state.observed_dispatch(command);for(unsigned n=0;n<2;++n)f.layouts[n]=state.observed_set_layout(f.bound,n+1);
        for(unsigned n=0;n<7;++n)f.buffers[n]=state.observed_buffer_binding(f.bound,buffer_sets[n],buffer_bindings[n]);
        for(unsigned n=0;n<14;++n){f.images[n]=state.observed_scene_image_descriptor(f.bound,image_sets[n],image_bindings[n]);f.views[n]=state.scene_image_view(f.images[n].image);}
        f.water.parameters=state.observed_buffer_binding(f.bound,2,13);
        constexpr std::array<unsigned,4> waterBindings{14,37,38,39};
        for(unsigned n=0;n<4;++n){f.water.images[n]=state.observed_scene_image_descriptor(f.bound,2,waterBindings[n]);f.water.views[n]=state.scene_image_view(f.water.images[n].image);}
        for(unsigned n=0;n<7;++n)f.samplers[n]=state.observed_sampler_descriptor(f.bound,sampler_sets[n],sampler_bindings[n]);
        return native_mtoon_scene_candidate(f);
    }catch(...){return {};}
}
} // namespace evrm
