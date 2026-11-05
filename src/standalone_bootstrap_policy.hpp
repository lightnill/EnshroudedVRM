#pragma once
#include <cstddef>
#include <string_view>

namespace enshrouded::vrm {
// Zero is the inactive default, keeping the bounded static workspace in BSS
// instead of embedding its large zero-filled arrays as initialized file data.
enum class BootstrapReason { wrong_process, ready, wrong_directory, unsafe_path, disabled, search_override, too_long, already_present };
struct BootstrapPlan {
    wchar_t paths[32768]{};
    wchar_t manifest[32768]{};
    std::size_t manifest_length = 0;
    std::size_t length = 0;
    bool activate = false;
    BootstrapReason reason = BootstrapReason::wrong_process;
};
void prepare_bootstrap_paths(std::wstring_view module_path, std::wstring_view executable_path,
                             std::wstring_view inherited_paths, bool disabled, bool search_override,
                             BootstrapPlan& output) noexcept;
}
