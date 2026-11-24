#pragma once
#include "native_mtoon_scene.hpp"
#include "native_buffer_memory.hpp"
#include "native_image_memory.hpp"
#include <limits>
namespace evrm {
namespace mtoon_scene_backing_detail {
template<class A,class B>bool disjoint(const A& a,const B& b)noexcept{
    if(!a||!b||a.offset>(std::numeric_limits<VkDeviceSize>::max)()-a.bytes||b.offset>(std::numeric_limits<VkDeviceSize>::max)()-b.bytes)return false;
    if(a.memory!=b.memory)return true;
    if(a.memory_generation!=b.memory_generation)return false;
    return a.offset+a.bytes<=b.offset||b.offset+b.bytes<=a.offset;
}
}
// Caller holds the native backend source guard during capture/current checks.
// Fully observed inactive image aliases may share allocations. This tuple
// rejects physical overlap among distinct active image objects, every input
// versus every output, and all output pairs. Legacy global exclusivity and
// strict3D GI backing APIs are unchanged. These are metadata facts only; the
// immediate original Deferred use is still required by the scene-view parent.
// Necessary metadata/physical-backing group only. No GPU initialization/use,
// layout, queue ownership, producer coverage, original-retirement lease or
// SceneReady/write permission is established here. Keep this exact immutable
// tuple through the separately admitted original recording and execution.
struct NativeMToonSceneBackingDiagnostic {unsigned code=0,role=UINT32_MAX;};
class NativeMToonSceneBacking {
public:
    explicit operator bool()const noexcept{return bool(scene_);}
    bool for_scene(const NativeMToonSceneCandidate& scene)const noexcept{return scene_&&scene&&&scene_.metadata()==&scene.metadata();}
    std::uint32_t backed_images()const noexcept{return images_mask_;}
    std::uint32_t backed_water_inputs()const noexcept{return water_mask_;}
    bool backing_current(const NativeBufferMemory& buffers,const NativeImageMemory& images)const noexcept{
        if(!scene_||buffers_!=&buffers||images_!=&images||(!scene_.metadata().raster.frontend&&(!scene_.metadata().probes||!scene_.metadata().probes->complete()))||(scene_.metadata().raster.frontend&&scene_.metadata().probes))return false;
        const auto& f=scene_.metadata();
        for(unsigned n=0;n<7;++n)if((scene_.available_buffers()&(1u<<n))&&(!buffers.valid(f.buffers[n].info.buffer,f.buffers[n].info.offset,f.buffers[n].info.range)||!images.current(buffer_tokens_[n])))return false;
        if(water_mask_&16u){const auto& b=f.water.parameters;if(!buffers.valid(b.info.buffer,b.info.offset,b.info.range)||!images.current(buffer_tokens_[7]))return false;}
        for(unsigned n=0;n<14;++n)if((images_mask_&(1u<<n))&&!images.current(image_tokens_[n]))return false;
        for(unsigned n=0;n<4;++n)if((water_mask_&(1u<<n))&&!images.current(water_tokens_[n]))return false;
        return true;
    }
    bool current(const NativeState& state,const NativeBufferMemory& buffers,const NativeImageMemory& images)const noexcept{
        return scene_&&scene_.current(state)&&backing_current(buffers,images);
    }
private:
    friend NativeMToonSceneBacking native_mtoon_scene_backing(const NativeMToonSceneCandidate&,const NativeBufferMemory&,const NativeImageMemory&,NativeMToonSceneBackingDiagnostic*)noexcept;
    NativeMToonSceneCandidate scene_;
    const NativeBufferMemory* buffers_=nullptr;const NativeImageMemory* images_=nullptr;
    std::array<NativeBufferBacking,8> buffer_tokens_;
    std::array<NativeSceneImageBinding,14> image_tokens_;
    std::array<NativeSceneImageBinding,4> water_tokens_;
    std::uint32_t images_mask_=0,water_mask_=0;
};
inline NativeMToonSceneBacking native_mtoon_scene_backing(const NativeMToonSceneCandidate& scene,
    const NativeBufferMemory& buffers,const NativeImageMemory& images,NativeMToonSceneBackingDiagnostic* diagnostic=nullptr)noexcept{
    if(diagnostic)*diagnostic={};
    const auto decline=[&](unsigned code,unsigned role){if(diagnostic)*diagnostic={code,role};return NativeMToonSceneBacking{};};
    try{if(!scene)return decline(1,UINT32_MAX);const auto& f=scene.metadata();NativeMToonSceneBacking out;
        // Three original native destinations are mandatory and pairwise
        // disjoint even if different image objects share an allocation.
        for(unsigned n=11;n<14;++n){if(!images.scene_image_binding(f.views[n].identity.image,out.image_tokens_[n]))return decline(2,n);
            for(unsigned previous=11;previous<n;++previous)if(!mtoon_scene_backing_detail::disjoint(out.image_tokens_[n],out.image_tokens_[previous]))return decline(3,n);out.images_mask_|=1u<<n;}
        const auto output_disjoint=[&](const auto& input){for(unsigned n=11;n<14;++n)if(!mtoon_scene_backing_detail::disjoint(input,out.image_tokens_[n]))return false;return true;};
        for(unsigned n=0;n<7;++n){if(!(scene.available_buffers()&(1u<<n)))continue;const auto& b=f.buffers[n];
            if(!buffers.valid(b.info.buffer,b.info.offset,b.info.range))return decline(4,n);
            if(!images.exclusive_buffer(b.info.buffer,b.info.offset,b.info.range,out.buffer_tokens_[n]))return decline(5,n);
            if(!output_disjoint(out.buffer_tokens_[n]))return decline(6,n);}
        const auto input_separate=[&](const NativeSceneImageBinding& input){
            for(unsigned n=0;n<11;++n)if((out.images_mask_&(1u<<n))&&input.image!=out.image_tokens_[n].image&&!mtoon_scene_backing_detail::disjoint(input,out.image_tokens_[n]))return false;
            for(unsigned n=0;n<4;++n)if((out.water_mask_&(1u<<n))&&input.image!=out.water_tokens_[n].image&&!mtoon_scene_backing_detail::disjoint(input,out.water_tokens_[n]))return false;
            return true;
        };
        // Required GBuffer/depth reads take priority over optional lighting
        // maps. No new read is admitted through a distinct overlapping alias.
        for(unsigned n=6;n<11;++n){NativeSceneImageBinding token;
            if(!(scene.available_images()&(1u<<n))||!images.scene_image_binding(f.views[n].identity.image,token)||!output_disjoint(token))return decline(7,n);
            if(!input_separate(token))return decline(9,n);out.image_tokens_[n]=token;out.images_mask_|=1u<<n;
        }
        for(unsigned n=0;n<6;++n){if(!(scene.available_images()&(1u<<n)))continue;NativeSceneImageBinding token;
            if(!images.scene_image_binding(f.views[n].identity.image,token)||!output_disjoint(token)||!input_separate(token))continue;
            out.image_tokens_[n]=token;out.images_mask_|=1u<<n;
        }
        const auto& water=f.water;const auto available=scene.available_water_inputs();
        if(available&16u){const auto& b=water.parameters;
            if(buffers.valid(b.info.buffer,b.info.offset,b.info.range)&&images.exclusive_buffer(b.info.buffer,b.info.offset,b.info.range,out.buffer_tokens_[7])&&output_disjoint(out.buffer_tokens_[7]))out.water_mask_|=16u;}
        for(unsigned n=0;n<4;++n)if(available&(1u<<n)){NativeSceneImageBinding token;
            if(images.scene_image_binding(water.views[n].identity.image,token)&&output_disjoint(token)&&input_separate(token)){out.water_tokens_[n]=token;out.water_mask_|=1u<<n;}}
        out.scene_=scene;out.buffers_=&buffers;out.images_=&images;return out;
    }catch(...){return decline(8,UINT32_MAX);}
}
} // namespace evrm
