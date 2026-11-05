#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace enshrouded::vrm {
enum class InputBootstrapPhase { not_started, paths, policy, manifest, environment, ready };
enum class InputBootstrapResult { not_started, path_query_failed, wrong_process, ready, wrong_directory,
    unsafe_path, disabled, search_override, too_long, already_present, manifest_refused, environment_failed };
// Fixed enum/scalar attach record, reported only after the verified safe gate.
void input_startup_record(InputBootstrapPhase phase,InputBootstrapResult result,DWORD error) noexcept;
// Bounded setup only. No chain loading, hashing, workers or file writes here.
void input_startup_attach(HMODULE own,const wchar_t* own_path,DWORD own_length,
                          const wchar_t* executable,DWORD executable_length) noexcept;
void input_startup_detach() noexcept;
}
