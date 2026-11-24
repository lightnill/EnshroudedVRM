#pragma once
#include "native_mtoon_request.hpp"
#include <vulkan/vulkan.h>

namespace evrm {
class NativeAvatarAssetStager;
enum class NativeMToonFallbackImage : std::uint32_t {volume,cube,array};
// Immutable uploaded leaves. Published only through a completed asset-bank
// job; this is not native scene admission. Retain the resources through every
// submitted consumer and obey the parent stager's device/queue lifetime rules.
class NativeMToonResources {
public:
    ~NativeMToonResources();
    NativeMToonResources(const NativeMToonResources&)=delete;
    NativeMToonResources& operator=(const NativeMToonResources&)=delete;
    const std::shared_ptr<const NativeMToonRequest>& request()const noexcept;
    VkDescriptorBufferInfo uniform(std::size_t material)const noexcept;
    VkDescriptorImageInfo texture(std::size_t material,vrm::MToonTextureSlot)const noexcept;
    // Empty when no selected map consumes raw UVs; never invent a valid stream.
    VkDescriptorBufferInfo uvs()const noexcept;
    // Valid typed owned descriptors for unavailable optional roles only. These
    // white pixels grant no native mask/source readiness or lighting default.
    // Use only on the exact original upload queue/family; completion is part
    // of the parent asset bank's existing ready/adoption transaction.
    VkDescriptorImageInfo fallback(NativeMToonFallbackImage,VkQueue,std::uint32_t family)const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl_;
    explicit NativeMToonResources(std::shared_ptr<Impl>);
    friend class NativeAvatarAssetStager;
};
}
