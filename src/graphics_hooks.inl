// Forward every native call unchanged; observe only successful creation/submission.
#define EVRM_GRAPHICS_HOOK_NAMES(X) \
    X(vkCreateImage) \
    X(vkDestroyImage) \
    X(vkCreateImageView) \
    X(vkDestroyImageView) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkCreateRenderPass) \
    X(vkCreateRenderPass2) \
    X(vkCreateRenderPass2KHR) \
    X(vkDestroyRenderPass) \
    X(vkCreateFramebuffer) \
    X(vkDestroyFramebuffer) \
    X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) \
    X(vkCreateGraphicsPipelines) \
    X(vkCreateComputePipelines) \
    X(vkDestroyPipeline) \
    X(vkBeginCommandBuffer) \
    X(vkResetCommandBuffer) \
    X(vkFreeCommandBuffers) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdBeginRenderPass2) \
    X(vkCmdBeginRenderPass2KHR) \
    X(vkCmdEndRenderPass) \
    X(vkCmdEndRenderPass2) \
    X(vkCmdEndRenderPass2KHR) \
    X(vkCmdBeginRendering) \
    X(vkCmdBeginRenderingKHR) \
    X(vkCmdEndRendering) \
    X(vkCmdEndRenderingKHR) \
    X(vkCmdBindPipeline) \
    X(vkCmdSetViewport) \
    X(vkCmdSetScissor) \
    X(vkCmdDispatch) \
    X(vkCmdDispatchIndirect) \
    X(vkCmdDispatchBase) \
    X(vkCmdDispatchBaseKHR) \
    X(vkCmdDrawIndexedIndirect) \
    X(vkCmdDrawIndexedIndirectCount) \
    X(vkCmdDrawIndexedIndirectCountKHR) \
    X(vkCmdDrawIndexedIndirectCountAMD) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdPipelineBarrier2) \
    X(vkCmdPipelineBarrier2KHR) \
    X(vkCmdExecuteCommands) \
    X(vkQueueSubmit) \
    X(vkQueueSubmit2) \
    X(vkQueueSubmit2KHR) \
    X(vkQueueBindSparse) X(vkDeviceWaitIdle)

