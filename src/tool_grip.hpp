#pragma once
#include "retarget.hpp"
#include <array>

namespace evrm {
struct ToolHandCorrection {
    vrm::Matrix model_delta{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    // Absolute buffered VRM wrist pose, in native hand-bone axes/model units.
    // Client slots rebase their fresh native grip against this same pose.
    vrm::Matrix target_native_hand{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    bool valid=false;
    // Optional item-only translation in native model units. Wrist/palm and
    // shield frames retain target_native_hand; items follow palm anatomy.
    std::array<float,3> item_offset_native{};
    bool item_offset_valid=false;
};
// Right, left. Rigid native-model correction; never scales held objects.
std::array<ToolHandCorrection,2> make_tool_hand_corrections(
    const vrm::Avatar&,const vrm::SourcePose&,const vrm::RetargetResult&,
    double height_scale,double basis_xz) noexcept;
}
