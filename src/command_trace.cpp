#include "command_trace.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace evrm {
namespace {
template<class T> std::uint64_t number(T value) noexcept {
    if constexpr (std::is_pointer_v<T>)
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
    else return static_cast<std::uint64_t>(value);
}

struct Chain {
    std::vector<std::uint32_t> types;
    const VkTimelineSemaphoreSubmitInfo* timeline = nullptr;
    std::uint64_t issues = 0;
};

// Chain pointers exist only during a callback. Retained metadata owns numeric
// sTypes and explicitly supported scalar fields, never application pointers.
Chain read_chain(const void* first, bool allow_timeline = false) {
    Chain result;
    std::array<const VkBaseInStructure*, CommandTrace::max_pnext_depth> seen{};
    auto node = static_cast<const VkBaseInStructure*>(first);
    std::size_t count = 0;
    while (node) {
        if (std::find(seen.begin(), seen.begin() + count, node) != seen.begin() + count) {
            result.issues |= trace_cycle;
            break;
        }
        if (count == seen.size()) { result.issues |= trace_depth_limit; break; }
        seen[count++] = node;
        result.types.push_back(static_cast<std::uint32_t>(node->sType));
        if (allow_timeline && node->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
            if (result.timeline) result.issues |= trace_invalid_state;
            else result.timeline = reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(node);
        } else result.issues |= trace_unsupported;
        node = node->pNext;
    }
    return result;
}

std::size_t event_units(const CommandTraceEvent& e) noexcept {
    auto units = 1 + e.regions.size() + e.pnext_types.size() +
        e.descriptor_sets.size() + e.dynamic_offsets.size();
    if (e.rendering) {
        units += 1;
        for (const auto& attachment : e.rendering->colors) units += 1 + attachment.pnext_types.size();
        if (e.rendering->has_depth) units += 1 + e.rendering->depth.pnext_types.size();
        if (e.rendering->has_stencil) units += 1 + e.rendering->stencil.pnext_types.size();
    }
    return units;
}
CommandTraceRenderingAttachment rendering_attachment(const VkRenderingAttachmentInfo& info,
                                                     std::uint64_t& issues) {
    CommandTraceRenderingAttachment result;
    if (info.sType != VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO) issues |= trace_invalid_state;
    result.image_view = number(info.imageView);
    result.image_layout = static_cast<std::uint32_t>(info.imageLayout);
    result.resolve_mode = static_cast<std::uint32_t>(info.resolveMode);
    result.resolve_image_view = number(info.resolveImageView);
    result.resolve_image_layout = static_cast<std::uint32_t>(info.resolveImageLayout);
    result.load_op = static_cast<std::uint32_t>(info.loadOp);
    result.store_op = static_cast<std::uint32_t>(info.storeOp);
    static_assert(sizeof(result.clear_value_bits) == sizeof(info.clearValue));
    std::memcpy(result.clear_value_bits.data(), &info.clearValue, sizeof(info.clearValue));
    auto chain = read_chain(info.pNext);
    issues |= chain.issues;
    result.pnext_types = std::move(chain.types);
    return result;
}
} // namespace

struct CommandTrace::Impl {
    enum class State { initial, recording, executable };
    struct Ref { std::uint64_t command = 0, generation = 0; };
    struct Recorded {
        CommandTraceEvent event;
        std::vector<Ref> secondary;
    };
    struct Query { std::uint64_t pool = 0; std::uint32_t query = 0; };
    struct Command {
        std::uint64_t command = 0, generation = 0, pool = 0, issues = 0;
        std::uint32_t family = UINT32_MAX;
        VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        State state = State::initial;
        bool inside_pass = false, inside_conditional = false, has_begin = false, has_end = false;
        std::size_t units = 1;
        CommandTraceEvent begin_event, end_event;
        std::vector<Recorded> events;
        std::vector<Query> active_queries;
    };

    std::mutex mutex;
    // A metadata exception may interrupt a recording. Keep its loss visible
    // across later frames as well as the callback that failed.
    std::atomic<std::uint64_t> sticky_issues{0};
    std::unordered_map<std::uint64_t, Command> commands;
    std::unordered_map<std::uint64_t, std::uint32_t> queues;
    std::uint64_t generation = 0, serial = 0, sequence = 0, issues = 0;
    std::size_t stored_units = 0, frame_units = 0, output_events = 0, output_batches = 0;
    CommandTraceSnapshot frame;

