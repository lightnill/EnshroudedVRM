#pragma once
#include "native_skinning.hpp"
#include <vulkan/vulkan.h>
#include <filesystem>

namespace evrm {
class NativeSkinningGpu;
class NativeSkinGpuAsset {
public:
    ~NativeSkinGpuAsset();
    VkDescriptorBufferInfo indices()const noexcept;
    VkDescriptorBufferInfo surface()const noexcept;
    bool mouth_available()const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl;
    explicit NativeSkinGpuAsset(std::shared_ptr<Impl>);
    friend class NativeSkinningGpu;
};
class NativeSkinGpuGeometry {
public:
    ~NativeSkinGpuGeometry();
    VkDescriptorBufferInfo binding()const noexcept;
    VkDescriptorBufferInfo surface_binding()const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl;
    explicit NativeSkinGpuGeometry(std::shared_ptr<Impl>);
    friend class NativeSkinningGpu;
};
class NativeSkinGpuFrame {
public:
    ~NativeSkinGpuFrame();
    std::shared_ptr<const NativeSkinGpuGeometry> geometry()const noexcept;
    // Diagnostic only: caller must establish execution completion first.
    std::uint32_t status_after_completion()const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl;
    explicit NativeSkinGpuFrame(std::shared_ptr<Impl>);
    friend class NativeSkinningGpu;
};
// Caller serializes creation/recording/retirement and keeps device and the
// quarantine flag alive until all leases are gone. Leases retain pipeline and
// allocation ownership, but geometry history never retains predecessor frames.
class NativeSkinningGpu {
public:
    NativeSkinningGpu(VkDevice,PFN_vkGetDeviceProcAddr,const VkPhysicalDeviceMemoryProperties&,
        const VkPhysicalDeviceLimits&,const std::filesystem::path&,const bool* quarantine);
    ~NativeSkinningGpu();
    std::shared_ptr<NativeSkinGpuAsset> asset(std::shared_ptr<const NativeSkinAsset>);
    std::shared_ptr<NativeSkinGpuAsset> asset(std::shared_ptr<const NativeSkinAsset>,bool surface,
        const std::shared_ptr<NativeSkinGpuAsset>& reuse={});
    // Lazy enhanced compute resources. Static mask UV uploads need no pipeline.
    void prepare_surface();
    void upload(VkCommandBuffer,const std::shared_ptr<NativeSkinGpuAsset>&);
    // Call only after successful completion of the upload's owning submission.
    void finish_upload(const std::shared_ptr<NativeSkinGpuAsset>&);
    std::shared_ptr<NativeSkinGpuFrame> frame(const std::shared_ptr<NativeSkinGpuAsset>&,
        const NativeSkinPose&,VkDescriptorBufferInfo prepared_frame,
        std::shared_ptr<const NativeSkinGpuGeometry> previous={},const vrm::MouthWeights& mouth_weights={});
    std::shared_ptr<NativeSkinGpuFrame> frame(const std::shared_ptr<NativeSkinGpuAsset>&,
        const NativeSkinPose&,VkDescriptorBufferInfo prepared_frame,
        std::shared_ptr<const NativeSkinGpuGeometry> previous,const vrm::MouthWeights& mouth_weights,bool surface);
    // Preparation must already have written the shared frame. This records
    // transforms -> deformation -> aggregate commit gate -> graphics visibility.
    void record(VkCommandBuffer,const std::shared_ptr<NativeSkinGpuFrame>&);
private:
    struct Impl;std::shared_ptr<Impl> impl;
};
}
