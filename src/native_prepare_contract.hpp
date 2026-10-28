#pragma once
#include <cstddef>
#include <cstdint>

namespace evrm {
// Private additive diagnostic only. Lighting qualifies active-view placement;
// it does not establish entity ownership or authorize native draw suppression.
struct alignas(16) NativePrepareControl {
    std::uint32_t permit[4]{}; // CPU permit, parts <=64, Instance128 records <=131072, regions <=32
    std::uint32_t offsets[4]{}; // absolute VisCamera, GbufCamera, LightingOwner, arena bytes <=4MiB
    std::uint32_t extent[4]{}; // width, height, reserved, reserved
    float config[4]{}; // height scale [0.1,10], XZ basis sign (+1 VRM1/-1 VRM0), diagnostic X, reserved
    std::uint32_t parts[64][4]{}; // instance ID, expected model@120, skin@108, tag@112
    std::uint32_t regions[32][4]{}; // absolute indirect offset, absolute count offset, max count <=1024, stride24
    std::uint32_t blend[4]{}; // mode0/1, native2320 offset, forward96 offset, reserved0
    float blend_min[4]{},blend_max[4]{}; // all BLEND model-space positions; w0
    float blend_base_max[4]{}; // nonnegative absolute RGB bounds; alpha bound1
    float blend_emissive_max[4]{}; // nonnegative absolute RGB bounds; w0
    std::uint32_t history[4]{}; // previous emitted frame eligible0/1; reserved0
    std::uint32_t shadow[4]{}; // validated native Shadow views enabled0/1, count<=32, reserved0
    std::uint32_t shadowViews[32][4]{}; // absolute ShadowUBO352 arena offset, reserved0
};
struct alignas(16) NativePreparedFrame {
    float currentRows[16]{};
    float previousUvRows[16]{};
    float normalRows[12]{};
    float screen[4]{};
    std::uint32_t control[4]{}; // commit, GPU-validated previous emitted history, reserved, reserved
    std::uint32_t rootScale[4]{}; // packed actor root.xyz; bits of actor * loaded-model height scale
};
inline constexpr std::uint32_t native_oit_constants_offset=65536;
// Output-only menu checkpoints outside the unchanged224-byte render ABI.
inline constexpr std::uint32_t native_menu_diagnostic_offset=224,native_menu_prepared_bytes=240;
inline constexpr std::uint32_t native_menu_diagnostic_magic=0x4D454E55;
struct NativeMenuPrepareDiagnostic {std::uint32_t magic=0,checkpoint=0,reserved[2]{};};
static_assert(sizeof(NativeMenuPrepareDiagnostic)==16&&sizeof(NativePreparedFrame)==native_menu_diagnostic_offset,"menu diagnostic gap ABI");
inline constexpr std::uint32_t native_oit_forward_offset=131072;
inline constexpr std::uint32_t native_oit_prepared_bytes=131168;
inline constexpr std::uint32_t native_oit_diagnostic_offset=256;
inline constexpr std::uint32_t native_oit_diagnostic_magic=0x4F495431;
// Output-only diagnostic in the unused mixed frame gap; never permit authority.
struct alignas(16) NativeOitPrepareDiagnostic {
    std::uint32_t magic=0,checkpoint=0,valid_mask=0,reserved=0;
    std::uint32_t constants[10]{},w[2]{},domain[2]{},radiance[3]{},spare[11]{};
};
static_assert(sizeof(NativeOitPrepareDiagnostic)==128&&offsetof(NativeOitPrepareDiagnostic,constants)==16&&
    offsetof(NativeOitPrepareDiagnostic,w)==56&&offsetof(NativeOitPrepareDiagnostic,domain)==64&&
    offsetof(NativeOitPrepareDiagnostic,radiance)==72&&offsetof(NativeOitPrepareDiagnostic,spare)==84,"owned OIT diagnostic ABI");
static_assert(native_oit_diagnostic_offset>=sizeof(NativePreparedFrame)&&
    native_oit_diagnostic_offset+sizeof(NativeOitPrepareDiagnostic)<=native_oit_constants_offset,"owned diagnostic gap");
static_assert(alignof(NativePrepareControl)==16&&sizeof(NativePrepareControl)==2224,"native preparation control ABI");
static_assert(offsetof(NativePrepareControl,permit)==0&&offsetof(NativePrepareControl,offsets)==16&&
    offsetof(NativePrepareControl,extent)==32&&offsetof(NativePrepareControl,config)==48&&
    offsetof(NativePrepareControl,parts)==64&&offsetof(NativePrepareControl,regions)==1088,"native preparation control offsets");
static_assert(offsetof(NativePrepareControl,blend)==1600&&offsetof(NativePrepareControl,blend_min)==1616&&
    offsetof(NativePrepareControl,blend_max)==1632&&offsetof(NativePrepareControl,blend_base_max)==1648&&
    offsetof(NativePrepareControl,blend_emissive_max)==1664&&offsetof(NativePrepareControl,history)==1680&&
    offsetof(NativePrepareControl,shadow)==1696&&offsetof(NativePrepareControl,shadowViews)==1712,"native OIT/history/shadow control offsets");
static_assert(alignof(NativePreparedFrame)==16&&sizeof(NativePreparedFrame)==224,"native frame ABI");
static_assert(offsetof(NativePreparedFrame,currentRows)==0&&offsetof(NativePreparedFrame,previousUvRows)==64&&
    offsetof(NativePreparedFrame,normalRows)==128&&offsetof(NativePreparedFrame,screen)==176&&
    offsetof(NativePreparedFrame,control)==192&&offsetof(NativePreparedFrame,rootScale)==208,"avatar_native frame offsets");
} // namespace evrm