EVRM_EXPORT VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks* allocator, VkImage* image) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateImage>(state, "vkCreateImage");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, image);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.image_created(*image, *info); });
    if (result == VK_SUCCESS && info && image) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.image_created(*image, *info); });
    if(result==VK_SUCCESS&&info&&image&&state->native){
        auto source_guard=state->native->native_source_guard();
        observe_native(state,[&](evrm::NativeState& native){native.image_created(*image,*info);});
        if(auto* memory=state->native->image_memory())memory->image_created(*image,*info);
    }
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyImage>(state, "vkDestroyImage");
    if (!next) return;
    // Retire metadata first so a new presentation cannot pick this resource.
    // The renderer callback waits for any presentation that already picked it.
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.image_destroyed(image); });
    if (state->avatar) state->avatar->native_image_destroyed(image);
    {
        std::unique_lock<std::mutex> source_guard;if(state->native)source_guard=state->native->native_source_guard();
        observe_native(state,[&](evrm::NativeState& native){native.image_destroyed(image);});
        if(state->native)if(auto* memory=state->native->image_memory())memory->image_destroyed(image);
        next(device,image,allocator);
    }
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.image_destroyed(image); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* info, const VkAllocationCallbacks* allocator, VkImageView* view) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateImageView>(state, "vkCreateImageView");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, view);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.view_created(*view, *info); });
    if (result == VK_SUCCESS && info && view) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.image_view_created(*view, *info); });
    if(result==VK_SUCCESS&&info&&view&&state->native){
        auto source_guard=state->native->native_source_guard();
        observe_native(state,[&](evrm::NativeState& native){native.view_created(*view,*info);});
    }
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView view, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyImageView>(state, "vkDestroyImageView");
    if (!next) return;
    {
        std::unique_lock<std::mutex> source_guard;if(state->native)source_guard=state->native->native_source_guard();
        observe_native(state,[&](evrm::NativeState& native){native.view_destroyed(view);});
        next(device,view,allocator);
    }
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.view_destroyed(view); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.image_view_destroyed(view); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* info, const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateSwapchainKHR>(state, "vkCreateSwapchainKHR");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, swapchain);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.swapchain_created(*swapchain, *info); });
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroySwapchainKHR>(state, "vkDestroySwapchainKHR");
    if (!next) return;
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.swapchain_destroyed(swapchain); });
    if (state->avatar) state->avatar->native_swapchain_destroyed(swapchain);
    next(device, swapchain, allocator);
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks* allocator, VkRenderPass* pass) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateRenderPass>(state, "vkCreateRenderPass");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, pass);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.renderpass_created(*pass, *info); });
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.renderpass_created(*pass, *info); });
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2* info, const VkAllocationCallbacks* allocator, VkRenderPass* pass) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateRenderPass2>(state, "vkCreateRenderPass2");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, pass);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.renderpass2_created(*pass, *info); });
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.renderpass2_created(*pass, *info); });
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateRenderPass2KHR(VkDevice device, const VkRenderPassCreateInfo2* info, const VkAllocationCallbacks* allocator, VkRenderPass* pass) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateRenderPass2KHR>(state, "vkCreateRenderPass2KHR");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, pass);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.renderpass2_created(*pass, *info); });
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.renderpass2_created(*pass, *info); });
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyRenderPass(VkDevice device, VkRenderPass pass, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyRenderPass>(state, "vkDestroyRenderPass");
    if (!next) return;
    next(device, pass, allocator);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.renderpass_destroyed(pass); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.renderpass_destroyed(pass); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* info, const VkAllocationCallbacks* allocator, VkFramebuffer* framebuffer) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateFramebuffer>(state, "vkCreateFramebuffer");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, framebuffer);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.framebuffer_created(*framebuffer, *info); });
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.framebuffer_created(*framebuffer, *info); });
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyFramebuffer>(state, "vkDestroyFramebuffer");
    if (!next) return;
    next(device, framebuffer, allocator);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.framebuffer_destroyed(framebuffer); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.framebuffer_destroyed(framebuffer); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* info, const VkAllocationCallbacks* allocator, VkShaderModule* module) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateShaderModule>(state, "vkCreateShaderModule");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, module);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.shader_created(*module, *info); });
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.shader_created(*module, *info); });
    if (result == VK_SUCCESS) observe_native(state, [&](evrm::NativeState& native) { native.shader_created(*module,*info); });
    if(result==VK_SUCCESS&&state->native)if(auto* clips=state->native->clipping())clips->shader_created(*module,*info);
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule module, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyShaderModule>(state, "vkDestroyShaderModule");
    if (!next) return;
    next(device, module, allocator);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.shader_destroyed(module); });
    observe_native(state, [&](evrm::NativeState& native) { native.shader_destroyed(module); });
    if(state->native)if(auto* clips=state->native->clipping())clips->shader_destroyed(module);
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.shader_destroyed(module); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache cache, uint32_t count, const VkGraphicsPipelineCreateInfo* infos, const VkAllocationCallbacks* allocator, VkPipeline* pipelines) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateGraphicsPipelines>(state, "vkCreateGraphicsPipelines");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, cache, count, infos, allocator, pipelines);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.pipelines_created(count, infos, pipelines); });
    if (infos && pipelines) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.graphics_pipelines_created(count, infos, pipelines); });
    if (infos && pipelines) observe_native(state, [&](evrm::NativeState& native) { native.graphics_pipelines_created(count,infos,pipelines); });
    if(result==VK_SUCCESS&&infos&&pipelines&&state->native)if(auto* clips=state->native->clipping())clips->graphics_pipelines_created(count,infos,pipelines);
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyPipeline>(state, "vkDestroyPipeline");
    if (!next) return;
    next(device, pipeline, allocator);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.pipeline_destroyed(pipeline); });
    observe_native(state, [&](evrm::NativeState& native) { native.pipeline_destroyed(pipeline); });
    if(state->native)if(auto* clips=state->native->clipping())clips->pipeline_destroyed(pipeline);
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.pipeline_destroyed(pipeline); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo* infos,const VkAllocationCallbacks* allocator,VkPipeline* pipelines) {
    const auto state=device_state(device);
    const auto next=device_proc<PFN_vkCreateComputePipelines>(state,"vkCreateComputePipelines");
    if(!next)return VK_ERROR_INITIALIZATION_FAILED;
    const auto result=next(device,cache,count,infos,allocator,pipelines);
    // A failed batch may still return successfully created non-null pipelines.
    if(infos&&pipelines)observe_graphics(state,[&](evrm::GraphicsState& graphics){graphics.compute_pipelines_created(count,infos,pipelines);});
    if(infos&&pipelines)observe_trace(state,[&](evrm::CommandTraceRuntime& trace){trace.compute_pipelines_created(count,infos,pipelines);});
    if(infos&&pipelines)observe_native(state,[&](evrm::NativeState& native){native.compute_pipelines_created(count,infos,pipelines);});
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer command, const VkCommandBufferBeginInfo* info) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkBeginCommandBuffer>(state, "vkBeginCommandBuffer");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(command, info);
    if (info && state->deferred) state->deferred->command_begin(command, *info, result);
    if (result == VK_SUCCESS && state->native && info) state->native->command_begin(command,*info);
    if (result == VK_SUCCESS) {
        observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.command_begin(command); });
        observe(state, [&](evrm::GpuProbe& probe) { probe.command_begin(command); });
        if (info) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().begin(command, *info); });
    }
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer command, VkCommandBufferResetFlags flags) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkResetCommandBuffer>(state, "vkResetCommandBuffer");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(command, flags);
    if (state->deferred) state->deferred->command_reset(command, result);
    if (result == VK_SUCCESS && state->native) state->native->command_reset(command);
    if (result == VK_SUCCESS) {
        observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.command_begin(command); });
        observe(state, [&](evrm::GpuProbe& probe) { probe.command_begin(command); });
        observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().reset(command); });
    }
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count, const VkCommandBuffer* commands) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkFreeCommandBuffers>(state, "vkFreeCommandBuffers");
    if (!next) return;
    next(device, pool, count, commands);
    if (state->deferred) state->deferred->command_freed(count, commands);
    if (state->native) state->native->command_freed(count,commands);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.command_freed(count, commands); });
    observe(state, [&](evrm::GpuProbe& probe) { probe.command_freed(count, commands); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().freed(pool, count, commands); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer command, const VkRenderPassBeginInfo* info, VkSubpassContents contents) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdBeginRenderPass>(state, "vkCmdBeginRenderPass");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, info, contents);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.begin_pass(command, *info); });
    if (info) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pass_begin(command, info->renderPass, info->framebuffer, contents); if (info->pNext) trace.collector().unsupported(command, VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdBeginRenderPass2(VkCommandBuffer command, const VkRenderPassBeginInfo* info, const VkSubpassBeginInfo* begin) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdBeginRenderPass2>(state, "vkCmdBeginRenderPass2");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, info, begin);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.begin_pass(command, *info); });
    if (info) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pass_begin(command, info->renderPass, info->framebuffer, begin ? begin->contents : VK_SUBPASS_CONTENTS_INLINE, evrm::CommandTracePass::render_pass2); if (info->pNext || !begin || (begin && begin->pNext)) trace.collector().unsupported(command, VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdBeginRenderPass2KHR(VkCommandBuffer command, const VkRenderPassBeginInfo* info, const VkSubpassBeginInfo* begin) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdBeginRenderPass2KHR>(state, "vkCmdBeginRenderPass2KHR");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, info, begin);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.begin_pass(command, *info); });
    if (info) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pass_begin(command, info->renderPass, info->framebuffer, begin ? begin->contents : VK_SUBPASS_CONTENTS_INLINE, evrm::CommandTracePass::render_pass2); if (info->pNext || !begin || (begin && begin->pNext)) trace.collector().unsupported(command, VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer command) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdEndRenderPass>(state, "vkCmdEndRenderPass");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.end_pass(command); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pass_end(command); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdEndRenderPass2(VkCommandBuffer command, const VkSubpassEndInfo* end) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdEndRenderPass2>(state, "vkCmdEndRenderPass2");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, end);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.end_pass(command); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pass_end(command, evrm::CommandTracePass::render_pass2); if (end && end->pNext) trace.collector().unsupported(command, VK_STRUCTURE_TYPE_SUBPASS_END_INFO); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdEndRenderPass2KHR(VkCommandBuffer command, const VkSubpassEndInfo* end) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdEndRenderPass2KHR>(state, "vkCmdEndRenderPass2KHR");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, end);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.end_pass(command); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pass_end(command, evrm::CommandTracePass::render_pass2); if (end && end->pNext) trace.collector().unsupported(command, VK_STRUCTURE_TYPE_SUBPASS_END_INFO); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer command, const VkRenderingInfo* info) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdBeginRendering>(state, "vkCmdBeginRendering");
    if (!next) return;
    native_begin_rendering(state, command, info, next);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.begin_rendering(command, *info); });
    if (info) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().begin_rendering(command, *info); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdBeginRenderingKHR(VkCommandBuffer command, const VkRenderingInfo* info) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdBeginRenderingKHR>(state, "vkCmdBeginRenderingKHR");
    if (!next) return;
    native_begin_rendering(state, command, info, next);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.begin_rendering(command, *info); });
    if (info) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().begin_rendering(command, *info); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer command) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdEndRendering>(state, "vkCmdEndRendering");
    if (!next) return;
    native_before_command(state, command);
    if (state->native) state->native->before_end_rendering(command);
    next(command);
    if (state->deferred) state->deferred->rendering_ended(command);
    if(state->native&&state->deferred&&state->native->pose_capture_boundary(command))
        if(const auto family=state->deferred->pose_copy_family(command))observe(state,[&](evrm::GpuProbe& probe){probe.capture_inline(command,*family);});
    if (state->native) state->native->end_rendering(command);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.end_rendering(command); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pass_end(command, evrm::CommandTracePass::dynamic_rendering); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdEndRenderingKHR(VkCommandBuffer command) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdEndRenderingKHR>(state, "vkCmdEndRenderingKHR");
    if (!next) return;
    native_before_command(state, command);
    if (state->native) state->native->before_end_rendering(command);
    next(command);
    if (state->deferred) state->deferred->rendering_ended(command);
    if(state->native&&state->deferred&&state->native->pose_capture_boundary(command))
        if(const auto family=state->deferred->pose_copy_family(command))observe(state,[&](evrm::GpuProbe& probe){probe.capture_inline(command,*family);});
    if (state->native) state->native->end_rendering(command);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.end_rendering(command); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pass_end(command, evrm::CommandTracePass::dynamic_rendering); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer command, uint32_t first, uint32_t count, const VkViewport* values) {
    const auto state=device_state(command);
    const auto next=device_proc<PFN_vkCmdSetViewport>(state,"vkCmdSetViewport");
    if(!next)return;
    native_before_forwarded_command(state,command,"vkCmdSetViewport");
    next(command,first,count,values);
    observe_native(state,[&](evrm::NativeState& native){native.viewports_set(command,first,count,values);});
}
EVRM_EXPORT void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer command, uint32_t first, uint32_t count, const VkRect2D* values) {
    const auto state=device_state(command);
    const auto next=device_proc<PFN_vkCmdSetScissor>(state,"vkCmdSetScissor");
    if(!next)return;
    native_before_forwarded_command(state,command,"vkCmdSetScissor");
    next(command,first,count,values);
    observe_native(state,[&](evrm::NativeState& native){native.scissors_set(command,first,count,values);});
}
EVRM_EXPORT void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer command, VkPipelineBindPoint point, VkPipeline pipeline) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdBindPipeline>(state, "vkCmdBindPipeline");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::independent_state);
    next(command, point, pipeline);
    observe_native(state,[&](evrm::NativeState& native){native.pipeline_bound(command,point,pipeline);});
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.pipeline_bound(command, point, pipeline); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().pipeline_bound(command, point, pipeline); });
    if(point==VK_PIPELINE_BIND_POINT_GRAPHICS) {
        bool model=false;VkPipelineLayout layout{};observe_graphics(state,[&](evrm::GraphicsState& graphics){model=graphics.model_pipeline(pipeline);layout=graphics.pipeline_layout(pipeline);});
        observe(state,[&](evrm::GpuProbe& probe){probe.pipeline_bound(command,model,layout);});
    }else if(point==VK_PIPELINE_BIND_POINT_COMPUTE) {
        bool anchor=false;VkPipelineLayout layout{};
        observe_graphics(state,[&](evrm::GraphicsState& graphics){anchor=graphics.player_anchor_pipeline(pipeline);layout=graphics.pipeline_layout(pipeline);});
        observe(state,[&](evrm::GpuProbe& probe){probe.compute_pipeline_bound(command,anchor,layout);});
    }
}
EVRM_EXPORT void VKAPI_CALL vkCmdDispatch(VkCommandBuffer command,uint32_t x,uint32_t y,uint32_t z) {
    const auto state=device_state(command);const auto next=device_proc<PFN_vkCmdDispatch>(state,"vkCmdDispatch");if(!next)return;
    native_before_command(state, command);
    next(command,x,y,z);
    if(state->native&&x&&y&&z)state->native->dispatch(command,true,x,y,z);
    observe(state,[&](evrm::GpuProbe& probe){probe.compute_dispatched(command,x,y,z);});
    observe_trace(state,[&](evrm::CommandTraceRuntime& trace){trace.collector().dispatch(command,x,y,z);});
}
EVRM_EXPORT void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer command,VkBuffer buffer,VkDeviceSize offset) {
    const auto state=device_state(command);const auto next=device_proc<PFN_vkCmdDispatchIndirect>(state,"vkCmdDispatchIndirect");if(!next)return;
    native_before_command(state, command);
    next(command,buffer,offset);
    if(state->native)state->native->dispatch(command,false);
    observe(state,[&](evrm::GpuProbe& probe){probe.compute_dispatch_unsupported(command);});
    observe_trace(state,[&](evrm::CommandTraceRuntime& trace){trace.collector().dispatch_indirect(command,buffer,offset);});
}
#define EVRM_DISPATCH_BASE_HOOK(name) \
EVRM_EXPORT void VKAPI_CALL name(VkCommandBuffer command,uint32_t bx,uint32_t by,uint32_t bz,uint32_t x,uint32_t y,uint32_t z) { \
    const auto state=device_state(command);const auto next=device_proc<PFN_##name>(state,#name);if(!next)return; \
    native_before_command(state, command); \
    next(command,bx,by,bz,x,y,z); \
    if(state->native&&x&&y&&z)state->native->dispatch(command,!(bx||by||bz),x,y,z); \
    observe(state,[&](evrm::GpuProbe& probe){if(bx||by||bz)probe.compute_dispatch_unsupported(command);else probe.compute_dispatched(command,x,y,z);}); \
    observe_trace(state,[&](evrm::CommandTraceRuntime& trace){trace.collector().dispatch_base(command,bx,by,bz,x,y,z);}); \
}
EVRM_DISPATCH_BASE_HOOK(vkCmdDispatchBase)
EVRM_DISPATCH_BASE_HOOK(vkCmdDispatchBaseKHR)
#undef EVRM_DISPATCH_BASE_HOOK
EVRM_EXPORT void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer command,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride) {
    const auto state=device_state(command);const auto next=device_proc<PFN_vkCmdDrawIndexedIndirect>(state,"vkCmdDrawIndexedIndirect");if(!next)return;
    if(state->native)state->native->unsupported_draw(command);
    native_before_command(state, command, evrm::DeferredRenderingCommand::candidate_draw,false,"vkCmdDrawIndexedIndirect");
    if(state->native)state->native->before_native_draw(command);
    next(command,buffer,offset,count,stride);
    if(state->native)state->native->after_native_draw(command);
    observe(state,[&](evrm::GpuProbe& probe){probe.draw_indexed_indirect(command,buffer,offset,count,stride);});
    observe_trace(state,[&](evrm::CommandTraceRuntime& trace){trace.collector().draw_indexed_indirect(command,buffer,offset,count,stride);});
}
#define EVRM_DRAW_COUNT_HOOK(name) \
EVRM_EXPORT void VKAPI_CALL name(VkCommandBuffer command,VkBuffer buffer,VkDeviceSize offset,VkBuffer count_buffer,VkDeviceSize count_offset,uint32_t count,uint32_t stride) { \
    const auto state=device_state(command);const auto next=device_proc<PFN_##name>(state,#name);if(!next)return; \
    if(state->native)state->native->draw(command,buffer,offset,count_buffer,count_offset,count,stride); \
    native_before_command(state, command, evrm::DeferredRenderingCommand::candidate_draw,false,#name); \
    if(state->native)state->native->render_current(command); \
    if(state->native)state->native->before_native_draw(command); \
    next(command,buffer,offset,count_buffer,count_offset,count,stride); \
    if(state->native)state->native->after_native_draw(command); \
    observe(state,[&](evrm::GpuProbe& probe){probe.draw_indexed_indirect(command,buffer,offset,count,stride,count_buffer,count_offset);}); \
    observe_trace(state,[&](evrm::CommandTraceRuntime& trace){trace.collector().draw_indexed_indirect(command,buffer,offset,count,stride,count_buffer,count_offset);}); \
}
EVRM_DRAW_COUNT_HOOK(vkCmdDrawIndexedIndirectCount)
EVRM_DRAW_COUNT_HOOK(vkCmdDrawIndexedIndirectCountKHR)
EVRM_DRAW_COUNT_HOOK(vkCmdDrawIndexedIndirectCountAMD)
#undef EVRM_DRAW_COUNT_HOOK
EVRM_EXPORT void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command, VkPipelineStageFlags source, VkPipelineStageFlags destination, VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier* memory, uint32_t buffer_count, const VkBufferMemoryBarrier* buffers, uint32_t image_count, const VkImageMemoryBarrier* images) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdPipelineBarrier>(state, "vkCmdPipelineBarrier");
    if (!next) return;
    native_before_command(state, command);
    next(command, source, destination, flags, memory_count, memory, buffer_count, buffers, image_count, images);
    if(state->native)state->native->barriers(command,source,destination,flags,memory_count,memory,buffer_count,buffers,image_count,images);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.barriers(command, image_count, images); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer command, const VkDependencyInfo* info) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdPipelineBarrier2>(state, "vkCmdPipelineBarrier2");
    if (!next) return;
    native_before_command(state, command);
    next(command, info);
    if(state->native)state->native->barriers2(command,info);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.barriers2(command, *info); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdPipelineBarrier2KHR(VkCommandBuffer command, const VkDependencyInfo* info) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdPipelineBarrier2KHR>(state, "vkCmdPipelineBarrier2KHR");
    if (!next) return;
    native_before_command(state, command);
    next(command, info);
    if(state->native)state->native->barriers2(command,info);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.barriers2(command, *info); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdExecuteCommands(VkCommandBuffer command, uint32_t count, const VkCommandBuffer* children) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdExecuteCommands>(state, "vkCmdExecuteCommands");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::other, true);
    next(command, count, children);
    observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.execute(command, count, children); });
    observe(state, [&](evrm::GpuProbe& probe) { probe.execute(command, count, children); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().execute(command, count, children); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo* submits, VkFence fence) {
    const auto state = device_state(queue);
    const auto next = device_proc<PFN_vkQueueSubmit>(state, "vkQueueSubmit");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto mutex = queue_mutex(state, queue);
    std::lock_guard<std::recursive_mutex> queue_guard(*mutex);
    if(state->native){const auto gate=state->native->before_submit(queue,count,submits);if(gate!=VK_SUCCESS)return gate;}
    const auto result = state->native&&state->probe?
        state->probe->submit_async(queue,count,submits,fence,next,state->native->state().buffer_epoch()):next(queue,count,submits,fence);
    if(state->native)state->native->submission_result(queue,result);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.submitted(queue, count, submits); });
    if (result == VK_SUCCESS) observe(state, [&](evrm::GpuProbe& probe) { probe.submitted(queue,count, submits); });
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().submitted(queue, count, submits); });
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkQueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits, VkFence fence) {
    const auto state = device_state(queue);
    const auto next = device_proc<PFN_vkQueueSubmit2>(state, "vkQueueSubmit2");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto mutex = queue_mutex(state, queue);
    std::lock_guard<std::recursive_mutex> queue_guard(*mutex);
    if(state->native){const auto gate=state->native->before_submit2(queue,count,submits);if(gate!=VK_SUCCESS)return gate;}
    const auto result = next(queue, count, submits, fence);
    if(state->native&&state->probe)state->probe->inline_submitted2(queue,count,submits,result,state->native->state().buffer_epoch());
    if(state->native)state->native->submission_result(queue,result);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.submitted2(queue, count, submits); });
    if (result == VK_SUCCESS) observe(state, [&](evrm::GpuProbe& probe) { probe.submitted2(queue,count, submits); });
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().submitted2(queue, count, submits); });
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkQueueSubmit2KHR(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits, VkFence fence) {
    const auto state = device_state(queue);
    const auto next = device_proc<PFN_vkQueueSubmit2KHR>(state, "vkQueueSubmit2KHR");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto mutex = queue_mutex(state, queue);
    std::lock_guard<std::recursive_mutex> queue_guard(*mutex);
    if(state->native){const auto gate=state->native->before_submit2(queue,count,submits);if(gate!=VK_SUCCESS)return gate;}
    const auto result = next(queue, count, submits, fence);
    if(state->native&&state->probe)state->probe->inline_submitted2(queue,count,submits,result,state->native->state().buffer_epoch());
    if(state->native)state->native->submission_result(queue,result);
    if (result == VK_SUCCESS) observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.submitted2(queue, count, submits); });
    if (result == VK_SUCCESS) observe(state, [&](evrm::GpuProbe& probe) { probe.submitted2(queue,count, submits); });
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().submitted2(queue, count, submits); });
    return result;
}

