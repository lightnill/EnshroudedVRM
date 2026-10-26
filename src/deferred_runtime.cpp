#include "deferred_runtime.hpp"
#include <cstring>
#include <string_view>
#include <utility>

namespace evrm {
bool rendering_independent_state(const char* name) noexcept {
    if(!name)return false;
    // Khronos command properties mark these exact commands as State with
    // Render Pass Scope Both. Do not infer safety from a vkCmdSet prefix:
    // events and dynamic-rendering attachment mappings must still flush.
    static constexpr std::array<std::string_view,10> independent{{
        "vkCmdBindPipeline","vkCmdBindDescriptorSets","vkCmdBindVertexBuffers","vkCmdBindIndexBuffer",
        "vkCmdPushConstants","vkCmdSetViewport","vkCmdSetScissor","vkCmdSetStencilReference",
        "vkCmdSetStencilCompareMask","vkCmdSetStencilWriteMask"
    }};
    for(const auto command:independent)if(std::strcmp(name,command.data())==0)return true;
    return false;
}
bool rendering_command_coverage_supported(std::uint32_t version,const DeviceFeaturesObservation& observation) noexcept {
    if(!observation.complete||observation.status!=DeviceFeatureStatus::ok||
       observation.enabled_extensions.size()>device_feature_extension_limit||
       observation.declared_extension_count!=observation.enabled_extensions.size())return false;
    if(!version)version=VK_API_VERSION_1_0;
    // Core patch revisions do not add command entry points.
    if(VK_API_VERSION_VARIANT(version)!=0||VK_API_VERSION_MAJOR(version)!=1||VK_API_VERSION_MINOR(version)>4)return false;
    // ID2 adds presentation metadata; wait2 adds only the host command
    // vkWaitForPresent2KHR. Neither adds a command-buffer recording entry.
    static constexpr std::array<std::string_view,23> allowed{{
        "VK_KHR_swapchain","VK_KHR_swapchain_maintenance1","VK_EXT_shader_demote_to_helper_invocation",
        "VK_KHR_shader_non_semantic_info","VK_KHR_dynamic_rendering","VK_EXT_memory_priority",
        "VK_EXT_pageable_device_local_memory","VK_EXT_memory_budget","VK_KHR_external_semaphore_win32",
        "VK_KHR_external_memory_win32","VK_EXT_image_robustness","VK_KHR_device_group",
        "VK_KHR_robustness2","VK_EXT_mesh_shader","VK_KHR_acceleration_structure",
        "VK_KHR_deferred_host_operations","VK_KHR_ray_query","VK_NVX_binary_import",
        "VK_NVX_image_view_handle","VK_KHR_buffer_device_address","VK_KHR_push_descriptor",
        "VK_KHR_present_id2","VK_KHR_present_wait2"
    }};
    for(const auto& extension:observation.enabled_extensions) {
        const std::string_view name(extension.data(),extension.size());bool found=false;
        for(const auto candidate:allowed)if(name==candidate){found=true;break;}
        if(!found)return false;
    }
    return true;
}
bool rendering_scope_sensitive(const char* name) noexcept {
    if(!name)return true;
    static constexpr std::array<std::string_view,18> scopes{{
        "vkCmdBeginQuery","vkCmdEndQuery","vkCmdBeginQueryIndexedEXT","vkCmdEndQueryIndexedEXT",
        "vkCmdBeginConditionalRenderingEXT","vkCmdEndConditionalRenderingEXT",
        "vkCmdBeginTransformFeedbackEXT","vkCmdEndTransformFeedbackEXT","vkCmdExecuteCommands",
        "vkCmdBeginRenderPass","vkCmdEndRenderPass","vkCmdNextSubpass",
        "vkCmdBeginRenderPass2","vkCmdBeginRenderPass2KHR","vkCmdEndRenderPass2","vkCmdEndRenderPass2KHR",
        "vkCmdNextSubpass2","vkCmdNextSubpass2KHR"
    }};
    for(const auto scope:scopes)if(std::strncmp(name,scope.data(),scope.size()+1)==0)return true;
    return false;
}

DeferredRenderingRuntime::Pool* DeferredRenderingRuntime::find_pool(VkCommandPool handle) noexcept {
    if(handle)for(auto& pool:pools_)if(pool.handle==handle)return &pool;
    return nullptr;
}
DeferredRenderingRuntime::Command* DeferredRenderingRuntime::find_command(VkCommandBuffer handle) noexcept {
    if(handle)for(auto& command:commands_)if(command.handle==handle)return &command;
    return nullptr;
}
void DeferredRenderingRuntime::reset_recording(Command& command) noexcept {
    command.pending.discard();command.begin=nullptr;
    command.recording=false;command.rendering_active=false;command.tainted=false;command.one_time=false;
}
void DeferredRenderingRuntime::retire(Command& command) noexcept {
    reset_recording(command);command.handle=VK_NULL_HANDLE;command.pool=VK_NULL_HANDLE;
    command.primary=false;command.pool_eligible=false;command.family=UINT32_MAX;
}
void DeferredRenderingRuntime::retire_pool_commands(VkCommandPool pool) noexcept {
    for(auto& command:commands_)if(command.handle&&command.pool==pool)retire(command);
}
void DeferredRenderingRuntime::pool_created(VkCommandPool handle,const VkCommandPoolCreateInfo& info,VkQueueFlags family) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);if(!handle)return;
        retire_pool_commands(handle);auto* found=find_pool(handle);
        if(!found)for(auto& pool:pools_)if(!pool.handle){found=&pool;break;}
        if(!found)return;
        constexpr auto supported=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT|VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        constexpr auto required=VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT;
        found->handle=handle;found->family=info.queueFamilyIndex;found->eligible=info.sType==VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO&&!info.pNext&&
            !(info.flags&~supported)&&(family&required)==required;
    }catch(...){}
}
void DeferredRenderingRuntime::pool_destroyed(VkCommandPool handle) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);if(!handle)return;
        retire_pool_commands(handle);if(auto* pool=find_pool(handle))*pool={};
    }catch(...){}
}
void DeferredRenderingRuntime::pool_reset(VkCommandPool handle,VkResult result) noexcept {
    if(result!=VK_SUCCESS||!handle)return;
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& command:commands_)if(command.handle&&command.pool==handle)reset_recording(command);
    }catch(...){}
}
void DeferredRenderingRuntime::allocated(const VkCommandBufferAllocateInfo& info,const VkCommandBuffer* handles) noexcept {
    if(!handles)return;
    try {
        std::lock_guard<std::mutex> lock(mutex_);const auto* pool=find_pool(info.commandPool);
        const bool supported=info.sType==VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO&&!info.pNext&&
            info.commandBufferCount<=max_commands&&
            (info.level==VK_COMMAND_BUFFER_LEVEL_PRIMARY||info.level==VK_COMMAND_BUFFER_LEVEL_SECONDARY)&&pool;
        // Metadata stays bounded even when a legal native array is larger.
        // Matching old identities must still retire anywhere in that array.
        for(std::uint32_t i=0;i<info.commandBufferCount;++i) {
            const auto handle=handles[i];if(!handle)continue;
            if(auto* old=find_command(handle))retire(*old);
            if(!supported)continue;
            for(auto& command:commands_)if(!command.handle) {
                command.handle=handle;command.pool=info.commandPool;
                command.primary=info.level==VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.pool_eligible=pool->eligible;command.family=pool->family;break;
            }
        }
    }catch(...){}
}
void DeferredRenderingRuntime::command_begin(VkCommandBuffer handle,const VkCommandBufferBeginInfo& info,VkResult result) noexcept {
    if(result!=VK_SUCCESS)return;
    try {
        std::lock_guard<std::mutex> lock(mutex_);auto* command=find_command(handle);if(!command)return;
        reset_recording(*command);command->recording=true;
        constexpr VkCommandBufferUsageFlags supported=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        command->tainted=info.sType!=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO||info.pNext||(info.flags&~supported);
        command->one_time=info.flags==VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        // pInheritanceInfo is ignored for primaries; secondaries never defer.
    }catch(...){}
}
void DeferredRenderingRuntime::command_reset(VkCommandBuffer handle,VkResult result) noexcept {
    if(result!=VK_SUCCESS)return;
    try {std::lock_guard<std::mutex> lock(mutex_);if(auto* command=find_command(handle))reset_recording(*command);}catch(...){}
}
void DeferredRenderingRuntime::command_freed(std::uint32_t count,const VkCommandBuffer* handles) noexcept {
    if(!handles)return;
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        for(std::uint32_t i=0;i<count;++i)if(auto* command=find_command(handles[i]))retire(*command);
    }catch(...){}
}
void DeferredRenderingRuntime::command_end(VkCommandBuffer handle,VkResult) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);if(auto* command=find_command(handle)) {
            command->recording=false;command->rendering_active=false;command->tainted=true;
            // Pending may only be discarded by a reset or retirement. If the
            // caller failed to flush before End, still return its exact begin.
        }
    }catch(...){}
}
bool DeferredRenderingRuntime::begin_rendering(VkCommandBuffer handle,const VkRenderingInfo& info,
                                               PFN_vkCmdBeginRendering entry,bool external_eligible) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);auto* command=find_command(handle);
        if(!command||!command->recording)return false;
        const bool nested=command->rendering_active;command->rendering_active=true;
        if(nested){command->tainted=true;return false;}
        if(!external_eligible||!entry||!command->primary||!command->pool_eligible||command->tainted)return false;
        if(!command->pending.try_defer(info,true))return false;
        command->begin=entry;return true;
    }catch(...){return false;}
}
std::optional<NativeRenderingRelease> DeferredRenderingRuntime::before(VkCommandBuffer handle,DeferredRenderingCommand kind,bool taint) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);auto* command=find_command(handle);if(!command)return {};
        if(taint)command->tainted=true;
        if(command->tainted||!command->recording||!command->rendering_active)kind=DeferredRenderingCommand::other;
        auto release=command->pending.before(kind);if(!release)return {};
        const auto entry=command->begin;command->begin=nullptr;
        return NativeRenderingRelease{handle,entry,std::move(*release)};
    }catch(...){return {};}
}
void DeferredRenderingRuntime::rendering_ended(VkCommandBuffer handle) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);if(auto* command=find_command(handle)) {
            command->rendering_active=false;if(command->pending.pending())command->tainted=true;
        }
    }catch(...){}
}
std::optional<std::uint32_t> DeferredRenderingRuntime::pose_copy_family(VkCommandBuffer handle)noexcept {
    try{std::lock_guard<std::mutex> lock(mutex_);const auto* command=find_command(handle);
        if(command&&command->primary&&command->pool_eligible&&command->recording&&command->one_time&&
           !command->rendering_active&&!command->tainted&&command->family!=UINT32_MAX)return command->family;
    }catch(...){}return {};
}
} // namespace evrm
