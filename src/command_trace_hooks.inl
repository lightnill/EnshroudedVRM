// Optional metadata observation only. Native arguments/results are preserved.
#define EVRM_TRACE_HOOK_NAMES(X) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkResetCommandPool) \
    X(vkAllocateCommandBuffers) X(vkEndCommandBuffer) \
    X(vkCmdNextSubpass) X(vkCmdNextSubpass2) X(vkCmdNextSubpass2KHR) \
    X(vkCmdCopyBuffer) X(vkCmdCopyBuffer2) X(vkCmdCopyBuffer2KHR) \
    X(vkCmdFillBuffer) X(vkCmdUpdateBuffer) \
    X(vkCmdBeginQuery) X(vkCmdEndQuery) \
    X(vkCmdBeginConditionalRenderingEXT) X(vkCmdEndConditionalRenderingEXT)

EVRM_EXPORT void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer command, VkQueryPool pool, uint32_t query, VkQueryControlFlags flags) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdBeginQuery>(state, "vkCmdBeginQuery");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, pool, query, flags);
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().query_begin(command, pool, query, flags); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer command, VkQueryPool pool, uint32_t query) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdEndQuery>(state, "vkCmdEndQuery");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, pool, query);
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().query_end(command, pool, query); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdBeginConditionalRenderingEXT(VkCommandBuffer command, const VkConditionalRenderingBeginInfoEXT* info) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdBeginConditionalRenderingEXT>(state, "vkCmdBeginConditionalRenderingEXT");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, info);
    if (info) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().conditional_begin(command, *info); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdEndConditionalRenderingEXT(VkCommandBuffer command) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdEndConditionalRenderingEXT>(state, "vkCmdEndConditionalRenderingEXT");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command);
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().conditional_end(command); });
}

EVRM_EXPORT VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkCommandPool* pool) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateCommandPool>(state, "vkCreateCommandPool");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, pool);
    if (result == VK_SUCCESS && info && pool && state->deferred) {
        const auto flags = info->queueFamilyIndex < state->deferred_family_flags.size()
            ? state->deferred_family_flags[info->queueFamilyIndex] : VkQueueFlags{0};
        state->deferred->pool_created(*pool, *info, flags);
    }
    if (result == VK_SUCCESS && info && pool && state->trace) {
        try {
            std::lock_guard<std::mutex> guard(state->trace_pool_mutex);
            if (state->trace_pools.size() < evrm::CommandTrace::max_commands)
                state->trace_pools[*pool] = info->queueFamilyIndex;
        } catch (...) { evrm::log_event("command-trace.pool.failed"); }
    }
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyCommandPool>(state, "vkDestroyCommandPool");
    if (!next) return;
    next(device, pool, allocator);
    if (state->deferred) state->deferred->pool_destroyed(pool);
    if (state->native) state->native->pool_destroyed(pool);
    observe(state,[&](evrm::GpuProbe& probe){probe.command_pool_reset(pool,true);});
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pool_destroyed(pool); });
    if (state->trace) try {
        std::lock_guard<std::mutex> guard(state->trace_pool_mutex);
        state->trace_pools.erase(pool);
    } catch (...) { evrm::log_event("command-trace.pool.failed"); }
}
EVRM_EXPORT VkResult VKAPI_CALL vkResetCommandPool(VkDevice device, VkCommandPool pool, VkCommandPoolResetFlags flags) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkResetCommandPool>(state, "vkResetCommandPool");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, pool, flags);
    if (state->deferred) state->deferred->pool_reset(pool, result);
    if (result == VK_SUCCESS && state->native) state->native->pool_reset(pool);
    if(result==VK_SUCCESS)observe(state,[&](evrm::GpuProbe& probe){probe.command_pool_reset(pool);});
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pool_reset(pool); });
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* commands) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkAllocateCommandBuffers>(state, "vkAllocateCommandBuffers");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, commands);
    if (result == VK_SUCCESS && info && state->deferred) state->deferred->allocated(*info, commands);
    if (result == VK_SUCCESS && info && state->native) state->native->allocated(*info,commands);
    if(result==VK_SUCCESS&&info)observe(state,[&](evrm::GpuProbe& probe){probe.commands_allocated(*info,commands);});
    if (result == VK_SUCCESS && info) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) {
        trace.collector().allocated(*info, commands, trace_pool_family(state, info->commandPool));
    });
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer command) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkEndCommandBuffer>(state, "vkEndCommandBuffer");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    native_before_command(state, command);
    const auto result = next(command);
    if (state->deferred) state->deferred->command_end(command, result);
    if (state->native) state->native->command_end(command,result);
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().end(command); });
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer command, VkSubpassContents contents) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdNextSubpass>(state, "vkCmdNextSubpass");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, contents);
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().next_subpass(command, contents); });
}
#define EVRM_TRACE_NEXT_SUBPASS(name) \
EVRM_EXPORT void VKAPI_CALL name(VkCommandBuffer command, const VkSubpassBeginInfo* begin, const VkSubpassEndInfo* end) { \
    const auto state = device_state(command); const auto next = device_proc<PFN_##name>(state, #name); if (!next) return; \
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true); \
    next(command, begin, end); \
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { \
        trace.collector().next_subpass(command, begin ? begin->contents : VK_SUBPASS_CONTENTS_INLINE, evrm::CommandTracePass::render_pass2); \
        if (!begin || (begin && begin->pNext) || (end && end->pNext)) trace.collector().unsupported(command, VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO); \
    }); \
}
EVRM_TRACE_NEXT_SUBPASS(vkCmdNextSubpass2)
EVRM_TRACE_NEXT_SUBPASS(vkCmdNextSubpass2KHR)
#undef EVRM_TRACE_NEXT_SUBPASS

