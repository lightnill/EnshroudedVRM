#pragma once
#include "debug_report_types.hpp"
#include <functional>
namespace evrm {
using DebugReportSink=std::function<bool(const std::uint8_t*,std::size_t)>;
bool write_debug_report_zip(const DebugReportContents&,const DebugReportSink&)noexcept;
std::vector<std::uint8_t> encode_debug_report_png(const DebugReportImage&)noexcept;
DebugReportImage decode_debug_report_png(const std::vector<std::uint8_t>&)noexcept;
std::uint32_t debug_report_crc32(const std::uint8_t*,std::size_t)noexcept;
}
