#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace evrm {
constexpr std::size_t debug_report_archive_limit=16u*1024*1024,debug_report_png_limit=9u*1024*1024;
enum class DebugReportCode:std::uint8_t {None,Unavailable,Busy,Permission,Disk,Stale,Unsupported,Invalid,Limit,Cancelled};
enum class DebugReportPhase:std::uint8_t {Idle,Collecting,AwaitingPixels,Encoding,Preview,Writing,Finalizing,Complete,Partial,Cancelled,Failed};
enum class DebugReportSceneFamily:std::uint8_t {Unknown,Terrain,Building};
struct DebugReportGpuInfo {bool available=false,shared_memory=false;std::string model;std::uint32_t vendor=0,device=0,driver_version=0,api_version=0;std::uint64_t local_bytes=0,budget_bytes=0;};
struct DebugReportSystem {std::string cpu_model;std::uint32_t logical_processors=0,physical_cores=0,windows_build=0;
    std::uint64_t ram_total=0,ram_available=0,working_set=0,private_bytes=0;DebugReportGpuInfo gpu;};
struct DebugReportMaterial {std::uint32_t index=0,preset=0;};
struct DebugReportSettings {bool available=false,avatar_available=false,appearance_available=false,replacement_enabled=true,voice_enabled=false;std::uint32_t voice_source=0,strength=85,sensitivity=50,smoothing_ms=60,whole_preset=0;
    std::vector<DebugReportMaterial> materials;};
struct DebugReportModel {bool available=false,bundled_sample=false,morphs_known=false;std::uint32_t vrm_version=0,mouth_shapes=0;
    std::uint64_t meshes=0,vertices=0,indices=0,materials=0,joints=0,morphs=0,textures=0,texture_bytes=0;};
struct DebugReportMapLocation {bool available=false,local_world_player=false;std::array<double,3> xyz{};std::uint64_t sample_ms=0;};
struct DebugReportScene {DebugReportMapLocation location;bool relative_clip_available=false,render_activity_available=false,depth_available=false,lighting_available=false;
    std::array<double,16> player_to_clip{};std::array<std::uint32_t,3> lighting_extent{};
    std::uint32_t width=0,height=0,depth_format=0,depth_samples=0;std::uint64_t sample_ms=0,terrain_draws=0,building_draws=0,unknown_draws=0;};
struct DebugReportImage {std::uint32_t width=0,height=0;std::vector<std::uint8_t> rgba;};
struct DebugReportData {std::string mod_version,game_sha256,utc,graphics_source_json,diagnostics;
    std::uint64_t request_ms=0,collected_ms=0,diagnostics_contention_drops=0,diagnostics_overwritten=0;DebugReportSystem system;DebugReportSettings settings;DebugReportModel model;DebugReportScene scene;
    std::vector<std::uint32_t> frame_intervals_us;std::vector<DebugReportCode> partial;};
struct DebugReportFile {std::string name;std::vector<std::uint8_t> bytes;};
using DebugReportContents=std::vector<DebugReportFile>;
enum class DebugReportActionKind:std::uint8_t {None,Begin,IncludePreview,SaveTextOnly,Cancel,OpenFolder};
struct DebugReportAction {DebugReportActionKind kind=DebugReportActionKind::None;std::uint64_t job=0;bool include_image=false;std::string image_id;};
struct DebugReportStatus {DebugReportPhase phase=DebugReportPhase::Idle;DebugReportCode code=DebugReportCode::None;std::uint64_t job=0;
    bool capture_requested=false;
    std::uint64_t capture_source=0; // Private context generation; never exported.
    std::shared_ptr<const std::vector<std::uint32_t>> capture_program;
    std::string filename,image_id;bool preview_presented=false;std::shared_ptr<const DebugReportImage> preview;};
}
