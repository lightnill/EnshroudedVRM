#pragma once
#include "device_features.hpp"
namespace evrm {
// Original enabled device feature only. This enables no device feature and
// supplies no resource initialization, lifetime, source or submit authority.
inline bool native_mtoon_shade_features_supported(const DeviceFeaturesObservation& f)noexcept{
    return f.complete&&f.status==DeviceFeatureStatus::ok&&f.single_physical_device&&
        f.descriptor_binding_partially_bound.available&&f.descriptor_binding_partially_bound.enabled;
}
}
