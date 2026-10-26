#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace evrm {
enum class NativeClipKind { none, visibility, visibility_alpha, gbuffer, billboard_visibility, billboard_gbuffer, shadow };
struct NativeClipModule {
    std::vector<std::uint32_t> words;
    std::array<std::uint8_t,32> original_digest{};
    NativeClipKind kind=NativeClipKind::none;
    bool mesh=false;
};
// Exact original-module fingerprints only. Adds private set5/b0 commit UBO
// (uint at byte192) and set5/b1 readonly4096-word bitset. This pure transform
// establishes neither resource lifetime, current ownership nor draw coverage.
bool patch_native_clip_shader(const void*,std::size_t,NativeClipModule&,std::string&) noexcept;
#ifdef EVRM_NATIVE_CLIP_TESTING
// Test executable only: independently authored fixture with the same anchors.
// This symbol is absent from normal production compilation.
bool patch_native_clip_fixture_shader(const void*,std::size_t,NativeClipModule&,std::string&) noexcept;
#endif
}
