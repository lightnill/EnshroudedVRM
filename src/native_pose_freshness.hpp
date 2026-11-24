#pragma once
#include <cstdint>
namespace evrm {
inline constexpr std::uint64_t native_pose_max_age_ms=500;
inline bool native_pose_is_fresh(std::uint64_t published,std::uint64_t now)noexcept {
    return published&&now>=published&&now-published<=native_pose_max_age_ms;
}
}
