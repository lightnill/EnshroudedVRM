#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <optional>

namespace evrm {

// Pure bounded recording decision. This class makes no Vulkan calls. It does
// not establish pipeline identity, resource lifetime, ownership, or coverage.
struct DeferredRenderingInfo {
    static constexpr std::uint32_t max_colors = 8;
    VkRenderingFlags flags = 0;
    VkRect2D render_area{};
    std::uint32_t layer_count = 0, view_mask = 0, color_count = 0;
    std::array<VkRenderingAttachmentInfo, max_colors> colors{};
    bool has_depth = false, has_stencil = false;
    VkRenderingAttachmentInfo depth{}, stencil{};
    // Pointers in this short-lived call view refer only to this owned object.
    // Rebuild after copying/moving; no cached self-referential pointers exist.
    VkRenderingInfo call_view() const noexcept;
};

enum class DeferredRenderingCommand {
    independent_state, candidate_draw, other
};
struct DeferredRenderingRelease {
    DeferredRenderingInfo begin;
    bool preparation_allowed = false;
};

class DeferredRenderingBegin {
public:
    DeferredRenderingBegin() = default;
    DeferredRenderingBegin(const DeferredRenderingBegin&) = delete;
    DeferredRenderingBegin& operator=(const DeferredRenderingBegin&) = delete;
    // external_eligible must include primary recording, family capabilities,
    // inactive unsupported scopes, complete command interception, and pass
    // classification. Unsupported inputs return false for immediate native
    // forwarding. A second begin never replaces an existing pending begin.
    bool try_defer(const VkRenderingInfo&, bool external_eligible) noexcept;
    // The caller forwards independent state normally. Every other command
    // first releases the exact original begin. Only a classified candidate
    // draw may offer preparation before that release is issued downstream.
    std::optional<DeferredRenderingRelease> before(DeferredRenderingCommand) noexcept;
    bool pending() const noexcept { return pending_.has_value(); }
    // Only for native-successful reset/free/discard; never silently discard
    // while continuing the same recording. An empty rendering instance must
    // be released before forwarding its EndRendering.
    void discard() noexcept { pending_.reset(); }
private:
    std::optional<DeferredRenderingInfo> pending_;
};

} // namespace evrm
