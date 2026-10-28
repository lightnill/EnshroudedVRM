#pragma once
#include "native_avatar_pose.hpp"
#include <array>
#include <vector>

namespace evrm {
enum class NativeMaterialPlanFailure {none,geometry,draw_range,material,alpha_mode,nonfinite,uv_bounds,all_blend,allocation};
struct NativeMaterialPlan {
    std::vector<std::uint32_t> opaque_draws,blend_draws;
    std::array<float,4> blend_min{},blend_max{},base_max{},emissive_max{};
    NativeMaterialPlanFailure failure=NativeMaterialPlanFailure::none;
    bool mixed=false;
};
// Atomic CPU partition/bounds only. Draw indices refer to this exact immutable
// pose. No ownership, source readiness, viewport, GPU permit or native exemption.
// Initial native path requires at least one OPAQUE/MASK witness. BLEND uses an
// owned single-mip base texture with explicit LOD0; all referenced |UV|<=16384.
bool make_native_material_plan(const vrm::Avatar&,const NativeAvatarPose&,NativeMaterialPlan&)noexcept;
}
