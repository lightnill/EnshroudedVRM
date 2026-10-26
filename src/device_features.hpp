#pragma once
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace evrm {
inline constexpr std::size_t device_feature_chain_limit=32;
inline constexpr std::size_t device_feature_extension_limit=64;
inline constexpr std::size_t device_feature_name_limit=VK_MAX_EXTENSION_NAME_SIZE;
enum class DeviceFeatureSource { none,enabled_features,features2,vulkan12,descriptor_indexing,multiple,vulkan13,dynamic_rendering,mesh_shader };
enum class DeviceFeatureStatus {
    ok,invalid_create_info,chain_cycle,chain_limit,extension_limit,
    invalid_extension_name,conflicting_features,invalid_feature_value,allocation_failure
};
struct DeviceFeatureFlag {
    bool available=false,enabled=false;
    DeviceFeatureSource source=DeviceFeatureSource::none;
};
struct DeviceFeaturesObservation {
    DeviceFeatureStatus status=DeviceFeatureStatus::ok;
    bool complete=false;
    bool p_enabled_features_present=false,features2_present=false;
    bool vulkan12_present=false,descriptor_indexing_present=false;
    DeviceFeatureFlag geometry_shader,uniform_buffer_update_after_bind,independent_blend;
    DeviceFeatureFlag dynamic_rendering;
    DeviceFeatureFlag mesh_shader;
    bool single_physical_device=true;
    DeviceFeatureFlag storage_buffer_update_after_bind,update_unused_while_pending;
    DeviceFeatureFlag descriptor_binding_partially_bound;
    uint32_t declared_extension_count=0;
    std::vector<VkStructureType> chain_s_types;
    std::vector<std::string> enabled_extensions;
};
// Observe readable caller-owned input without mutating or retaining its pointers.
// Absent representations are unavailable. Unknown structs are listed, not read.
// Complete means the relevant observation is bounded and consistent; it is not
// validation of every Vulkan chain rule or evidence of physical-device support.
// Any traversal/name/conflict failure makes all requested feature flags unavailable.
DeviceFeaturesObservation inspect_device_features(const VkDeviceCreateInfo&) noexcept;
bool native_rendering_environment_supported(std::uint32_t,const DeviceFeaturesObservation&) noexcept;
}