EVRM_EXPORT void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer command, VkBuffer source, VkBuffer destination,
    uint32_t count, const VkBufferCopy* regions) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdCopyBuffer>(state, "vkCmdCopyBuffer");
    if (!next) return;
    native_before_command(state, command);
    next(command, source, destination, count, regions);
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().copy_buffer(command, source, destination, count, regions); });
}
#define EVRM_TRACE_COPY_BUFFER2(name) \
EVRM_EXPORT void VKAPI_CALL name(VkCommandBuffer command, const VkCopyBufferInfo2* info) { \
    const auto state = device_state(command); const auto next = device_proc<PFN_##name>(state, #name); if (!next) return; \
    native_before_command(state, command); \
    next(command, info); \
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { \
        if (!info) { trace.collector().unsupported(command, VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2); return; } \
        if (info->pNext) trace.collector().unsupported(command, VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2); \
        if (info->regionCount > evrm::CommandTrace::max_transfer_regions || (info->regionCount && !info->pRegions)) { \
            trace.collector().copy_buffer(command, info->srcBuffer, info->dstBuffer, info->regionCount, nullptr); return; \
        } \
        std::array<VkBufferCopy, evrm::CommandTrace::max_transfer_regions> regions{}; \
        for (uint32_t i = 0; i < info->regionCount; ++i) { \
            const auto& region = info->pRegions[i]; regions[i] = {region.srcOffset, region.dstOffset, region.size}; \
            if (region.pNext) trace.collector().unsupported(command, VK_STRUCTURE_TYPE_BUFFER_COPY_2); \
        } \
        trace.collector().copy_buffer(command, info->srcBuffer, info->dstBuffer, info->regionCount, regions.data()); \
    }); \
}
EVRM_TRACE_COPY_BUFFER2(vkCmdCopyBuffer2)
EVRM_TRACE_COPY_BUFFER2(vkCmdCopyBuffer2KHR)
#undef EVRM_TRACE_COPY_BUFFER2

EVRM_EXPORT void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdFillBuffer>(state, "vkCmdFillBuffer");
    if (!next) return;
    native_before_command(state, command);
    next(command, buffer, offset, size, data);
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().fill_buffer(command, buffer, offset, size, data); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, const void* data) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdUpdateBuffer>(state, "vkCmdUpdateBuffer");
    if (!next) return;
    native_before_command(state, command);
    next(command, buffer, offset, size, data);
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().update_buffer(command, buffer, offset, size); });
}