    template<class F> void observe(F&& f) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex);
            f();
        } catch (...) {
            sticky_issues.fetch_or(trace_allocation_failure, std::memory_order_relaxed);
        }
    }
    void mark(std::uint64_t value) noexcept { issues |= value; }
    void mark(Command& command, std::uint64_t value) noexcept {
        command.issues |= value;
        mark(value);
    }
    std::uint64_t new_generation() noexcept {
        if (generation == UINT64_MAX) {
            sticky_issues.fetch_or(trace_overflow, std::memory_order_relaxed);
            return 0;
        }
        return ++generation;
    }
    bool available(std::size_t count) const noexcept {
        return count <= max_stored_units && stored_units <= max_stored_units - count &&
            frame_units <= max_stored_units - count - stored_units;
    }
    bool claim_record(Command& command, std::size_t count) noexcept {
        if (!available(count)) { mark(command, trace_overflow); return false; }
        stored_units += count;
        command.units += count;
        return true;
    }
    bool claim_frame(std::size_t count) noexcept {
        if (!available(count)) { mark(trace_overflow); return false; }
        frame_units += count;
        return true;
    }
    Command* find(std::uint64_t handle) noexcept {
        auto found = commands.find(handle);
        if (found == commands.end()) { mark(trace_missing_information); return nullptr; }
        return &found->second;
    }
    Command* recording(VkCommandBuffer handle) noexcept {
        auto command = find(number(handle));
        if (command && command->state != State::recording) {
            mark(*command, trace_invalid_state);
            return nullptr;
        }
        return command;
    }
    CommandTraceEvent event(const Command& command, CommandTraceKind kind) const {
        CommandTraceEvent result;
        result.kind = kind;
        result.command = command.command;
        result.generation = command.generation;
        result.queue_family = command.family;
        result.level = command.level;
        return result;
    }
    void append(Command& command, CommandTraceEvent e, std::vector<Ref> refs = {}) {
        if (command.events.size() >= max_events_per_command) { mark(command, trace_overflow); return; }
        if (!claim_record(command, event_units(e) + refs.size())) return;
        command.events.push_back(Recorded{std::move(e), std::move(refs)});
    }
    void simple(VkCommandBuffer handle, CommandTraceKind kind,
                std::array<std::uint64_t, 8> args) {
        auto command = recording(handle);
        if (!command) return;
        auto e = event(*command, kind);
        e.args = args;
        append(*command, std::move(e));
    }
    void clear(Command& command) noexcept {
        stored_units -= command.units - 1;
        const auto id = command.command, pool = command.pool;
        const auto family = command.family;
        const auto level = command.level;
        command = Command{};
        command.command = id;
        command.pool = pool;
        command.family = family;
        command.level = level;
        command.generation = new_generation();
    }
    void clear_pool(std::uint64_t pool, bool destroy) {
        for (auto it = commands.begin(); it != commands.end();) {
            if (it->second.pool != pool) { ++it; continue; }
            if (destroy) {
                stored_units -= it->second.units;
                it = commands.erase(it);
            } else {
                clear(it->second);
                ++it;
            }
        }
    }
    bool output(CommandTraceBatch& batch, const CommandTraceEvent& e) {
        if (output_events == max_output_events) { mark(trace_overflow); return false; }
        if (!claim_frame(event_units(e))) return false;
        batch.events.push_back(e);
        ++output_events;
        return true;
    }
    void flatten(CommandTraceBatch& batch, Ref ref, std::uint32_t family,
                 std::vector<Ref>& stack, bool secondary) {
        const auto found = commands.find(ref.command);
        if (found == commands.end()) {
            mark(trace_missing_information | (secondary ? static_cast<std::uint64_t>(trace_stale_secondary) : 0));
            return;
        }
        const auto& command = found->second;
        if (command.generation != ref.generation) { mark(trace_stale_secondary); return; }
        if (std::find_if(stack.begin(), stack.end(), [&](const Ref& item) {
            return item.command == ref.command && item.generation == ref.generation;
        }) != stack.end()) { mark(trace_cycle); return; }
        if (stack.size() > max_secondary_depth) { mark(trace_depth_limit); return; }
        if (secondary && command.level != VK_COMMAND_BUFFER_LEVEL_SECONDARY) { mark(trace_invalid_state); return; }
        if (!secondary && command.level != VK_COMMAND_BUFFER_LEVEL_PRIMARY) { mark(trace_invalid_state); return; }
        if (family != UINT32_MAX && command.family != family) { mark(trace_family_mismatch); return; }
        mark(command.issues);
        if (command.state != State::executable || !command.has_begin || !command.has_end) {
            mark(trace_invalid_state);
            return;
        }
        stack.push_back(ref);
        if (!output(batch, command.begin_event)) { stack.pop_back(); return; }
        for (const auto& recorded : command.events) {
            if (!output(batch, recorded.event)) break;
            for (const auto& child : recorded.secondary) {
                flatten(batch, child, family, stack, true);
                if (output_events == max_output_events) break;
            }
            if (output_events == max_output_events) break;
        }
        output(batch, command.end_event);
        stack.pop_back();
    }
    CommandTraceSubmission* start_submit(VkQueue queue, bool submit2) {
        if (!claim_frame(1)) return nullptr;
        CommandTraceSubmission submission;
        if (serial == UINT64_MAX) { mark(trace_overflow); return nullptr; }
        submission.serial = ++serial;
        submission.queue = number(queue);
        submission.submit2 = submit2;
        const auto known = queues.find(number(queue));
        if (known == queues.end()) mark(trace_missing_information);
        else submission.queue_family = known->second;
        frame.submissions.push_back(std::move(submission));
        return &frame.submissions.back();
    }
    CommandTraceBatch* start_batch(CommandTraceSubmission& submission) {
        if (output_batches == max_submit_batches) { mark(trace_overflow); return nullptr; }
        if (!claim_frame(1)) return nullptr;
        submission.batches.emplace_back();
        ++output_batches;
        return &submission.batches.back();
    }
    void batch_chain(CommandTraceBatch& batch, const Chain& chain) {
        mark(chain.issues);
        if (claim_frame(chain.types.size())) batch.pnext_types = chain.types;
    }
    void command_ref(CommandTraceBatch& batch, CommandTraceCommandRef ref,
                     std::uint32_t family) {
        const auto found = commands.find(ref.command);
        if (found == commands.end()) mark(trace_missing_information);
        else ref.generation = found->second.generation;
        if (!claim_frame(1 + ref.pnext_types.size())) return;
        batch.commands.push_back(ref);
        std::vector<Ref> stack;
        flatten(batch, Ref{ref.command, ref.generation}, family, stack, false);
    }
    bool copy_semaphore(CommandTraceBatch& batch, CommandTraceSemaphore semaphore, bool signal) {
        if (!claim_frame(1 + semaphore.pnext_types.size())) return false;
        (signal ? batch.signals : batch.waits).push_back(std::move(semaphore));
        return true;
    }
    void legacy_batch(CommandTraceSubmission& submission, const VkSubmitInfo& info) {
        auto batch = start_batch(submission);
        if (!batch) return;
        if (info.sType != VK_STRUCTURE_TYPE_SUBMIT_INFO) mark(trace_invalid_state);
        const auto chain = read_chain(info.pNext, true);
        batch_chain(*batch, chain);
        const auto timeline = chain.timeline;
        bool wait_values = false, signal_values = false;
        if (timeline) {
            wait_values = timeline->waitSemaphoreValueCount == info.waitSemaphoreCount &&
                (!info.waitSemaphoreCount || timeline->pWaitSemaphoreValues);
            signal_values = timeline->signalSemaphoreValueCount == info.signalSemaphoreCount &&
                (!info.signalSemaphoreCount || timeline->pSignalSemaphoreValues);
            if (!wait_values || !signal_values) mark(trace_missing_information);
        }
        if (info.waitSemaphoreCount > max_semaphores_per_batch ||
            info.signalSemaphoreCount > max_semaphores_per_batch ||
            info.commandBufferCount > max_commands) mark(trace_overflow);
        if (info.waitSemaphoreCount && (!info.pWaitSemaphores || !info.pWaitDstStageMask)) mark(trace_missing_information);
        if (info.signalSemaphoreCount && !info.pSignalSemaphores) mark(trace_missing_information);
        if (info.commandBufferCount && !info.pCommandBuffers) mark(trace_missing_information);
        if (info.pWaitSemaphores) {
            for (std::uint32_t i = 0; i < std::min(info.waitSemaphoreCount, max_semaphores_per_batch); ++i) {
                CommandTraceSemaphore semaphore;
                semaphore.semaphore = number(info.pWaitSemaphores[i]);
                semaphore.has_stage_mask = info.pWaitDstStageMask != nullptr;
                if (semaphore.has_stage_mask) semaphore.stage_mask = info.pWaitDstStageMask[i];
                semaphore.has_value = timeline && wait_values;
                if (semaphore.has_value) semaphore.value = timeline->pWaitSemaphoreValues[i];
                if (!copy_semaphore(*batch, std::move(semaphore), false)) break;
            }
        }
        if (info.pSignalSemaphores) {
            for (std::uint32_t i = 0; i < std::min(info.signalSemaphoreCount, max_semaphores_per_batch); ++i) {
                CommandTraceSemaphore semaphore;
                semaphore.semaphore = number(info.pSignalSemaphores[i]);
                semaphore.has_value = timeline && signal_values;
                if (semaphore.has_value) semaphore.value = timeline->pSignalSemaphoreValues[i];
                if (!copy_semaphore(*batch, std::move(semaphore), true)) break;
            }
        }
        if (info.pCommandBuffers) {
            for (std::uint32_t i = 0; i < std::min(info.commandBufferCount, max_commands); ++i) {
                CommandTraceCommandRef ref;
                ref.command = number(info.pCommandBuffers[i]);
                command_ref(*batch, std::move(ref), submission.queue_family);
            }
        }
    }
    void modern_batch(CommandTraceSubmission& submission, const VkSubmitInfo2& info) {
        auto batch = start_batch(submission);
        if (!batch) return;
        if (info.sType != VK_STRUCTURE_TYPE_SUBMIT_INFO_2) mark(trace_invalid_state);
        batch->flags = info.flags;
        batch_chain(*batch, read_chain(info.pNext));
        if (info.waitSemaphoreInfoCount > max_semaphores_per_batch ||
            info.signalSemaphoreInfoCount > max_semaphores_per_batch ||
            info.commandBufferInfoCount > max_commands) mark(trace_overflow);
        if (info.waitSemaphoreInfoCount && !info.pWaitSemaphoreInfos) mark(trace_missing_information);
        if (info.signalSemaphoreInfoCount && !info.pSignalSemaphoreInfos) mark(trace_missing_information);
        if (info.commandBufferInfoCount && !info.pCommandBufferInfos) mark(trace_missing_information);
        const auto semaphores = [&](std::uint32_t count, const VkSemaphoreSubmitInfo* infos, bool signal) {
            if (!infos) return;
            for (std::uint32_t i = 0; i < std::min(count, max_semaphores_per_batch); ++i) {
                if (infos[i].sType != VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO) mark(trace_invalid_state);
                const auto chain = read_chain(infos[i].pNext);
                mark(chain.issues);
                CommandTraceSemaphore semaphore;
                semaphore.semaphore = number(infos[i].semaphore);
                semaphore.stage_mask = infos[i].stageMask;
                semaphore.value = infos[i].value;
                semaphore.device_index = infos[i].deviceIndex;
                semaphore.has_stage_mask = true;
                semaphore.has_value = true;
                semaphore.pnext_types = chain.types;
                if (!copy_semaphore(*batch, std::move(semaphore), signal)) break;
            }
        };
        semaphores(info.waitSemaphoreInfoCount, info.pWaitSemaphoreInfos, false);
        semaphores(info.signalSemaphoreInfoCount, info.pSignalSemaphoreInfos, true);
        if (info.pCommandBufferInfos) {
            for (std::uint32_t i = 0; i < std::min(info.commandBufferInfoCount, max_commands); ++i) {
                const auto& input = info.pCommandBufferInfos[i];
                if (input.sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO) mark(trace_invalid_state);
                const auto chain = read_chain(input.pNext);
                mark(chain.issues);
                CommandTraceCommandRef ref;
                ref.command = number(input.commandBuffer);
                ref.device_mask = input.deviceMask;
                ref.pnext_types = chain.types;
                command_ref(*batch, std::move(ref), submission.queue_family);
            }
        }
    }
};

