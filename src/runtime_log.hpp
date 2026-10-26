#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace evrm {
inline void log_event(const std::string& event) noexcept {
    try {
        static std::mutex mutex;
        static unsigned lines = 0;
        std::lock_guard<std::mutex> guard(mutex);
        wchar_t path[32768]{};
        const auto length = GetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG", path, 32768);
        if (!length || length >= 32768 || lines >= 10000) return;
        std::ofstream output(std::filesystem::path(path), std::ios::app);
        if (!output) return;
        output << GetTickCount64() << " pid=" << GetCurrentProcessId() << ' ' << event.substr(0, 4096) << '\n';
        ++lines;
    } catch (...) {
        // Diagnostics must not change the graphics call's result.
    }
}
inline void log_event(const char* event) noexcept {
    if (!event) return;
    try { log_event(std::string(event)); }
    catch (...) { /* Keep literal conversion inside the diagnostic boundary. */ }
}
// Keep diagnostic formatting/allocation inside the same optional boundary.
template<class Builder> inline void log_event_lazy(Builder&& builder) noexcept {
    try { log_event(builder()); } catch (...) {}
}
}
