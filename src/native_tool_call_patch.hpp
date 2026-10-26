#pragma once
#include <array>
#include <cstdint>
namespace evrm {
// Exact success-block replacement. Replays 14 known non-relative bytes, invokes
// a callback with R15 (destination), then resumes. The pinned block has RSP%16=0.
// Published relay pages live until process
// exit, even after restoration, because a native thread may have entered one.
class NativeToolCallPatch {
    std::uintptr_t site_=0,wrapper_=0;
    std::array<unsigned char,14> before_{},after_{};
    bool installed_=false;
public:
    bool install(std::uintptr_t site,const std::array<unsigned char,14>& original,std::uintptr_t wrapper) noexcept;
    bool restore() noexcept;
};
}
