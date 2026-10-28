#pragma once
#include "native_owner.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace evrm {
// ID, model@120, skin@108, tag@112 in a zero-based native128 prefix.
using NativeInstanceQualifier=std::array<std::uint32_t,4>;
// Same-transform candidates are not an ownership or current-upload proof.
// Caller owns the readable byte span and proves VkBuffer origin/generation,
// snapshot freshness, current owner revalidation and subsequent GPU agreement.
// Output clears on failure; selected may alias output.
bool collect_native_instance_qualifiers(const void*,std::size_t,
    const std::vector<NativeInstanceQualifier>& selected,
    std::vector<NativeInstanceQualifier>& out,std::string&) noexcept;
// Owner must be trusted unmodified capture output. Filters candidate stamps;
// establishes neither a source lifetime lease nor a CPU/GPU publication epoch.
bool select_native_owner_qualifiers(const NativeOwnerCandidate&,
    const std::vector<NativeInstanceQualifier>& selected,
    const std::vector<NativeInstanceQualifier>& candidates,
    std::vector<NativeInstanceQualifier>& out,std::string&) noexcept;
}
