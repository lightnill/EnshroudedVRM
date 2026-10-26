#pragma once

#include "retarget.hpp"

namespace evrm {

// Avatar-model space only. The native GPU pass supplies the separate actor,
// basis and height transform. In particular, normals contain no actor rotation.
struct NativeAvatarVertex {
    float position[3], normal[3], uv[2], previous_position[3];
};
static_assert(sizeof(NativeAvatarVertex) == 44, "Native vertex pulling ABI");

struct NativeAvatarDraw {
    std::uint32_t first_index, index_count;
    int material = -1;
};

struct NativeAvatarPose {
    std::vector<NativeAvatarVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<NativeAvatarDraw> draws;
    std::string error;
    explicit operator bool() const { return error.empty(); }
};

// Failure is atomic: no vertices, indices or draws accompany an error.
// History requires valid matching vertex/index/draw layout. The caller resets
// history when switching assets or reactivating an avatar (identity is external).
NativeAvatarPose make_native_avatar_pose(const vrm::Avatar& avatar,
    const vrm::RetargetResult& pose, const NativeAvatarPose* previous = nullptr) noexcept;

} // namespace evrm
