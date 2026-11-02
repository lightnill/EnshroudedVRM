#pragma once
#include "debug_report_types.hpp"
#include <string_view>
namespace evrm {
const char* debug_report_code_name(DebugReportCode)noexcept;
DebugReportContents make_debug_report_contents(const DebugReportData&);
std::string debug_report_graphics_json(std::string_view);
}
