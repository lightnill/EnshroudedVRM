#pragma once

#include "gpu_probe.hpp"
#include "retarget.hpp"

#include <filesystem>
#include <memory>

namespace evrm {

struct SelectedPart {
    std::uint32_t instance_id = 0;
    std::uint32_t model_index = 0;
    std::uint32_t skin_matrix_offset = 0;
    std::vector<std::uint32_t> palette_joint_ids;
    // Explicit model palette slots owned by native simulation chains.
    // Unmarked zero entries remain genuine hierarchy-root observations.
    std::vector<std::uint32_t> auxiliary_palette_indices;
    // Geometry can identify several resources with an identical palette.
    std::vector<std::string> native_model_guids;
};

struct PlayerFrame {
    vrm::SourcePose source_pose;
    // Column-major, native model space -> world relative to camera packed origin.
    vrm::Matrix actor_world{};
    // Column-major conversion of the origin-relative four float4 rows at byte
    // 848 (_22_m31, shader c53). Consumes actor_world's camera-relative output.
    // The renderer flips output clip Y and uses the game's reversed-Z depth.
    vrm::Matrix camera_world_to_clip{};
    // Stable projection for the post-temporal avatar pass. Native depth remains
    // in jittered raster space, offset by -camera_jitter_uv from this projection.
    vrm::Matrix camera_stable_world_to_clip{};
    std::array<float,2> camera_jitter_uv{};
    std::array<std::uint32_t, 3> camera_packed_origin{};
    std::vector<SelectedPart> parts;
    std::uint32_t queue_family = 0;
    std::size_t observed_joint_count = 0;
    std::string error;
    explicit operator bool() const { return error.empty() && !parts.empty(); }
};

// Profiles/catalogs are local research files containing game-derived metadata.
// This source contains no native bind matrices or captured instance identities.
// Construction is atomic: a load/build error leaves ready()==false. Every read
// failure returns an empty pose/part selection, so suppression cannot use it.
class EnshroudedBridge {
public:
    EnshroudedBridge(const std::filesystem::path& profile_path,
                     const std::filesystem::path& catalog_path,
                     const std::string& verified_exe_sha256);
    bool ready() const noexcept;
    const std::string& error() const noexcept;
    // Requires submitted culled_spans in descriptor-relative 8-byte records.
    // Allocation capacity is never evidence that a culled record was drawn.
    PlayerFrame read_player_frame(const GpuSnapshot&) const noexcept;
    static std::map<std::string, std::size_t> readback_limits();

private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    std::string error_;
};

} // namespace evrm
