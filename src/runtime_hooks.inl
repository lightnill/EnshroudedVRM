// Included in runtime_bridge.cpp so all hooks share its validated dispatch state.
#include "native_image_memory.hpp"
#define EVRM_DEVICE_HOOK_NAMES(X) \
    X(vkCreateBuffer) X(vkDestroyBuffer) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkBindBufferMemory2) X(vkBindBufferMemory2KHR) \
    X(vkBindImageMemory) X(vkBindImageMemory2) X(vkBindImageMemory2KHR) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkAllocateDescriptorSets) X(vkFreeDescriptorSets) \
    X(vkResetDescriptorPool) X(vkDestroyDescriptorPool) \
    X(vkUpdateDescriptorSets) X(vkCreateDescriptorUpdateTemplate) \
    X(vkDestroyDescriptorUpdateTemplate) X(vkUpdateDescriptorSetWithTemplate) \
    X(vkCmdBindDescriptorSets) X(vkGetDeviceQueue) X(vkGetDeviceQueue2) X(vkQueueWaitIdle)

EVRM_EXPORT VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* info,
                                               const VkAllocationCallbacks* allocator, VkBuffer* buffer) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateBuffer>(state, "vkCreateBuffer");
    if (!next || !info || !buffer) return VK_ERROR_INITIALIZATION_FAILED;
    auto actual = *info;
    if (state->probe && state->probe->needs_transfer_source(actual)) actual.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    auto result = next(device, &actual, allocator, buffer);
    if (result != VK_SUCCESS && actual.usage != info->usage) {
        actual = *info;
        result = next(device, info, allocator, buffer);
    }
    if (result == VK_SUCCESS) observe(state, [&](evrm::GpuProbe& probe) { probe.buffer_created(*buffer, actual); });
    if (result == VK_SUCCESS) observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.buffer_created(*buffer, actual); });
    if(result==VK_SUCCESS&&state->native){
        auto source_guard=state->native->native_source_guard();
        observe_native(state,[&](evrm::NativeState& native){native.buffer_created(*buffer,actual);});
        if(auto* memory=state->native->buffer_memory())memory->buffer_created(*buffer,actual);
        if(auto* memory=state->native->image_memory())memory->buffer_created(*buffer,actual);
    }
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyBuffer>(state, "vkDestroyBuffer");
    if (!next) return;
    observe(state, [&](evrm::GpuProbe& probe) { probe.buffer_destroyed(buffer); });
    {
        std::unique_lock<std::mutex> source_guard;
        if(state->native)source_guard=state->native->native_source_guard();
        observe_native(state, [&](evrm::NativeState& native) { native.buffer_destroyed(buffer); });
        if(state->native)if(auto* memory=state->native->buffer_memory())memory->buffer_destroyed(buffer);
        if(state->native)if(auto* memory=state->native->image_memory())memory->buffer_destroyed(buffer);
        next(device, buffer, allocator);
    }
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.buffer_destroyed(buffer); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkAllocateMemory(VkDevice device,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks* allocator,VkDeviceMemory* memory) {
    const auto state=device_state(device);const auto next=device_proc<PFN_vkAllocateMemory>(state,"vkAllocateMemory");if(!next)return VK_ERROR_INITIALIZATION_FAILED;
    const auto result=next(device,info,allocator,memory);
    if(result==VK_SUCCESS&&info&&memory&&state->native){
        auto source_guard=state->native->native_source_guard();
        if(auto* tracked=state->native->buffer_memory())tracked->memory_created(*memory,*info);
        if(auto* tracked=state->native->image_memory())tracked->memory_created(*memory,*info);
    }
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkFreeMemory(VkDevice device,VkDeviceMemory memory,const VkAllocationCallbacks* allocator) {
    const auto state=device_state(device);const auto next=device_proc<PFN_vkFreeMemory>(state,"vkFreeMemory");if(!next)return;
    observe(state,[&](evrm::GpuProbe& probe){probe.memory_destroyed(memory);});
    std::unique_lock<std::mutex> source_guard;
    if(state->native)source_guard=state->native->native_source_guard();
    if(state->native)if(auto* tracked=state->native->buffer_memory())tracked->memory_destroyed(memory);
    if(state->native)if(auto* tracked=state->native->image_memory())tracked->memory_destroyed(memory);
    next(device,memory,allocator);
}
template<class State>void native_observe_buffer_backing(const State& state,VkDevice device,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset,bool ordinary){
    if(!state->native)return;
    auto* tracked=state->native->image_memory();if(!tracked)return;
    if(!ordinary){tracked->uncertain_memory(memory);return;}
    VkMemoryRequirements requirements{};const VkMemoryRequirements* known=nullptr;
    if(tracked->buffer_requirements_safe(buffer))if(const auto query=device_proc<PFN_vkGetBufferMemoryRequirements>(state,"vkGetBufferMemoryRequirements")){
        query(device,buffer,&requirements);known=&requirements;
    }
    tracked->buffer_bound(buffer,memory,offset,known);
}
template<class State>void native_observe_image_backing(const State& state,VkDevice device,VkImage image,VkDeviceMemory memory,VkDeviceSize offset,bool ordinary){
    if(!state->native)return;
    auto* tracked=state->native->image_memory();if(!tracked)return;
    if(!ordinary){tracked->uncertain_memory(memory);return;}
    VkMemoryRequirements requirements{};const VkMemoryRequirements* known=nullptr;
    if(tracked->image_requirements_safe(image))if(const auto query=device_proc<PFN_vkGetImageMemoryRequirements>(state,"vkGetImageMemoryRequirements")){
        query(device,image,&requirements);known=&requirements;
    }
    tracked->image_bound(image,memory,offset,known);
}
EVRM_EXPORT VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset) {
    const auto state=device_state(device);const auto next=device_proc<PFN_vkBindBufferMemory>(state,"vkBindBufferMemory");if(!next)return VK_ERROR_INITIALIZATION_FAILED;
    std::unique_lock<std::mutex> source_guard;if(state->native)source_guard=state->native->native_source_guard();
    const auto result=next(device,buffer,memory,offset);
    observe(state,[&](evrm::GpuProbe& probe){probe.buffer_bound(buffer,memory,result==VK_SUCCESS);});
    if(state->native)if(auto* tracked=state->native->buffer_memory())tracked->bound(buffer,result==VK_SUCCESS?memory:VK_NULL_HANDLE,offset);
    native_observe_buffer_backing(state,device,buffer,memory,offset,result==VK_SUCCESS);
    return result;
}
#define EVRM_BIND_BUFFER_MEMORY2_HOOK(name) \
EVRM_EXPORT VkResult VKAPI_CALL name(VkDevice device,uint32_t count,const VkBindBufferMemoryInfo* infos) { \
    const auto state=device_state(device);const auto next=device_proc<PFN_##name>(state,#name);if(!next)return VK_ERROR_INITIALIZATION_FAILED; \
    std::unique_lock<std::mutex> source_guard;if(state->native)source_guard=state->native->native_source_guard(); \
    const auto result=next(device,count,infos); \
    if(count<=4096&&(!count||infos))for(uint32_t i=0;i<count;++i)observe(state,[&](evrm::GpuProbe& probe){probe.buffer_bound(infos[i].buffer,infos[i].memory,result==VK_SUCCESS&&infos[i].sType==VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO&&!infos[i].pNext);}); \
    if(state->native){ \
        auto* images=state->native->image_memory(); \
        if(count>4096||(count&&!infos)){if(images)images->disable();return result;} \
        for(uint32_t i=0;i<count;++i){const auto& item=infos[i]; \
            if(item.sType!=VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO){if(images)images->disable();continue;} \
            const bool ordinary=result==VK_SUCCESS&&!item.pNext; \
            if(auto* tracked=state->native->buffer_memory())tracked->bound(item.buffer,ordinary?item.memory:VK_NULL_HANDLE,item.memoryOffset); \
            native_observe_buffer_backing(state,device,item.buffer,item.memory,item.memoryOffset,ordinary); \
        } \
    } \
    return result; \
}
EVRM_BIND_BUFFER_MEMORY2_HOOK(vkBindBufferMemory2)
EVRM_BIND_BUFFER_MEMORY2_HOOK(vkBindBufferMemory2KHR)
#undef EVRM_BIND_BUFFER_MEMORY2_HOOK
EVRM_EXPORT VkResult VKAPI_CALL vkBindImageMemory(VkDevice device,VkImage image,VkDeviceMemory memory,VkDeviceSize offset) {
    const auto state=device_state(device);const auto next=device_proc<PFN_vkBindImageMemory>(state,"vkBindImageMemory");if(!next)return VK_ERROR_INITIALIZATION_FAILED;
    std::unique_lock<std::mutex> source_guard;if(state->native)source_guard=state->native->native_source_guard();
    const auto result=next(device,image,memory,offset);
    native_observe_image_backing(state,device,image,memory,offset,result==VK_SUCCESS);return result;
}
#define EVRM_BIND_IMAGE_MEMORY2_HOOK(name) \
EVRM_EXPORT VkResult VKAPI_CALL name(VkDevice device,uint32_t count,const VkBindImageMemoryInfo* infos) { \
    const auto state=device_state(device);const auto next=device_proc<PFN_##name>(state,#name);if(!next)return VK_ERROR_INITIALIZATION_FAILED; \
    std::unique_lock<std::mutex> source_guard;if(state->native)source_guard=state->native->native_source_guard(); \
    const auto result=next(device,count,infos); \
    if(state->native){if(auto* tracked=state->native->image_memory()){ \
        if(count>4096||(count&&!infos)){tracked->disable();return result;} \
        for(uint32_t i=0;i<count;++i){const auto& item=infos[i]; \
            if(item.sType!=VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO){tracked->disable();continue;} \
            native_observe_image_backing(state,device,item.image,item.memory,item.memoryOffset,result==VK_SUCCESS&&!item.pNext); \
        } \
    }} \
    return result; \
}
EVRM_BIND_IMAGE_MEMORY2_HOOK(vkBindImageMemory2)
EVRM_BIND_IMAGE_MEMORY2_HOOK(vkBindImageMemory2KHR)
#undef EVRM_BIND_IMAGE_MEMORY2_HOOK
EVRM_EXPORT VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkDescriptorSetLayout* layout) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateDescriptorSetLayout>(state, "vkCreateDescriptorSetLayout");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, layout);
    if(result==VK_SUCCESS && state->native && info && layout)try{
        const auto* chain=static_cast<const VkDescriptorSetLayoutBindingFlagsCreateInfo*>(info->pNext);
        const bool known=chain && chain->sType==VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO && !chain->pNext &&
            chain->bindingCount==info->bindingCount && chain->bindingCount<=256 && (!chain->bindingCount || chain->pBindingFlags);
        VkDescriptorBindingFlags mask=0;if(known)for(uint32_t i=0;i<chain->bindingCount;++i)mask|=chain->pBindingFlags[i];
        if((info->flags || mask || (chain&&!known)) && state->native_layout_notices.fetch_add(1)<24){
            std::ostringstream message;message<<"native-additive.layout-declaration handle="<<*layout<<" flags="<<info->flags<<" next="<<(chain?chain->sType:VK_STRUCTURE_TYPE_MAX_ENUM)<<" bindings="<<info->bindingCount<<" bindingFlags="<<mask;
            if(info->bindingCount<=256 && info->pBindings)for(uint32_t i=0;i<info->bindingCount;++i){const auto& b=info->pBindings[i];message<<" ["<<b.binding<<","<<b.descriptorType<<","<<b.descriptorCount<<","<<(known?chain->pBindingFlags[i]:0)<<"]";}
            evrm::log_event(message.str());}
    }catch(...){}
    if (result == VK_SUCCESS) observe(state, [&](evrm::GpuProbe& probe) { probe.layout_created(*layout, *info); });
    if (result == VK_SUCCESS) observe_native(state, [&](evrm::NativeState& native) { native.layout_created(*layout, *info); });
    if(result==VK_SUCCESS&&state->native)if(auto* clips=state->native->clipping())clips->layout_created(*layout,*info);
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyDescriptorSetLayout>(state, "vkDestroyDescriptorSetLayout");
    if (!next) return;
    observe(state, [&](evrm::GpuProbe& probe) { probe.layout_destroyed(layout); });
    observe_native(state, [&](evrm::NativeState& native) { native.layout_destroyed(layout); });
    if(state->native)if(auto* clips=state->native->clipping())clips->layout_destroyed(layout);
    next(device, layout, allocator);
}
EVRM_EXPORT VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* sets) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkAllocateDescriptorSets>(state, "vkAllocateDescriptorSets");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, sets);
    if (result == VK_SUCCESS) observe(state, [&](evrm::GpuProbe& probe) { probe.sets_allocated(*info, sets); });
    if (result == VK_SUCCESS) observe_native(state, [&](evrm::NativeState& native) { native.sets_allocated(*info, sets); });
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice device, VkDescriptorPool pool, uint32_t count, const VkDescriptorSet* sets) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkFreeDescriptorSets>(state, "vkFreeDescriptorSets");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, pool, count, sets);
    if (result == VK_SUCCESS) observe(state, [&](evrm::GpuProbe& probe) { probe.sets_freed(count, sets); });
    if (result == VK_SUCCESS) observe_native(state, [&](evrm::NativeState& native) { native.sets_freed(count, sets); });
    return result;
}
EVRM_EXPORT VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice device, VkDescriptorPool pool, VkDescriptorPoolResetFlags flags) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkResetDescriptorPool>(state, "vkResetDescriptorPool");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, pool, flags);
    if (result == VK_SUCCESS) observe(state, [&](evrm::GpuProbe& probe) { probe.pool_reset(pool); });
    if (result == VK_SUCCESS) observe_native(state, [&](evrm::NativeState& native) { native.pool_reset(pool); });
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyDescriptorPool>(state, "vkDestroyDescriptorPool");
    if (!next) return;
    observe(state, [&](evrm::GpuProbe& probe) { probe.pool_reset(pool); });
    observe_native(state, [&](evrm::NativeState& native) { native.pool_reset(pool); });
    next(device, pool, allocator);
}
EVRM_EXPORT void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t write_count, const VkWriteDescriptorSet* writes,
                                                   uint32_t copy_count, const VkCopyDescriptorSet* copies) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkUpdateDescriptorSets>(state, "vkUpdateDescriptorSets");
    if (!next) return;
    next(device, write_count, writes, copy_count, copies);
    observe(state, [&](evrm::GpuProbe& probe) { probe.descriptors_written(write_count, writes); probe.descriptors_copied(copy_count, copies); });
    observe_native(state, [&](evrm::NativeState& native) { native.descriptors_written(write_count, writes); native.descriptors_copied(copy_count, copies); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkDescriptorUpdateTemplate* update) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkCreateDescriptorUpdateTemplate>(state, "vkCreateDescriptorUpdateTemplate");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = next(device, info, allocator, update);
    if (result == VK_SUCCESS) observe(state, [&](evrm::GpuProbe& probe) { probe.template_created(*update, *info); });
    if (result == VK_SUCCESS) observe_native(state, [&](evrm::NativeState& native) { native.template_created(*update, *info); });
    return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate update, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkDestroyDescriptorUpdateTemplate>(state, "vkDestroyDescriptorUpdateTemplate");
    if (!next) return;
    observe(state, [&](evrm::GpuProbe& probe) { probe.template_destroyed(update); });
    observe_native(state, [&](evrm::NativeState& native) { native.template_destroyed(update); });
    next(device, update, allocator);
}
EVRM_EXPORT void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet set, VkDescriptorUpdateTemplate update, const void* data) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkUpdateDescriptorSetWithTemplate>(state, "vkUpdateDescriptorSetWithTemplate");
    if (!next) return;
    next(device, set, update, data);
    observe(state, [&](evrm::GpuProbe& probe) { probe.template_written(set, update, data); });
    observe_native(state, [&](evrm::NativeState& native) { native.template_written(set, update, data); });
}
EVRM_EXPORT void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer command, VkPipelineBindPoint point, VkPipelineLayout layout,
    uint32_t first, uint32_t count, const VkDescriptorSet* sets, uint32_t dynamic_count, const uint32_t* dynamic_offsets) {
    const auto state = device_state(command);
    const auto next = device_proc<PFN_vkCmdBindDescriptorSets>(state, "vkCmdBindDescriptorSets");
    if (!next) return;
    native_before_command(state, command, evrm::DeferredRenderingCommand::independent_state);
    next(command, point, layout, first, count, sets, dynamic_count, dynamic_offsets);
    observe(state, [&](evrm::GpuProbe& probe) { probe.sets_bound(command, point, layout,first,count, sets, dynamic_count, dynamic_offsets); });
    observe_native(state, [&](evrm::NativeState& native) { native.sets_bound(command,point,layout,first,count,sets,dynamic_count,dynamic_offsets); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().bind_descriptor_sets(command, point, layout, first, count, sets, dynamic_count, dynamic_offsets); });
}
EVRM_EXPORT VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device,const VkPipelineLayoutCreateInfo* info,const VkAllocationCallbacks* allocator,VkPipelineLayout* layout) {
    const auto state=device_state(device);const auto next=device_proc<PFN_vkCreatePipelineLayout>(state,"vkCreatePipelineLayout");if(!next)return VK_ERROR_INITIALIZATION_FAILED;
    const auto result=next(device,info,allocator,layout);if(result==VK_SUCCESS&&state->native&&info&&layout){
        try{if(info->setLayoutCount==5 && info->pSetLayouts && state->native_pipeline_layout_notices.fetch_add(1)<24){
            std::ostringstream message;message<<"native-additive.pipeline-layout-declaration handle="<<*layout<<" flags="<<info->flags<<" next="<<bool(info->pNext)<<" sets=";
            for(uint32_t i=0;i<5;++i)message<<info->pSetLayouts[i]<<",";
            message<<" push="<<info->pushConstantRangeCount;if(info->pushConstantRangeCount<=32 && info->pPushConstantRanges)for(uint32_t i=0;i<info->pushConstantRangeCount;++i){const auto& r=info->pPushConstantRanges[i];message<<" ["<<r.stageFlags<<","<<r.offset<<","<<r.size<<"]";}
            evrm::log_event(message.str());}}catch(...){}
        state->native->pipeline_layout_created(*layout,*info);}return result;
}
EVRM_EXPORT void VKAPI_CALL vkDestroyPipelineLayout(VkDevice device,VkPipelineLayout layout,const VkAllocationCallbacks* allocator) {
    const auto state=device_state(device);const auto next=device_proc<PFN_vkDestroyPipelineLayout>(state,"vkDestroyPipelineLayout");if(!next)return;
    if(state->native)state->native->pipeline_layout_destroyed(layout);
    next(device,layout,allocator);
}
EVRM_EXPORT void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* queue) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkGetDeviceQueue>(state, "vkGetDeviceQueue");
    if (!next) return;
    next(device, family, index, queue);
    if (queue && *queue) observe(state, [&](evrm::GpuProbe& probe) { probe.queue_observed(*queue, family); });
    if (queue && *queue) {
        queue_mutex(state, *queue);
        observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.queue_observed(*queue, family); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().queue_known(*queue, family); });
    }
}
EVRM_EXPORT void VKAPI_CALL vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue) {
    const auto state = device_state(device);
    const auto next = device_proc<PFN_vkGetDeviceQueue2>(state, "vkGetDeviceQueue2");
    if (!next) return;
    next(device, info, queue);
    if (queue && *queue && !info->flags && !info->pNext) observe(state, [&](evrm::GpuProbe& probe) { probe.queue_observed(*queue, info->queueFamilyIndex); });
    if (queue && *queue) {
        queue_mutex(state, *queue);
        observe_graphics(state, [&](evrm::GraphicsState& graphics) { graphics.queue_observed(*queue, info->queueFamilyIndex); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.collector().queue_known(*queue, info->queueFamilyIndex); });
    }
}
EVRM_EXPORT VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue) {
    const auto state = device_state(queue);
    const auto next = device_proc<PFN_vkQueueWaitIdle>(state, "vkQueueWaitIdle");
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    const auto mutex = queue_mutex(state, queue);
    std::lock_guard<std::recursive_mutex> queue_guard(*mutex);
    const auto result = next(queue);
    if (result == VK_SUCCESS && state->frame_count.load() == 0)
        observe(state, [&](evrm::GpuProbe& probe) { probe.capture(queue, 0, true); });
    return result;
}
