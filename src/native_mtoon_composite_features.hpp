#pragma once
#include "device_features.hpp"
namespace evrm {
// Existing enabled feature only; no device creation, feature enabling or scene authority.
inline bool native_mtoon_composite_features_supported(const DeviceFeaturesObservation& f)noexcept{
    return f.complete&&f.status==DeviceFeatureStatus::ok&&f.single_physical_device&&f.storage_image_extended_formats.available&&f.storage_image_extended_formats.enabled;
}
}
