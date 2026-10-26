#pragma once
#include "deferred_rendering.hpp"
#include "device_features.hpp"
#include <array>
#include <mutex>
#include <optional>

namespace evrm {

// Pure interception-coverage gate; it neither enables features nor proves
// pipeline identity, resource readiness, or ownership.
bool rendering_command_coverage_supported(std::uint32_t app_api_version,
                                          const DeviceFeaturesObservation&) noexcept;
bool rendering_scope_sensitive(const char* exact_command_name) noexcept;
bool rendering_independent_state(const char* exact_command_name) noexcept;

struct NativeRenderingRelease {
    VkCommandBuffer command = VK_NULL_HANDLE;
    PFN_vkCmdBeginRendering begin = nullptr;
    DeferredRenderingRelease release;
};

// Bounded recording metadata only. Native execution and the exact command
// flush allowlist belong to the caller. No callback invokes Vulkan.
class DeferredRenderingRuntime {
public:
    static constexpr std::uint32_t max_pools = 128, max_commands = 512;
    DeferredRenderingRuntime() = default;
    DeferredRenderingRuntime(const DeferredRenderingRuntime&) = delete;
    DeferredRenderingRuntime& operator=(const DeferredRenderingRuntime&) = delete;

    void pool_created(VkCommandPool, const VkCommandPoolCreateInfo&, VkQueueFlags actual_family_flags) noexcept;
    void pool_destroyed(VkCommandPool) noexcept;
    void pool_reset(VkCommandPool, VkResult native_result) noexcept;
    void allocated(const VkCommandBufferAllocateInfo&, const VkCommandBuffer*) noexcept;
    void command_begin(VkCommandBuffer, const VkCommandBufferBeginInfo&, VkResult native_result) noexcept;
    void command_reset(VkCommandBuffer, VkResult native_result) noexcept;
    void command_freed(std::uint32_t, const VkCommandBuffer*) noexcept;
    // Flush before native EndCommandBuffer, including its failure path. This
    // notification stops recording but never drops an unflushed begin.
    void command_end(VkCommandBuffer, VkResult native_result) noexcept;
    // Flush an earlier pending begin first. Even a declined native begin
    // establishes virtual rendering scope until rendering_ended.
    bool begin_rendering(VkCommandBuffer, const VkRenderingInfo&,
                         PFN_vkCmdBeginRendering exact_native_entry, bool external_eligible) noexcept;
    std::optional<NativeRenderingRelease> before(VkCommandBuffer, DeferredRenderingCommand,
                                                 bool disallow_later_deferral = false) noexcept;
    void rendering_ended(VkCommandBuffer) noexcept;
    std::optional<std::uint32_t> pose_copy_family(VkCommandBuffer) noexcept;

private:
    struct Pool {
        VkCommandPool handle = VK_NULL_HANDLE;
        bool eligible = false;
        std::uint32_t family=UINT32_MAX;
    };
    struct Command {
        VkCommandBuffer handle = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE;
        bool primary = false, pool_eligible = false, recording = false;
        bool rendering_active = false, tainted = false;
        bool one_time=false;std::uint32_t family=UINT32_MAX;
        PFN_vkCmdBeginRendering begin = nullptr;
        DeferredRenderingBegin pending;
    };
    std::mutex mutex_;
    std::array<Pool, max_pools> pools_{};
    std::array<Command, max_commands> commands_{};
    Pool* find_pool(VkCommandPool) noexcept;
    Command* find_command(VkCommandBuffer) noexcept;
    static void reset_recording(Command&) noexcept;
    static void retire(Command&) noexcept;
    void retire_pool_commands(VkCommandPool) noexcept;
};

} // namespace evrm
