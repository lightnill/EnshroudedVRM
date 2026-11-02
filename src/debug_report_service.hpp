#pragma once
#include "debug_report_types.hpp"
#include <filesystem>
#include <functional>
#include <limits>
#include <string_view>
namespace evrm {
struct DebugReportOptions {
    std::filesystem::path directory,game_directory,capture_shader;std::string mod_version,game_sha256,nonce;bool background=true;
    std::size_t write_limit=std::numeric_limits<std::size_t>::max();
    std::function<std::uint64_t()> clock;
    std::function<std::string()> utc;
    std::function<DebugReportSystem()> system;
    std::function<DebugReportData(std::uint64_t,std::uint64_t,std::uint64_t)> sources;
    std::function<void(std::string&,std::string&)> files;
    std::function<void(const std::filesystem::path&)> open_folder;
};
class DebugReportService {
public:
    explicit DebugReportService(DebugReportOptions);
    ~DebugReportService();
    DebugReportService(const DebugReportService&)=delete;
    DebugReportService& operator=(const DebugReportService&)=delete;
    std::uint64_t request(bool include_image)noexcept;
    std::shared_ptr<const DebugReportStatus> status()const noexcept;
    bool capture_started(std::uint64_t,std::uint64_t source=0)noexcept;
    void capture_retired(std::uint64_t source)noexcept;
    bool capture_failed(std::uint64_t,DebugReportCode)noexcept;
    bool submit_pixels(std::uint64_t,std::shared_ptr<const DebugReportImage>,std::uint64_t source=0)noexcept;
    bool preview_presented(std::uint64_t,std::string_view)noexcept;
    bool apply(const DebugReportAction&)noexcept;
    void pump()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
std::shared_ptr<const DebugReportStatus> debug_report_status()noexcept;
void debug_report_apply(const DebugReportAction&)noexcept;
bool debug_report_capture_started(std::uint64_t,std::uint64_t source)noexcept;
void debug_report_capture_retired(std::uint64_t source)noexcept;
void debug_report_capture_failed(std::uint64_t,DebugReportCode)noexcept;
void debug_report_submit_pixels(std::uint64_t,std::shared_ptr<const DebugReportImage>,std::uint64_t source)noexcept;
void debug_report_preview_presented(std::uint64_t,std::string_view)noexcept;
bool debug_report_busy()noexcept;
}
