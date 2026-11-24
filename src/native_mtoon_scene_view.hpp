#pragma once
#include "native_mtoon_scene_backing.hpp"
#include "native_mtoon_shade.hpp"
#include "native_mtoon_composite.hpp"
namespace evrm {
// Already-uploaded owned leaves, retained by the enclosing frame. Image order:
// 2D,3D,cube,2D-array. Typed placeholders make every descriptor valid; their
// contents never enable an unavailable native role or invent a lighting term.
struct NativeMToonSceneFallbacks {
    VkDescriptorBufferInfo uniform{},storage{};
    std::array<VkDescriptorImageInfo,4> images{};
};
// Immutable exact role adapter for one original world Deferred dispatch.
// Preparation alone grants no SceneReady. The parent must capture this only
// immediately after that original dispatch in its observed primary, one-time,
// unprotected graphics+compute scope, before another native command. The
// parent's same-stage source-use contract, original queue/waits, current
// resources/backing, full compute restoration and original retirement govern
// its use. Never extend these borrowed objects beyond that original command.
class NativeMToonSceneView {
public:
    explicit operator bool()const noexcept{return bool(scene_);}
    std::uint32_t generation()const noexcept{return generation_;}
    std::uint32_t family()const noexcept{return family_;}
    VkExtent2D extent()const noexcept{return scene_.metadata().raster.extent;}
    std::uint32_t buffer_mask()const noexcept{return scene_?scene_.available_buffers():0u;}
    std::uint32_t image_mask()const noexcept{return image_mask_;}
    std::uint32_t sampler_mask()const noexcept{return sampler_mask_;}
    std::uint32_t water_mask()const noexcept{return water_mask_;}
    bool current(const NativeState& state,const NativeBufferMemory& buffers,const NativeImageMemory& images)const noexcept{
        return scene_&&backing_.for_scene(scene_)&&backing_.current(state,buffers,images);
    }
    void shade_inputs(NativeMToonShadeInputs& in)const noexcept{
        in.scene_buffers=buffers_;in.scene_images=images_;in.scene_samplers=samplers_;
    }
    const std::array<NativeMToonCompositeImage,3>& composite_images()const noexcept{return outputs_;}
private:
    friend NativeMToonSceneView native_mtoon_scene_view(const NativeMToonSceneCandidate&,const NativeMToonSceneBacking&,
        std::uint32_t,std::uint32_t,const NativeMToonSceneFallbacks&,std::uint32_t,std::uint32_t)noexcept;
    NativeMToonSceneCandidate scene_;NativeMToonSceneBacking backing_;
    std::uint32_t generation_=0,family_=UINT32_MAX,image_mask_=0,sampler_mask_=0,water_mask_=0;
    std::array<VkDescriptorBufferInfo,8> buffers_{};
    std::array<VkDescriptorImageInfo,11> images_{};
    std::array<VkSampler,7> samplers_{};
    std::array<NativeMToonCompositeImage,3> outputs_{};
};
inline NativeMToonSceneView native_mtoon_scene_view(const NativeMToonSceneCandidate& scene,const NativeMToonSceneBacking& backing,
    std::uint32_t generation,std::uint32_t family,const NativeMToonSceneFallbacks& owned,
    std::uint32_t qualified_images=0x3fffu,std::uint32_t qualified_water=31u)noexcept{
    try{
        if((qualified_images&~0x3fffu)||(qualified_water&~31u)||!scene||!backing.for_scene(scene)||!generation||family>=65536||!owned.uniform.buffer||owned.uniform.range==VK_WHOLE_SIZE||owned.uniform.range<640||owned.uniform.offset%4)return {};
        for(const auto& image:owned.images)if(!image.imageView||!image.sampler||(image.imageLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&image.imageLayout!=VK_IMAGE_LAYOUT_GENERAL))return {};
        const auto& f=scene.metadata();const auto extent=f.raster.extent;
        if(!extent.width||!extent.height||extent.width>16384||extent.height>16384||std::uint64_t(extent.width)*extent.height>(1u<<24))return {};
        NativeMToonSceneView out;out.generation_=generation;out.family_=family;
        // Policy can only withdraw metadata-backed roles. The parent supplies
        // the separately qualified original initialized read domains; it is
        // never derived from placeholder pixels or descriptor presence.
        auto images=backing.backed_images()&qualified_images;auto samplers=scene.available_samplers();auto water=backing.backed_water_inputs()&qualified_water;
        const auto usable=[&](const NativeSceneImageFacts& v){if(v.sharing_mode==VK_SHARING_MODE_EXCLUSIVE)return true;
            for(unsigned n=0;n<v.queue_family_count;++n)if(v.queue_families[n]==family)return true;return false;};
        for(unsigned n=0;n<14;++n)if((images&(1u<<n))&&!usable(f.views[n]))images&=~(1u<<n);
        for(unsigned n=0;n<4;++n)if((water&(1u<<n))&&!usable(f.water.views[n]))water&=~(1u<<n);
        // A sampled image without its exact original sampler is unavailable.
        // sun-mask and water classification use unfiltered integer texel loads.
        constexpr unsigned sampler_image[]{0,1,2,3,5};
        for(unsigned n=0;n<5;++n){if(!(samplers&(1u<<n)))images&=~(1u<<sampler_image[n]);if(!(images&(1u<<sampler_image[n])))samplers&=~(1u<<n);}
        if(!(samplers&32u))water&=~1u;if(!(water&1u))samplers&=~32u;
        if(!(samplers&64u))water&=~8u;if(!(water&8u))samplers&=~64u;
        if((images&0x3fc0u)!=0x3fc0u)return {}; // four GBuffer,depth and3 destinations
        out.image_mask_=images;out.sampler_mask_=samplers;out.water_mask_=water;
        constexpr unsigned buffer_order[]{6,5,0,3,2,4,1};
        for(unsigned n=0;n<3;++n)out.buffers_[n]=f.buffers[buffer_order[n]].info;
        out.buffers_[3]=(water&16u)?f.water.parameters.info:owned.uniform;out.buffers_[3].range=640;
        for(unsigned n=4;n<8;++n){const auto role=buffer_order[n-1];
            if(scene.available_buffers()&(1u<<role))out.buffers_[n]=f.buffers[role].info;
            else {if(!f.raster.frontend||role!=1||!owned.storage.buffer||owned.storage.range<4||owned.storage.range==VK_WHOLE_SIZE||owned.storage.offset%4)return {};
                out.buffers_[n]=owned.storage;out.buffers_[n].range=4;}}
        constexpr unsigned image_order[]{10,1,2,3,4,5,0};
        constexpr unsigned fallback_order[]{0,0,1,2,0,0,0,0,0,3,0};
        for(unsigned n=0;n<7;++n)out.images_[n]=(images&(1u<<image_order[n]))?f.images[image_order[n]].info:owned.images[fallback_order[n]];
        constexpr unsigned water_order[]{1,2,0,3};
        for(unsigned n=7;n<11;++n)out.images_[n]=(water&(1u<<water_order[n-7]))?f.water.images[water_order[n-7]].info:owned.images[fallback_order[n]];
        constexpr unsigned sampler_order[]{1,2,3,4,0,5,6};
        constexpr unsigned sampler_fallback[]{0,1,2,0,0,3,0};
        for(unsigned n=0;n<7;++n)out.samplers_[n]=(samplers&(1u<<sampler_order[n]))?f.samplers[sampler_order[n]].identity.sampler:owned.images[sampler_fallback[n]].sampler;
        for(unsigned n=0;n<3;++n)out.outputs_[n]={f.views[n+11].identity.image,f.images[n+11].info,f.views[n+11].format,extent};
        out.scene_=scene;out.backing_=backing;return out;
    }catch(...){return {};}
}
}
