#pragma once
#include "native_clip_shader.hpp"
#include <array>
#include <cstdint>

namespace evrm {
struct NativeBodyShaderIdentity {
    std::uint32_t graphics_stage_count=2;
    std::uint32_t vertex_bytes=0,fragment_bytes=0;
    std::array<std::uint8_t,32> vertex_digest{},fragment_digest{};
    std::uint32_t tess_control_bytes=0,tess_evaluation_bytes=0;
    std::array<std::uint8_t,32> tess_control_digest{},tess_evaluation_digest{};
    std::uint32_t mesh_bytes=0;
    std::array<std::uint8_t,32> mesh_digest{};
};
enum class NativeBodyCoverageKind { unknown,clip,separate_transform,brick,chunklet,vfx };
struct NativeBodyCoverage {
    NativeBodyCoverageKind kind=NativeBodyCoverageKind::unknown;
    NativeClipKind clip_kind=NativeClipKind::none;
    // All nonzero binding fields refer to descriptor set2. Brick uses set0 only.
    std::uint32_t instance_binding=0,culled_binding=0,transform_binding=0;
};
// Exact shader identity/dataflow only. separate_transform reads compact32;
// neither classification nor backing disjointness excludes alternate body data.
// A partial preview may leave these exact draws unchanged, without claiming
// complete body coverage. It must never apply an Instance128 mask to them.
// Every present stage needs a complete nonzero fingerprint. Brick and chunklet
// contracts pin all stage hashes; other contracts pin the vertex stage.
NativeBodyCoverage classify_native_body_coverage(const NativeBodyShaderIdentity&) noexcept;
}
