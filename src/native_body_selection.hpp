#pragma once
#include "native_owner.hpp"
#include "native_menu_owner.hpp"
#include "native_state.hpp"
#include <array>

namespace evrm {
struct NativeBodyMask {
    std::array<std::uint32_t,4096> words{};
    std::uint32_t selected=0;
};
// A captured, unmodified owner candidate is a prerequisite. This only links
// its owned model IDs to one current buffer observation and retarget list;
// it establishes no GPU completion, lifetime lease or complete pass coverage.
bool make_native_body_mask(const NativeOwnerCandidate&,
    const std::vector<std::array<std::uint32_t,4>>& retarget_parts,
    const NativeBufferBinding&,NativeBodyMask&,std::string&) noexcept;
// Menu selection contains only catalog-identified parts. Coincident transforms
// never extend this selection. Revalidate the separate menu context at submit.
bool make_native_menu_body_mask(const NativeMenuOwnerCandidate&,
    const std::vector<std::array<std::uint32_t,4>>& parts,
    const std::vector<std::array<std::uint32_t,4>>& candidates,
    const NativeBufferBinding&,NativeBodyMask&,
    std::vector<std::array<std::uint32_t,4>>& qualified,std::string&) noexcept;
}