CommandTrace::CommandTrace() noexcept {
    try { impl_ = std::make_unique<Impl>(); } catch (...) {}
}
CommandTrace::~CommandTrace() = default;

void CommandTrace::queue_known(VkQueue queue, std::uint32_t family) noexcept {
    if (impl_) impl_->observe([&] {
        if (!queue || family == UINT32_MAX) { impl_->mark(trace_missing_information); return; }
        const auto found = impl_->queues.find(number(queue));
        if (found != impl_->queues.end()) {
            if (found->second != family) impl_->mark(trace_invalid_state);
            return;
        }
        if (impl_->queues.size() == max_commands || !impl_->available(1)) { impl_->mark(trace_overflow); return; }
        impl_->queues.emplace(number(queue), family);
        ++impl_->stored_units;
    });
}
void CommandTrace::allocated(const VkCommandBufferAllocateInfo& info, const VkCommandBuffer* handles,
                             std::uint32_t family) noexcept {
    if (impl_) impl_->observe([&] {
        if (info.commandBufferCount && !handles) { impl_->mark(trace_missing_information); return; }
        if (info.commandBufferCount > max_commands) impl_->mark(trace_overflow);
        for (std::uint32_t i = 0; i < std::min(info.commandBufferCount, max_commands); ++i) {
            const auto id = number(handles[i]);
            if (!id) { impl_->mark(trace_missing_information); continue; }
            auto found = impl_->commands.find(id);
            if (found != impl_->commands.end()) {
                impl_->stored_units -= found->second.units;
                impl_->commands.erase(found);
            }
            if (impl_->commands.size() == max_commands || !impl_->available(1)) { impl_->mark(trace_overflow); continue; }
            Impl::Command command;
            command.command = id;
            command.pool = number(info.commandPool);
            command.family = family;
            command.level = info.level;
            command.generation = impl_->new_generation();
            if (info.sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO ||
                (info.level != VK_COMMAND_BUFFER_LEVEL_PRIMARY && info.level != VK_COMMAND_BUFFER_LEVEL_SECONDARY))
                impl_->mark(command, trace_invalid_state);
            if (family == UINT32_MAX) impl_->mark(command, trace_missing_information);
            impl_->commands.emplace(id, std::move(command));
            ++impl_->stored_units;
        }
    });
}
void CommandTrace::begin(VkCommandBuffer handle, const VkCommandBufferBeginInfo& info) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->find(number(handle));
        if (!command) return;
        impl_->clear(*command);
        command->state = Impl::State::recording;
        auto e = impl_->event(*command, CommandTraceKind::command_begin);
        e.args[0] = info.flags;
        e.args[1] = static_cast<std::uint64_t>(command->level);
        e.args[2] = command->pool;
        if (info.sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO) impl_->mark(*command, trace_invalid_state);
        if (command->family == UINT32_MAX) impl_->mark(*command, trace_missing_information);
        auto chain = read_chain(info.pNext);
        impl_->mark(*command, chain.issues);
        e.pnext_types = std::move(chain.types);
        // Vulkan ignores pInheritanceInfo for a primary command buffer, so
        // even a nonnull value there must not be inspected by observation.
        if (command->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY && info.pInheritanceInfo) {
            const auto& inheritance = *info.pInheritanceInfo;
            if (inheritance.sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO) impl_->mark(*command, trace_invalid_state);
            e.args[3] = number(inheritance.renderPass);
            e.args[4] = number(inheritance.framebuffer);
            e.args[5] = inheritance.subpass;
            e.args[6] = inheritance.queryFlags;
            e.args[7] = static_cast<std::uint64_t>(inheritance.pipelineStatistics) |
                (static_cast<std::uint64_t>(inheritance.occlusionQueryEnable) << 32);
            chain = read_chain(inheritance.pNext);
            impl_->mark(*command, chain.issues);
            e.pnext_types.insert(e.pnext_types.end(), chain.types.begin(), chain.types.end());
        } else if (command->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) impl_->mark(*command, trace_missing_information);
        if (impl_->claim_record(*command, event_units(e))) {
            command->begin_event = std::move(e);
            command->has_begin = true;
        }
    });
}
void CommandTrace::end(VkCommandBuffer handle) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        if (command->inside_pass || command->inside_conditional || !command->active_queries.empty())
            impl_->mark(*command, trace_invalid_state);
        if (impl_->claim_record(*command, 1)) {
            command->end_event = impl_->event(*command, CommandTraceKind::command_end);
            command->has_end = true;
        }
        command->state = Impl::State::executable;
    });
}
void CommandTrace::reset(VkCommandBuffer handle) noexcept {
    if (impl_) impl_->observe([&] { if (auto command = impl_->find(number(handle))) impl_->clear(*command); });
}
void CommandTrace::freed(VkCommandPool pool, std::uint32_t count, const VkCommandBuffer* handles) noexcept {
    if (impl_) impl_->observe([&] {
        if (count && !handles) { impl_->mark(trace_missing_information); return; }
        if (count > max_commands) { impl_->mark(trace_overflow); impl_->clear_pool(number(pool), true); return; }
        for (std::uint32_t i = 0; i < count; ++i) {
            auto found = impl_->commands.find(number(handles[i]));
            if (found == impl_->commands.end()) { impl_->mark(trace_missing_information); continue; }
            if (found->second.pool != number(pool)) impl_->mark(trace_invalid_state);
            impl_->stored_units -= found->second.units;
            impl_->commands.erase(found);
        }
    });
}
void CommandTrace::pool_reset(VkCommandPool pool) noexcept {
    if (impl_) impl_->observe([&] { impl_->clear_pool(number(pool), false); });
}
void CommandTrace::pool_destroyed(VkCommandPool pool) noexcept {
    if (impl_) impl_->observe([&] { impl_->clear_pool(number(pool), true); });
}
void CommandTrace::pass_begin(VkCommandBuffer handle, VkRenderPass pass, VkFramebuffer framebuffer,
                              VkSubpassContents contents, CommandTracePass kind, std::uint32_t flags) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        if (command->inside_pass) impl_->mark(*command, trace_invalid_state);
        command->inside_pass = true;
        if (kind != CommandTracePass::legacy && kind != CommandTracePass::render_pass2) impl_->mark(*command, trace_unsupported);
        auto e = impl_->event(*command, CommandTraceKind::pass_begin);
        e.args = {static_cast<std::uint64_t>(kind), number(pass), number(framebuffer), static_cast<std::uint64_t>(contents), flags, 0, 0, 0};
        impl_->append(*command, std::move(e));
    });
}
void CommandTrace::next_subpass(VkCommandBuffer handle, VkSubpassContents contents, CommandTracePass kind) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        if (!command->inside_pass) impl_->mark(*command, trace_invalid_state);
        if (kind != CommandTracePass::legacy && kind != CommandTracePass::render_pass2) impl_->mark(*command, trace_unsupported);
        auto e = impl_->event(*command, CommandTraceKind::next_subpass);
        e.args = {static_cast<std::uint64_t>(kind), static_cast<std::uint64_t>(contents), 0, 0, 0, 0, 0, 0};
        impl_->append(*command, std::move(e));
    });
}
void CommandTrace::pass_end(VkCommandBuffer handle, CommandTracePass kind) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        if (!command->inside_pass) impl_->mark(*command, trace_invalid_state);
        command->inside_pass = false;
        if (kind != CommandTracePass::legacy && kind != CommandTracePass::render_pass2) impl_->mark(*command, trace_unsupported);
        auto e = impl_->event(*command, CommandTraceKind::pass_end);
        e.args[0] = static_cast<std::uint64_t>(kind);
        impl_->append(*command, std::move(e));
    });
}
void CommandTrace::bind_descriptor_sets(VkCommandBuffer handle, VkPipelineBindPoint point,
    VkPipelineLayout layout, std::uint32_t first_set, std::uint32_t set_count,
    const VkDescriptorSet* sets, std::uint32_t dynamic_count, const std::uint32_t* offsets) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        auto e = impl_->event(*command, CommandTraceKind::descriptor_sets_bind);
        e.args = {static_cast<std::uint64_t>(point), number(layout), first_set, set_count, dynamic_count, 0, 0, 0};
        if (point != VK_PIPELINE_BIND_POINT_GRAPHICS && point != VK_PIPELINE_BIND_POINT_COMPUTE)
            impl_->mark(*command, trace_unsupported);
        if (static_cast<std::uint64_t>(first_set) + set_count > static_cast<std::uint64_t>(UINT32_MAX) + 1)
            impl_->mark(*command, trace_overflow);
        if (set_count > max_descriptor_sets) impl_->mark(*command, trace_overflow);
        else if (set_count && !sets) impl_->mark(*command, trace_missing_information);
        else for (std::uint32_t i = 0; i < set_count; ++i) e.descriptor_sets.push_back(number(sets[i]));
        if (dynamic_count > max_dynamic_offsets) impl_->mark(*command, trace_overflow);
        else if (dynamic_count && !offsets) impl_->mark(*command, trace_missing_information);
        else if (dynamic_count) e.dynamic_offsets.assign(offsets, offsets + dynamic_count);
        impl_->append(*command, std::move(e));
    });
}
void CommandTrace::begin_rendering(VkCommandBuffer handle, const VkRenderingInfo& info) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        if (command->inside_pass) impl_->mark(*command, trace_invalid_state);
        command->inside_pass = true;
        // Immediate attachment metadata does not resolve the rendering state,
        // extension semantics, image lifetime, layouts, or synchronization.
        impl_->mark(*command, trace_unsupported);
        if (info.sType != VK_STRUCTURE_TYPE_RENDERING_INFO) impl_->mark(*command, trace_invalid_state);
        auto e = impl_->event(*command, CommandTraceKind::rendering_begin);
        e.args = {info.flags, info.colorAttachmentCount, info.layerCount, info.viewMask, 0, 0, 0, 0};
        auto chain = read_chain(info.pNext);
        impl_->mark(*command, chain.issues);
        e.pnext_types = std::move(chain.types);
        CommandTraceRendering rendering;
        rendering.flags = info.flags;
        rendering.render_area = info.renderArea;
        rendering.layer_count = info.layerCount;
        rendering.view_mask = info.viewMask;
        rendering.color_attachment_count = info.colorAttachmentCount;
        std::uint64_t attachment_issues = 0;
        if (info.colorAttachmentCount > max_rendering_attachments) impl_->mark(*command, trace_overflow);
        else if (info.colorAttachmentCount && !info.pColorAttachments) impl_->mark(*command, trace_missing_information);
        else for (std::uint32_t i = 0; i < info.colorAttachmentCount; ++i)
            rendering.colors.push_back(rendering_attachment(info.pColorAttachments[i], attachment_issues));
        if (info.pDepthAttachment) {
            rendering.has_depth = true;
            rendering.depth = rendering_attachment(*info.pDepthAttachment, attachment_issues);
        }
        if (info.pStencilAttachment) {
            rendering.has_stencil = true;
            rendering.stencil = rendering_attachment(*info.pStencilAttachment, attachment_issues);
        }
        impl_->mark(*command, attachment_issues);
        e.rendering = std::move(rendering);
        impl_->append(*command, std::move(e));
    });
}
void CommandTrace::query_begin(VkCommandBuffer handle, VkQueryPool pool, std::uint32_t query,
                               VkQueryControlFlags flags) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        const auto duplicate = std::find_if(command->active_queries.begin(), command->active_queries.end(),
            [&](const Impl::Query& entry) { return entry.pool == number(pool) && entry.query == query; });
        if (duplicate != command->active_queries.end()) impl_->mark(*command, trace_invalid_state);
        else if (command->active_queries.size() == max_scope_queries) impl_->mark(*command, trace_overflow);
        else if (impl_->claim_record(*command, 1)) command->active_queries.push_back({number(pool), query});
        auto e = impl_->event(*command, CommandTraceKind::query_begin);
        e.args = {number(pool), query, flags, 0, 0, 0, 0, 0};
        impl_->append(*command, std::move(e));
    });
}
void CommandTrace::query_end(VkCommandBuffer handle, VkQueryPool pool, std::uint32_t query) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        const auto active = std::find_if(command->active_queries.begin(), command->active_queries.end(),
            [&](const Impl::Query& entry) { return entry.pool == number(pool) && entry.query == query; });
        if (active == command->active_queries.end()) impl_->mark(*command, trace_invalid_state);
        else {
            command->active_queries.erase(active);
            --command->units;
            --impl_->stored_units;
        }
        auto e = impl_->event(*command, CommandTraceKind::query_end);
        e.args = {number(pool), query, 0, 0, 0, 0, 0, 0};
        impl_->append(*command, std::move(e));
    });
}
void CommandTrace::conditional_begin(VkCommandBuffer handle,
                                      const VkConditionalRenderingBeginInfoEXT& info) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        if (command->inside_conditional) impl_->mark(*command, trace_invalid_state);
        command->inside_conditional = true;
        if (info.sType != VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT)
            impl_->mark(*command, trace_invalid_state);
        auto e = impl_->event(*command, CommandTraceKind::conditional_begin);
        e.args = {number(info.buffer), info.offset, info.flags, 0, 0, 0, 0, 0};
        auto chain = read_chain(info.pNext);
        impl_->mark(*command, chain.issues);
        e.pnext_types = std::move(chain.types);
        impl_->append(*command, std::move(e));
    });
}
void CommandTrace::conditional_end(VkCommandBuffer handle) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(handle);
        if (!command) return;
        if (!command->inside_conditional) impl_->mark(*command, trace_invalid_state);
        command->inside_conditional = false;
        impl_->append(*command, impl_->event(*command, CommandTraceKind::conditional_end));
    });
}
void CommandTrace::pipeline_bound(VkCommandBuffer c, VkPipelineBindPoint point, VkPipeline pipeline) noexcept {
    if (impl_) impl_->observe([&] { impl_->simple(c, CommandTraceKind::pipeline_bind, {static_cast<std::uint64_t>(point), number(pipeline), 0, 0, 0, 0, 0, 0}); });
}
void CommandTrace::dispatch(VkCommandBuffer c, std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    if (impl_) impl_->observe([&] { impl_->simple(c, CommandTraceKind::dispatch, {x, y, z, 0, 0, 0, 0, 0}); });
}
void CommandTrace::dispatch_base(VkCommandBuffer c, std::uint32_t bx, std::uint32_t by, std::uint32_t bz,
                                std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    if (impl_) impl_->observe([&] { impl_->simple(c, CommandTraceKind::dispatch_base, {bx, by, bz, x, y, z, 0, 0}); });
}
void CommandTrace::dispatch_indirect(VkCommandBuffer c, VkBuffer buffer, VkDeviceSize offset) noexcept {
    if (impl_) impl_->observe([&] { impl_->simple(c, CommandTraceKind::dispatch_indirect, {number(buffer), offset, 0, 0, 0, 0, 0, 0}); });
}
void CommandTrace::draw_indexed_indirect(VkCommandBuffer c, VkBuffer buffer, VkDeviceSize offset,
                                        std::uint32_t count, std::uint32_t stride, VkBuffer count_buffer,
                                        VkDeviceSize count_offset) noexcept {
    if (impl_) impl_->observe([&] { impl_->simple(c, count_buffer ? CommandTraceKind::draw_indexed_indirect_count : CommandTraceKind::draw_indexed_indirect,
        {number(buffer), offset, count, stride, number(count_buffer), count_offset, 0, 0}); });
}
void CommandTrace::copy_buffer(VkCommandBuffer c, VkBuffer source, VkBuffer destination,
                               std::uint32_t count, const VkBufferCopy* regions) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(c);
        if (!command) return;
        auto e = impl_->event(*command, CommandTraceKind::copy_buffer);
        e.args = {number(source), number(destination), count, 0, 0, 0, 0, 0};
        if (count > max_transfer_regions) impl_->mark(*command, trace_overflow);
        if (count && !regions) impl_->mark(*command, trace_missing_information);
        if (regions) for (std::uint32_t i = 0; i < std::min(count, max_transfer_regions); ++i)
            e.regions.push_back({regions[i].srcOffset, regions[i].dstOffset, regions[i].size});
        impl_->append(*command, std::move(e));
    });
}
void CommandTrace::fill_buffer(VkCommandBuffer c, VkBuffer buffer, VkDeviceSize offset,
                               VkDeviceSize size, std::uint32_t data) noexcept {
    if (impl_) impl_->observe([&] { impl_->simple(c, CommandTraceKind::fill_buffer, {number(buffer), offset, size, data, 0, 0, 0, 0}); });
}
void CommandTrace::update_buffer(VkCommandBuffer c, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) noexcept {
    if (impl_) impl_->observe([&] { impl_->simple(c, CommandTraceKind::update_buffer, {number(buffer), offset, size, 0, 0, 0, 0, 0}); });
}
void CommandTrace::execute(VkCommandBuffer c, std::uint32_t count, const VkCommandBuffer* handles) noexcept {
    if (impl_) impl_->observe([&] {
        auto command = impl_->recording(c);
        if (!command) return;
        auto e = impl_->event(*command, CommandTraceKind::execute_secondary);
        e.args[0] = count;
        std::vector<Impl::Ref> refs;
        if (count > max_commands) impl_->mark(*command, trace_overflow);
        if (count && !handles) impl_->mark(*command, trace_missing_information);
        if (handles) for (std::uint32_t i = 0; i < std::min(count, max_commands); ++i) {
            const auto child = impl_->commands.find(number(handles[i]));
            refs.push_back({number(handles[i]), child == impl_->commands.end() ? 0 : child->second.generation});
            if (child == impl_->commands.end()) impl_->mark(*command, trace_missing_information);
        }
        impl_->append(*command, std::move(e), std::move(refs));
    });
}
void CommandTrace::unsupported(VkCommandBuffer c, std::uint64_t tag) noexcept {
    if (impl_) impl_->observe([&] {
        if (auto command = impl_->recording(c)) {
            impl_->mark(*command, trace_unsupported);
            auto e = impl_->event(*command, CommandTraceKind::unsupported);
            e.args[0] = tag;
            impl_->append(*command, std::move(e));
        }
    });
}
void CommandTrace::submitted(VkQueue queue, std::uint32_t count, const VkSubmitInfo* infos) noexcept {
    if (impl_) impl_->observe([&] {
        auto submission = impl_->start_submit(queue, false);
        if (!submission) return;
        if (count > max_submit_batches) impl_->mark(trace_overflow);
        if (count && !infos) { impl_->mark(trace_missing_information); return; }
        for (std::uint32_t i = 0; i < std::min(count, max_submit_batches); ++i) impl_->legacy_batch(*submission, infos[i]);
    });
}
void CommandTrace::submitted2(VkQueue queue, std::uint32_t count, const VkSubmitInfo2* infos) noexcept {
    if (impl_) impl_->observe([&] {
        auto submission = impl_->start_submit(queue, true);
        if (!submission) return;
        if (count > max_submit_batches) impl_->mark(trace_overflow);
        if (count && !infos) { impl_->mark(trace_missing_information); return; }
        for (std::uint32_t i = 0; i < std::min(count, max_submit_batches); ++i) impl_->modern_batch(*submission, infos[i]);
    });
}
CommandTraceSnapshot CommandTrace::finish_frame() noexcept {
    CommandTraceSnapshot result;
    if (!impl_) {
        result.complete = false;
        result.issues = trace_allocation_failure;
        return result;
    }
    impl_->observe([&] {
        result = std::move(impl_->frame);
        impl_->frame = {};
        result.sequence = ++impl_->sequence;
        result.issues = impl_->issues | impl_->sticky_issues.load(std::memory_order_relaxed);
        result.complete = result.issues == 0;
        impl_->issues = 0;
        impl_->frame_units = 0;
        impl_->output_events = 0;
        impl_->output_batches = 0;
    });
    const auto sticky = impl_->sticky_issues.load(std::memory_order_relaxed);
    result.issues |= sticky;
    result.complete = result.complete && result.issues == 0;
    return result;
}
} // namespace evrm