EVRM_EXPORT VkResult VKAPI_CALL vkQueueBindSparse(VkQueue queue, uint32_t count, const VkBindSparseInfo* infos, VkFence fence) {
    const auto state = device_state(queue);
    const auto next = device_proc<PFN_vkQueueBindSparse>(state, "vkQueueBindSparse");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto mutex = queue_mutex(state, queue);
    std::lock_guard<std::recursive_mutex> queue_guard(*mutex);
    std::unique_lock<std::mutex> source_guard;if(state->native)source_guard=state->native->native_source_guard();
    if(count&&state->native)if(auto* memory=state->native->image_memory()){
        static std::atomic_flag sparse_notice=ATOMIC_FLAG_INIT;
        if(!sparse_notice.test_and_set(std::memory_order_relaxed))try{
            const auto diagnostic=memory->diagnostic(VK_NULL_HANDLE);
            evrm::log_event("native-image-backing.sparse-observed count="+std::to_string(count)+
                " registryObserved="+std::to_string(diagnostic.observed)+" registryDisabledBefore="+std::to_string(diagnostic.disabled)+
                " registryCounts="+std::to_string(diagnostic.allocations)+","+std::to_string(diagnostic.images)+","+std::to_string(diagnostic.buffers));
        }catch(...){}
        memory->disable();
    }
    return next(queue, count, infos, fence);
}
EVRM_EXPORT VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDeviceWaitIdle>(state, "vkDeviceWaitIdle");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    AllQueueLocks queues(state);
    return next(device);
}


