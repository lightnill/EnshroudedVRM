#pragma once
#include "native_skinning_gpu.hpp"
#include <vulkan/vk_layer.h>
namespace evrm {
enum class NativeAvatarUploadState {Idle,Pending,Ready,Failed};
class NativeAvatarAssetStager;
class NativeAvatarGpuAssets {
public:
    ~NativeAvatarGpuAssets();
    const std::shared_ptr<const vrm::Avatar>& avatar()const noexcept;
    const std::shared_ptr<NativeSkinGpuAsset>& skin()const noexcept;
    VkDescriptorImageInfo material(std::size_t)const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl_;
    explicit NativeAvatarGpuAssets(std::shared_ptr<Impl>);
    friend class NativeAvatarAssetStager;
};
// Caller serializes this object and queue submission using the existing queue
// ownership guard. A cancelled pending job remains owned until completion.
class NativeAvatarAssetStager {
public:
    NativeAvatarAssetStager(VkDevice,PFN_vkGetDeviceProcAddr,PFN_vkSetDeviceLoaderData,
        const VkPhysicalDeviceMemoryProperties&,std::shared_ptr<NativeSkinningGpu>,bool* quarantine);
    ~NativeAvatarAssetStager();
    NativeAvatarUploadState update(VkQueue,std::uint32_t family,std::uint64_t generation,
        const std::shared_ptr<const vrm::Avatar>&,const std::shared_ptr<const NativeSkinAsset>& skin={})noexcept;
    std::shared_ptr<NativeAvatarGpuAssets> ready(std::uint64_t,const std::shared_ptr<const vrm::Avatar>&)const noexcept;
    std::shared_ptr<NativeAvatarGpuAssets> take_ready(std::uint64_t,const std::shared_ptr<const vrm::Avatar>&)noexcept;
    void cancel()noexcept;
    const std::string& error()const noexcept;
    // Only after the owning device is proven idle (or lost) at teardown.
    void device_idle_complete()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
