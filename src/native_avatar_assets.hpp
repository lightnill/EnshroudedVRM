#pragma once
#include "native_skinning_gpu.hpp"
#include "native_surface_material.hpp"
#include <vulkan/vk_layer.h>
namespace evrm {
enum class NativeAvatarUploadState {Idle,Pending,Ready,Failed};
enum class NativeSurfaceTexture {Normal,Matcap,Rim};
class NativeAvatarAssetStager;
class NativeAvatarGpuAssets {
public:
    ~NativeAvatarGpuAssets();
    const std::shared_ptr<const vrm::Avatar>& avatar()const noexcept;
    const std::shared_ptr<NativeSkinGpuAsset>& skin()const noexcept;
    VkDescriptorImageInfo material(std::size_t)const noexcept;
    const NativeSurfaceSelection& surface_selection()const noexcept;
    VkDescriptorImageInfo surface_material(std::size_t,NativeSurfaceTexture)const noexcept;
    VkDescriptorBufferInfo surface_vertices()const noexcept;
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
        const VkPhysicalDeviceMemoryProperties&,std::uint32_t image_dimension_limit,
        std::shared_ptr<NativeSkinningGpu>,bool* quarantine);
    ~NativeAvatarAssetStager();
    NativeAvatarUploadState update(VkQueue,std::uint32_t family,std::uint64_t generation,
        const std::shared_ptr<const vrm::Avatar>&,const std::shared_ptr<const NativeSkinAsset>& skin={})noexcept;
    NativeAvatarUploadState update(VkQueue,std::uint32_t family,std::uint64_t generation,
        const std::shared_ptr<const vrm::Avatar>&,const std::shared_ptr<const NativeSkinAsset>&,
        const NativeSurfaceSelection&,const std::shared_ptr<NativeAvatarGpuAssets>& reuse={})noexcept;
    std::shared_ptr<NativeAvatarGpuAssets> ready(std::uint64_t,const std::shared_ptr<const vrm::Avatar>&)const noexcept;
    std::shared_ptr<NativeAvatarGpuAssets> take_ready(std::uint64_t,const std::shared_ptr<const vrm::Avatar>&)noexcept;
    std::shared_ptr<NativeAvatarGpuAssets> ready(std::uint64_t,const std::shared_ptr<const vrm::Avatar>&,const NativeSurfaceSelection&)const noexcept;
    std::shared_ptr<NativeAvatarGpuAssets> take_ready(std::uint64_t,const std::shared_ptr<const vrm::Avatar>&,const NativeSurfaceSelection&)noexcept;
    void cancel()noexcept;
    const std::string& error()const noexcept;
    // Only after the owning device is proven idle (or lost) at teardown.
    void device_idle_complete()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
