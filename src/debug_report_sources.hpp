#pragma once
#include "debug_report_types.hpp"
#include <string_view>
namespace evrm {
struct PlayerFrame;struct PreparedAvatar;struct ModSettings;struct AvatarMenuSnapshot;
bool debug_report_sources_pending()noexcept;
void debug_report_begin_sources(std::uint64_t job,std::uint64_t request_ms);
bool debug_report_sources_ready(std::uint64_t job)noexcept;
void debug_report_cancel_sources(std::uint64_t job)noexcept;
void debug_report_observe_player(const PlayerFrame&,std::uint64_t sample_ms)noexcept;
void debug_report_observe_scene(DebugReportSceneFamily)noexcept;
void debug_report_observe_view(std::uint32_t width,std::uint32_t height,std::uint32_t depth_format,std::uint32_t depth_samples)noexcept;
void debug_report_observe_lighting(std::array<std::uint32_t,3> extent)noexcept;
void debug_report_observe_gpu(const DebugReportGpuInfo&)noexcept;
void debug_report_observe_settings(const ModSettings&,const AvatarMenuSnapshot*)noexcept;
void debug_report_observe_model(const PreparedAvatar*)noexcept;
void debug_report_end_source_frame(std::uint64_t tick_ms)noexcept;
DebugReportData debug_report_take_sources(std::uint64_t job,std::uint64_t now_ms);
void debug_report_presented(std::uint64_t tick_us,std::uint64_t source_generation,bool menu_open,bool reporting)noexcept;
DebugReportSystem debug_report_collect_system()noexcept;
std::string debug_report_engine_events(std::string_view tail);
}
