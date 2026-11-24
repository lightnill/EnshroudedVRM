#pragma once
#include "vrm_asset.hpp"

namespace evrm {
// Native anatomy: right +X, up +Y, forward +Z. VRM1 right -X/forward
// +Z, VRM0 right +X/forward -Z. Both boundaries change handedness. The
// basis is its own inverse; it converts vectors, not semantic hand names.
inline float native_avatar_forward_sign(vrm::VrmVersion version) noexcept {
    return version==vrm::VrmVersion::V0?-1.f:1.f;
}
inline double native_avatar_axis_sign(int axis,double forward_sign) noexcept {
    return axis==0?-forward_sign:axis==2?forward_sign:1.;
}
inline vrm::Matrix native_avatar_basis(vrm::VrmVersion version) noexcept {
    const auto forward=native_avatar_forward_sign(version);
    return {-forward,0,0,0,0,1,0,0,0,0,forward,0,0,0,0,1};
}
}
