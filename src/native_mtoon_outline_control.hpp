#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace evrm {
// Matches the outline entry points' set2:7. All sources and this owned control
// belong to one retained recording. The parent joins the actual geometry and
// scene/submit admission before setting active=1; this POD grants no authority.
enum class NativeMToonLegacySource:std::uint32_t { Unavailable=0,PackedMetric=1,SkinRig=2 };
struct alignas(16) NativeMToonOutlineControl {
    std::array<std::uint32_t,4> facts{}; // current legacy mode, joined history, prior legacy mode, active
    std::array<float,4> time{}; // previous animation seconds, reserved0
};
static_assert(sizeof(NativeMToonOutlineControl)==32&&alignof(NativeMToonOutlineControl)==16&&offsetof(NativeMToonOutlineControl,time)==16);
}
