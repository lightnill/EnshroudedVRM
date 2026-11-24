#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
namespace evrm {
struct NativeMToonCompositeParameters {std::uint32_t width=0,height=0,generation=0,reserved=0;};
// Diagnostic only; caller independently proves the original GPU completion.
struct NativeMToonCompositeObservation {bool available=false;std::uint32_t generation=0,sampled=0,eligible=0,mixed=0,outline_mixed=0;};
struct NativeMToonCompositePixel {std::array<std::uint32_t,4> words{};};
static_assert(sizeof(NativeMToonCompositeParameters)==16&&sizeof(NativeMToonCompositePixel)==16);
struct NativeMToonCompositeImage {VkImage image{};VkDescriptorImageInfo descriptor{};VkFormat format=VK_FORMAT_UNDEFINED;VkExtent2D extent{};};
struct NativeMToonCompositeShader {const std::uint32_t* words=nullptr;std::size_t count=0;};
struct NativeMToonCompositeCapabilities {VkPhysicalDeviceLimits limits{};VkPhysicalDeviceMemoryProperties memory{};
    std::array<VkFormatFeatureFlags,3> format_features{};bool storage_image_extended_formats=false;};
// Owned pipeline program; contains no native scene/destination/source handles.
// Preparation belongs to the parent's worker/cache, never command recording.
// The parent pointer is an identity join only. Workers never read its mutable
// bool; the serialized cache/pass owner propagates uncertain completion into
// this program's atomic quarantine before releasing any device resources.
class NativeMToonCompositeProgram {
public:
    NativeMToonCompositeProgram(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonCompositeCapabilities&,NativeMToonCompositeShader,const bool* parent_quarantine);
    ~NativeMToonCompositeProgram();
    void quarantine()const noexcept;
    bool reusable()const noexcept;
    NativeMToonCompositeProgram(const NativeMToonCompositeProgram&)=delete;
    NativeMToonCompositeProgram& operator=(const NativeMToonCompositeProgram&)=delete;
private:
    struct Impl;std::unique_ptr<Impl> impl;
    friend class NativeMToonCompositePass;
};
// Host pass construction/recording/destruction is externally serialized. The
// program retains bounded dormant descriptor banks (and composite storage).
// Parent must prove original GPU completion/reset before releasing a pass;
// only a bank whose final pass lease retired can be rewritten/reused.
// Command recorder only. The parent admits exact native scene destinations,
// layouts, disjoint physical backing, original compute scope and queue, and
// retains every input/device/restore-layout/quarantine flag through retirement.
// It clears/produces same-generation owned packets/identities before this pass
// and arms the coherent UBO only at a supported original submission. This class
// creates no SceneReady, source lease or destination authority; no submissions,
// waits, push writes or image-layout transitions are performed here.
class NativeMToonCompositePass {
public:
    static constexpr std::uint32_t pixel_limit=1u<<24;
    NativeMToonCompositePass(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonCompositeCapabilities&,
        NativeMToonCompositeParameters,VkDescriptorBufferInfo packets,VkDescriptorBufferInfo identities,
        VkDescriptorBufferInfo permit,const std::array<NativeMToonCompositeImage,3>&,
        NativeMToonCompositeShader,const bool* parent_quarantine,
        std::shared_ptr<const NativeMToonCompositeProgram> prepared={});
    ~NativeMToonCompositePass();
    NativeMToonCompositePass(const NativeMToonCompositePass&)=delete;
    NativeMToonCompositePass& operator=(const NativeMToonCompositePass&)=delete;
    bool record(VkCommandBuffer,VkPipeline original,VkPipelineLayout compatible,
                std::uint32_t native_set_count,const VkDescriptorSet*)noexcept;
    // Reuse with an empty shader argument; still no scene/read/write authority.
    std::shared_ptr<const NativeMToonCompositeProgram> prepared_program()const noexcept;
    NativeMToonCompositeObservation observation_after_original_completion()const noexcept;
    void quarantine()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
