#include "../src/debug_report_service.hpp"
namespace evrm {
bool debug_report_capture_started(std::uint64_t,std::uint64_t)noexcept{return false;}
void debug_report_capture_retired(std::uint64_t)noexcept{}
void debug_report_capture_failed(std::uint64_t,DebugReportCode)noexcept{}
void debug_report_submit_pixels(std::uint64_t,std::shared_ptr<const DebugReportImage>,std::uint64_t)noexcept{}
void debug_report_preview_presented(std::uint64_t,std::string_view)noexcept{}
void debug_report_apply(const DebugReportAction&)noexcept{}
}
