#include "standalone_bootstrap_policy.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace enshrouded::vrm {
namespace {
bool equal(std::wstring_view left, std::wstring_view right) noexcept {
    return left.size() == right.size() && CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}
bool local_absolute(std::wstring_view path) noexcept {
    if (path.size() > 4 && path.substr(0,4) == L"\\\\?\\") path.remove_prefix(4);
    if (path.size() < 4 || !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) || path[1] != L':' || path[2] != L'\\') return false;
    for (std::size_t start = 3; start < path.size();) {
        auto end = path.find(L'\\', start); if (end == path.npos) end = path.size();
        const auto part = path.substr(start, end-start);
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ') return false;
        for (const auto c : part) if (c < 32 || c == L';' || c == L'/' || c == L':' || c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == L'?' || c == L'*') return false;
        start = end+1;
    }
    return true;
}
}
void prepare_bootstrap_paths(std::wstring_view module_path, std::wstring_view executable_path,
                             std::wstring_view inherited_paths, bool disabled, bool search_override,
                             BootstrapPlan& out) noexcept {
    out.length = 0; out.manifest_length = 0; out.activate = false;
    if (module_path.size() >= 32768 || executable_path.size() >= 32768 || inherited_paths.size() >= 32768) { out.reason = BootstrapReason::too_long; return; }
    if (disabled) { out.reason = BootstrapReason::disabled; return; }
    if (search_override) { out.reason = BootstrapReason::search_override; return; }
    const auto exe_slash = executable_path.rfind(L'\\'), module_slash = module_path.rfind(L'\\');
    if (exe_slash == executable_path.npos || !equal(executable_path.substr(exe_slash+1), L"enshrouded.exe")) { out.reason = BootstrapReason::wrong_process; return; }
    if (module_slash == module_path.npos || !equal(module_path.substr(module_slash+1), L"vulkan-1.dll") || !equal(module_path.substr(0,module_slash), executable_path.substr(0,exe_slash))) { out.reason = BootstrapReason::wrong_directory; return; }
    if (!local_absolute(module_path) || !local_absolute(executable_path)) { out.reason = BootstrapReason::unsafe_path; return; }
    constexpr std::wstring_view suffix = L"\\EnshroudedVRM\\Runtime\\enshrouded_vrm_layer.json";
    const auto size = module_slash + suffix.size();
    if (size >= 32768) { out.reason = BootstrapReason::too_long; return; }
    for (std::size_t i = 0; i < module_slash; ++i) out.manifest[i] = module_path[i];
    for (std::size_t i = 0; i < suffix.size(); ++i) out.manifest[module_slash+i] = suffix[i];
    out.manifest[size] = 0; out.manifest_length = size;
    const std::wstring_view target(out.manifest,size);
    for (std::size_t start = 0; start < inherited_paths.size();) {
        auto end = inherited_paths.find(L';',start); if (end == inherited_paths.npos) end = inherited_paths.size();
        auto entry = inherited_paths.substr(start,end-start);
        while (!entry.empty() && (entry.back() == L'\\' || entry.back() == L'/')) entry.remove_suffix(1);
        if (equal(entry,target)) { out.reason = BootstrapReason::already_present; return; }
        start = end+1;
    }
    const auto separator = !inherited_paths.empty() && inherited_paths.back() != L';' ? 1u : 0u;
    if (inherited_paths.size() + separator + size >= 32768) { out.reason = BootstrapReason::too_long; return; }
    for (const auto c : inherited_paths) out.paths[out.length++] = c;
    if (separator) out.paths[out.length++] = L';';
    for (const auto c : target) out.paths[out.length++] = c;
    out.paths[out.length] = 0; out.activate = true; out.reason = BootstrapReason::ready;
}
}
