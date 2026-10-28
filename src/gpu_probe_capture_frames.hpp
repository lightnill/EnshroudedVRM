#pragma once
#include <cstdint>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace evrm {
// Existing strict diagnostic-frame parser, separated for release-mode tests.
inline std::set<std::uint64_t> probe_capture_frames(std::wstring frames) {
    if(frames==L"none")return {};
    if(frames.empty())frames=L"120,180,240";
    std::set<std::uint64_t> result;
    std::wistringstream list(frames);std::wstring entry;
    while(std::getline(list,entry,L',')) {
        size_t end{};const auto value=std::stoull(entry,&end);
        if(end!=entry.size()||value>1000000)throw std::runtime_error("invalid capture frame");
        result.insert(value);
    }
    return result;
}
}
