#pragma once
#include "native_mtoon_probe_graph.hpp"
#include "native_mtoon_water.hpp"
#define EVRM_NATIVE_MTOON_SCENE_SAMPLERS 1
#define EVRM_NATIVE_MTOON_FRONTEND_SCENE 1
namespace evrm {
struct NativeMToonRasterView {
    VkCommandBuffer command{};VkExtent2D extent{};
    // Native GBuffer0..3 and depth, captured at the matched raster pass.
    std::array<NativeSceneImageFacts,5> attachments;
    // frontend requires the independently captured frontend menu owner, never an in-world preview.
    bool completed=false,preview=false,frontend=false;
};
struct NativeMToonSceneMetadata {
    VkCommandBuffer command{};NativeBoundState bound;NativeDispatchObservation dispatch;
    std::array<NativeDescriptorSetLayoutObservation,2> layouts; // native sets1,2
    // ManyLights parameters/map; visible bits, cells, records; cluster/main UBOs.
    std::array<NativeBufferBinding,7> buffers;
    // Shadow atlas, transmittance, fog shadow, diffuse IBL, sun mask, SSAO;
    // GBuffer0..3, depth, HDR, brightness, emission mask.
    std::array<NativeSceneImageDescriptor,14> images;
    std::array<NativeSceneImageFacts,14> views;
    NativeMToonRasterView raster;
    NativeMToonWaterMetadata water;
    // Atlas, atmosphere, fog3D, diffuse IBL, SSAO, water shadow, caustics.
    // Independent optional metadata; neither sampled pixels nor source leases.
    std::array<NativeSamplerDescriptor,7> samplers;
    std::shared_ptr<const NativeMToonProbeGraph> probes;
};
// Necessary metadata tuple only. Never grants SceneReady, descriptor/source
// lifetime, actual layouts, GPU coverage or a destination-write permit. Optional
// inputs have explicit availability bits, not invented valid defaults; the GPU
// consumer must require its flags/branches' inputs before using those sources.
class NativeMToonSceneCandidate {
public:
    explicit operator bool()const noexcept{return bool(data_);}
    const NativeMToonSceneMetadata& metadata()const noexcept;
    std::uint32_t available_images()const noexcept{return available_;}
    std::uint32_t available_buffers()const noexcept{return buffers_available_;}
    std::uint32_t available_samplers()const noexcept{return samplers_available_;}
    // Image bits0..3 and640-byte parameter bit4; optional metadata only.
    std::uint32_t available_water_inputs()const noexcept{return water_available_;}
    bool current(const NativeState&)const noexcept;
private:
    friend NativeMToonSceneCandidate native_mtoon_scene_candidate(const NativeMToonSceneMetadata&)noexcept;
    std::shared_ptr<const NativeMToonSceneMetadata> data_;std::uint32_t available_=0,water_available_=0,samplers_available_=0,buffers_available_=0;
};
NativeMToonSceneCandidate native_mtoon_scene_candidate(const NativeMToonSceneMetadata&)noexcept;
NativeMToonSceneCandidate native_mtoon_observe_scene(const NativeState&,VkCommandBuffer,
    const NativeMToonRasterView&,std::shared_ptr<const NativeMToonProbeGraph>)noexcept;
} // namespace evrm
