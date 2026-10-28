#define VK_NO_PROTOTYPES
#include "runtime_log.hpp"
#include "gpu_probe.hpp"
#include "graphics_state.hpp"
#include "avatar_renderer.hpp"
#include "device_features.hpp"
#include "command_trace_runtime.hpp"
#include "deferred_runtime.hpp"
#include "native_backend.hpp"
#include "native_clip_pipelines.hpp"
#include "native_buffer_memory.hpp"
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <map>
#include <vector>
#include <sstream>

#define EVRM_EXPORT extern "C" __declspec(dllexport)
EVRM_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*);
EVRM_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char*);
EVRM_EXPORT PFN_vkVoidFunction VKAPI_CALL evrmGetPhysicalDeviceProcAddr(VkInstance, const char*);
EVRM_EXPORT VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
EVRM_EXPORT void VKAPI_CALL vkDestroyInstance(VkInstance, const VkAllocationCallbacks*);
EVRM_EXPORT VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
EVRM_EXPORT void VKAPI_CALL vkDestroyDevice(VkDevice, const VkAllocationCallbacks*);
EVRM_EXPORT VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR*);

namespace {
struct InstanceState {
    VkInstance handle{};
    PFN_vkGetInstanceProcAddr get_proc{};
    PFN_GetPhysicalDeviceProcAddr get_physical_proc{};
    PFN_vkDestroyInstance destroy{};
    uint32_t api_version = VK_API_VERSION_1_0;
};
struct DeviceState {
    VkDevice handle{};
    PFN_vkGetDeviceProcAddr get_proc{};
    PFN_vkDestroyDevice destroy{};
    PFN_vkQueuePresentKHR present{};
    std::atomic<uint64_t> frame_count{0};
    std::shared_ptr<evrm::GpuProbe> probe;
    std::shared_ptr<evrm::GraphicsState> graphics;
    std::shared_ptr<evrm::AvatarRenderer> avatar;
    std::shared_ptr<evrm::CommandTraceRuntime> trace;
    std::shared_ptr<evrm::DeferredRenderingRuntime> deferred;
    std::shared_ptr<evrm::NativeBackend> native;
    std::array<VkQueueFlags, 64> deferred_family_flags{};
    std::atomic<uint64_t> deferred_releases{0};
    std::atomic<uint32_t> native_layout_notices{0},native_pipeline_layout_notices{0};
    std::mutex present_timing_mutex;
    double driver_present_ms=0;
    unsigned driver_present_samples=0;
    std::mutex trace_pool_mutex;
    std::unordered_map<VkCommandPool, uint32_t> trace_pools;
    std::mutex queue_table_mutex;
    std::map<VkQueue, std::shared_ptr<std::recursive_mutex>> queue_mutexes;
};
std::mutex state_mutex;
std::unordered_map<void*, std::shared_ptr<InstanceState>> instances;
std::unordered_map<void*, std::shared_ptr<DeviceState>> devices;

template<class Handle> void* dispatch_key(Handle handle) {
    return handle ? *reinterpret_cast<void**>(handle) : nullptr;
}
template<class Handle> std::shared_ptr<InstanceState> instance_state(Handle handle) {
    std::lock_guard<std::mutex> guard(state_mutex);
    const auto found = instances.find(dispatch_key(handle));
    return found == instances.end() ? nullptr : found->second;
}
template<class Handle> std::shared_ptr<DeviceState> device_state(Handle handle) {
    std::lock_guard<std::mutex> guard(state_mutex);
    const auto found = devices.find(dispatch_key(handle));
    return found == devices.end() ? nullptr : found->second;
}
template<class Chain> Chain* find_link(const void* next, VkStructureType type) {
    auto current = static_cast<const VkBaseInStructure*>(next);
    while (current) {
        if (current->sType == type) {
            auto chain = reinterpret_cast<const Chain*>(current);
            if (chain->function == VK_LAYER_LINK_INFO) return const_cast<Chain*>(chain);
        }
        current = current->pNext;
    }
    return nullptr;
}
template<class Function> PFN_vkVoidFunction as_proc(Function function) {
    return reinterpret_cast<PFN_vkVoidFunction>(function);
}
template<class Function> Function device_proc(const std::shared_ptr<DeviceState>& state, const char* name) {
    return state ? reinterpret_cast<Function>(state->get_proc(state->handle, name)) : nullptr;
}
template<class Function> void observe(const std::shared_ptr<DeviceState>& state, Function function) noexcept {
    if (!state || !state->probe) return;
    try { function(*state->probe); }
    catch (...) { evrm::log_event("probe.observation.failed"); }
}
PFN_vkSetDeviceLoaderData device_loader_callback(const void* next) {
    auto current = static_cast<const VkBaseInStructure*>(next);
    while (current) {
        if (current->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
            const auto chain = reinterpret_cast<const VkLayerDeviceCreateInfo*>(current);
            if (chain->function == VK_LOADER_DATA_CALLBACK) return chain->u.pfnSetDeviceLoaderData;
        }
        current = current->pNext;
    }
    return nullptr;
}
template<class Function> void observe_graphics(const std::shared_ptr<DeviceState>& state, Function function) noexcept {
    if (!state || !state->graphics) return;
    try { function(*state->graphics); }
    catch (...) { evrm::log_event("graphics.observation.failed"); }
}
template<class Function> void observe_trace(const std::shared_ptr<DeviceState>& state, Function function) noexcept {
    if (!state || !state->trace) return;
    try { function(*state->trace); }
    catch (...) { evrm::log_event("command-trace.observation.failed"); }
}
template<class Function> void observe_native(const std::shared_ptr<DeviceState>& state, Function function) noexcept {
    if (!state || !state->native) return;
    try { function(state->native->state()); }
    catch (...) { evrm::log_event("native-additive.observation.failed"); }
}
// This proof moves only independent binding commands before the single exact
// native BeginRendering. It inserts no private work and suppresses no draws.
void native_before_command(const std::shared_ptr<DeviceState>& state, VkCommandBuffer command,
                           evrm::DeferredRenderingCommand kind = evrm::DeferredRenderingCommand::other,
                           bool taint = false,const char* trigger=nullptr) noexcept {
    if (!state) return;
    if (taint && state->native) state->native->scope_unsupported(command);
    if (!state->deferred) return;
    const auto released = state->deferred->before(command, kind, taint);
    if (!released) return;
    const auto view = released->release.begin.call_view();
    if (state->native) state->native->before_rendering(command, view,trigger);
    released->begin(released->command, &view);
    const auto count = ++state->deferred_releases;
    if (count == 1) evrm::log_event("deferred-rendering.first-release native-begin-once; no private preparation");
}
void native_before_forwarded_command(const std::shared_ptr<DeviceState>& state, VkCommandBuffer command,
                                     const char* name) noexcept {
    observe_native(state,[&](evrm::NativeState& native){native.raster_unobserved(command,name);});
    if(state && state->native && evrm::native_state_command_unobserved(name))state->native->scope_unsupported(command,name);
    const auto kind = evrm::rendering_independent_state(name)
        ? evrm::DeferredRenderingCommand::independent_state : evrm::DeferredRenderingCommand::other;
    native_before_command(state, command, kind,
                          evrm::rendering_scope_sensitive(name),name);
    if(state&&state->native&&name&&!std::strcmp(name,"vkCmdClearAttachments"))state->native->before_attachment_clear(command);
    if(state&&state->native&&name&&std::strncmp(name,"vkCmdDraw",9)==0)state->native->uncovered_draw(command,name);
}
void native_after_forwarded_command(const std::shared_ptr<DeviceState>& state,VkCommandBuffer command,const char* name) noexcept {
    if(state&&state->native&&name&&std::strncmp(name,"vkCmdDraw",9)==0)state->native->after_native_draw(command);
}
void native_begin_rendering(const std::shared_ptr<DeviceState>& state, VkCommandBuffer command,
                            const VkRenderingInfo* info, PFN_vkCmdBeginRendering exact_next) noexcept {
    native_before_command(state, command);
    if (state && state->native && info) state->native->incoming_rendering(command, *info);
    if (!state || !state->deferred || !info ||
        !state->deferred->begin_rendering(command, *info, exact_next, true)) {
        if (state && state->native) state->native->rendering_unavailable(command);
        exact_next(command, info);
    }
}
uint32_t trace_pool_family(const std::shared_ptr<DeviceState>& state, VkCommandPool pool) noexcept {
    try {
        std::lock_guard<std::mutex> guard(state->trace_pool_mutex);
        const auto found = state->trace_pools.find(pool);
        return found == state->trace_pools.end() ? UINT32_MAX : found->second;
    } catch (...) { return UINT32_MAX; }
}
std::shared_ptr<std::recursive_mutex> queue_mutex(const std::shared_ptr<DeviceState>& state, VkQueue queue) {
    std::lock_guard<std::mutex> guard(state->queue_table_mutex);
    auto& mutex = state->queue_mutexes[queue];
    if (!mutex) mutex = std::make_shared<std::recursive_mutex>();
    return mutex;
}
struct AllQueueLocks {
    std::unique_lock<std::mutex> table;
    std::vector<std::unique_lock<std::recursive_mutex>> locks;
    explicit AllQueueLocks(const std::shared_ptr<DeviceState>& state) : table(state->queue_table_mutex) {
        for (const auto& item : state->queue_mutexes) locks.emplace_back(*item.second);
    }
};

void log_device_capabilities(const std::shared_ptr<InstanceState>& parent, VkPhysicalDevice physical,
                             const VkDeviceCreateInfo& info,
                             const evrm::DeviceFeaturesObservation& features) noexcept {
    try {
        std::string event = "device.features status=" + std::to_string(static_cast<unsigned>(features.status)) +
            " complete=" + std::to_string(features.complete);
        const auto append = [&](const char* name, const evrm::DeviceFeatureFlag& flag) {
            event += " " + std::string(name) + "=" + (flag.available ? (flag.enabled ? "1" : "0") : "unavailable") +
                     " source=" + std::to_string(static_cast<unsigned>(flag.source));
        };
        append("geometryShader", features.geometry_shader);
        append("independentBlend", features.independent_blend);
        append("dynamicRendering", features.dynamic_rendering);
        append("meshShader", features.mesh_shader);
        append("descriptorBindingUniformBufferUpdateAfterBind", features.uniform_buffer_update_after_bind);
        append("descriptorBindingStorageBufferUpdateAfterBind", features.storage_buffer_update_after_bind);
        append("descriptorBindingUpdateUnusedWhilePending", features.update_unused_while_pending);
        append("descriptorBindingPartiallyBound", features.descriptor_binding_partially_bound);
        event += " base=" + std::to_string(features.p_enabled_features_present) +
                 " features2=" + std::to_string(features.features2_present) +
                 " vulkan12=" + std::to_string(features.vulkan12_present) +
                 " indexing=" + std::to_string(features.descriptor_indexing_present) + " chain=";
        for (const auto type : features.chain_s_types) event += std::to_string(static_cast<int>(type)) + ",";
        evrm::log_event(event);
        for (const auto& extension : features.enabled_extensions) evrm::log_event("device.extension name=" + extension);

        const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            parent->get_proc(parent->handle, "vkGetPhysicalDeviceProperties"));
        if (properties) {
            VkPhysicalDeviceProperties values{};
            properties(physical, &values);
            evrm::log_event("device.limits maxBoundDescriptorSets=" + std::to_string(values.limits.maxBoundDescriptorSets) +
                " maxPerStageStorageBuffers=" + std::to_string(values.limits.maxPerStageDescriptorStorageBuffers) +
                " maxDescriptorSetStorageBuffers=" + std::to_string(values.limits.maxDescriptorSetStorageBuffers) +
                " maxDescriptorSetUniformBuffers=" + std::to_string(values.limits.maxDescriptorSetUniformBuffers) +
                " maxPerStageUniformBuffers=" + std::to_string(values.limits.maxPerStageDescriptorUniformBuffers) +
                " maxUniformBufferRange=" + std::to_string(values.limits.maxUniformBufferRange) +
                " maxStorageBufferRange=" + std::to_string(values.limits.maxStorageBufferRange) +
                " minUniformBufferOffsetAlignment=" + std::to_string(values.limits.minUniformBufferOffsetAlignment) +
                " minStorageBufferOffsetAlignment=" + std::to_string(values.limits.minStorageBufferOffsetAlignment) +
                " apiVersion=" + std::to_string(values.apiVersion));
        }
        const auto queues = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            parent->get_proc(parent->handle, "vkGetPhysicalDeviceQueueFamilyProperties"));
        if (queues && info.queueCreateInfoCount <= 64 && (info.pQueueCreateInfos || !info.queueCreateInfoCount)) {
            uint32_t count = 0;
            queues(physical, &count, nullptr);
            if (count && count <= 256) {
                std::vector<VkQueueFamilyProperties> values(count);
                queues(physical, &count, values.data());
                if (count <= values.size()) for (uint32_t i = 0; i < info.queueCreateInfoCount; ++i) {
                    const auto& requested = info.pQueueCreateInfos[i];
                    if (requested.queueFamilyIndex < count)
                        evrm::log_event("device.queue family=" + std::to_string(requested.queueFamilyIndex) +
                            " flags=" + std::to_string(values[requested.queueFamilyIndex].queueFlags) +
                            " requested=" + std::to_string(requested.queueCount));
                }
            }
        }
    } catch (...) { evrm::log_event("device.capabilities.observation.failed"); }
}
}

EVRM_EXPORT VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* info, const VkAllocationCallbacks* allocator, VkInstance* instance) {
    if (!info || !instance) return VK_ERROR_INITIALIZATION_FAILED;
    auto chain = find_link<VkLayerInstanceCreateInfo>(info->pNext, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO);
    if (!chain || !chain->u.pLayerInfo || !chain->u.pLayerInfo->pfnNextGetInstanceProcAddr)
        return VK_ERROR_INITIALIZATION_FAILED;
    const auto link = chain->u.pLayerInfo;
    const auto next_get_proc = link->pfnNextGetInstanceProcAddr;
    const auto next_physical_proc = link->pfnNextGetPhysicalDeviceProcAddr;
    const auto next_create = reinterpret_cast<PFN_vkCreateInstance>(next_get_proc(nullptr, "vkCreateInstance"));
    if (!next_create) return VK_ERROR_INITIALIZATION_FAILED;
    chain->u.pLayerInfo = link->pNext;
    const VkResult result = next_create(info, allocator, instance);
    if (result != VK_SUCCESS) return result;
    const auto next_destroy = reinterpret_cast<PFN_vkDestroyInstance>(next_get_proc(*instance, "vkDestroyInstance"));
    try {
        auto state = std::make_shared<InstanceState>();
        state->handle = *instance;
        state->get_proc = next_get_proc;
        state->get_physical_proc = next_physical_proc;
        state->destroy = next_destroy;
        if (info->pApplicationInfo && info->pApplicationInfo->apiVersion)
            state->api_version = info->pApplicationInfo->apiVersion;
        {
            std::lock_guard<std::mutex> guard(state_mutex);
            instances.emplace(dispatch_key(*instance), std::move(state));
        }
        evrm::log_event("instance.created app=" + std::string(info->pApplicationInfo && info->pApplicationInfo->pApplicationName
                        ? info->pApplicationInfo->pApplicationName : "unknown"));
        return result;
    } catch (...) {
        if (next_destroy) next_destroy(*instance, allocator);
        *instance = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
}

EVRM_EXPORT void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    const auto state = instance_state(instance);
    if (!state) return;
    {
        std::lock_guard<std::mutex> guard(state_mutex);
        instances.erase(dispatch_key(instance));
    }
    state->destroy(instance, allocator);
    evrm::log_event("instance.destroyed");
}

EVRM_EXPORT VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physical, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator, VkDevice* device) {
    if (!info || !device) return VK_ERROR_INITIALIZATION_FAILED;
    auto chain = find_link<VkLayerDeviceCreateInfo>(info->pNext, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO);
    const auto parent = instance_state(physical);
    if (!chain || !chain->u.pLayerInfo || !parent) return VK_ERROR_INITIALIZATION_FAILED;
    const auto link = chain->u.pLayerInfo;
    const auto next_get_proc = link->pfnNextGetDeviceProcAddr;
    const auto set_loader_data = device_loader_callback(info->pNext);
    const auto next_create = reinterpret_cast<PFN_vkCreateDevice>(link->pfnNextGetInstanceProcAddr(parent->handle, "vkCreateDevice"));
    if (!next_get_proc || !next_create) return VK_ERROR_INITIALIZATION_FAILED;
    const auto features = evrm::inspect_device_features(*info);
    chain->u.pLayerInfo = link->pNext;
    const VkResult result = next_create(physical, info, allocator, device);
    if (result != VK_SUCCESS) return result;
    log_device_capabilities(parent, physical, *info, features);
    const auto next_destroy = reinterpret_cast<PFN_vkDestroyDevice>(next_get_proc(*device, "vkDestroyDevice"));
    try {
        auto state = std::make_shared<DeviceState>();
        state->handle = *device;
        state->get_proc = next_get_proc;
        state->destroy = next_destroy;
        state->present = reinterpret_cast<PFN_vkQueuePresentKHR>(next_get_proc(*device, "vkQueuePresentKHR"));
        state->graphics = std::make_shared<evrm::GraphicsState>(*device, next_get_proc);
        try {
            wchar_t enabled[4]{};
            if (GetEnvironmentVariableW(L"ENSHROUDED_VRM_DEFER_RENDERING_PROOF", enabled, 4) == 1 && enabled[0] == L'1') {
                if (evrm::rendering_command_coverage_supported(parent->api_version, features)) {
                    const auto get_families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
                        parent->get_proc(parent->handle, "vkGetPhysicalDeviceQueueFamilyProperties"));
                    uint32_t count = 0;
                    if (get_families) get_families(physical, &count, nullptr);
                    if (count && count <= state->deferred_family_flags.size()) {
                        std::array<VkQueueFamilyProperties, 64> families{};
                        const auto capacity = count;
                        get_families(physical, &count, families.data());
                        if (count <= capacity) {
                            for (uint32_t i = 0; i < count; ++i) state->deferred_family_flags[i] = families[i].queueFlags;
                            state->deferred = std::make_shared<evrm::DeferredRenderingRuntime>();
                            evrm::log_event("deferred-rendering.enabled command-order-proof-only; no private preparation or suppression");
                        }
                    }
                } else evrm::log_event("deferred-rendering.declined command coverage unavailable");
            }
        } catch (...) { evrm::log_event("deferred-rendering.initialization.failed"); }
        try {
            wchar_t enabled[4]{}, directory[32768]{};
            const auto flag_size = GetEnvironmentVariableW(L"ENSHROUDED_VRM_TRACE_COMMANDS", enabled, 4);
            const auto directory_size = GetEnvironmentVariableW(L"ENSHROUDED_VRM_TRACE_DIR", directory, 32768);
            if (flag_size == 1 && enabled[0] == L'1' && directory_size && directory_size < 32768) {
                state->trace = std::make_shared<evrm::CommandTraceRuntime>(std::filesystem::path(directory));
                evrm::log_event("command-trace.enabled metadata-only; marker required");
            }
        } catch (...) { evrm::log_event("command-trace.initialization.failed"); }
        try {
            state->probe = std::make_shared<evrm::GpuProbe>(parent->handle, physical, *device, parent->get_proc, next_get_proc, set_loader_data);
        } catch (...) { evrm::log_event("probe.initialization.failed; original rendering retained"); }
        try {
            state->avatar = std::make_shared<evrm::AvatarRenderer>(parent->handle, physical, *device, parent->get_proc, next_get_proc, set_loader_data);
        } catch (const std::exception& error) { evrm::log_event("avatar.initialization.failed " + std::string(error.what())); }
        try {
            wchar_t native_flag[4]{},replacement_flag[4]{},shader_path[32768]{};
            const bool replacement=GetEnvironmentVariableW(L"ENSHROUDED_VRM_NATIVE_REPLACEMENT",replacement_flag,4)==1&&replacement_flag[0]==L'1';
            const bool additive=GetEnvironmentVariableW(L"ENSHROUDED_VRM_NATIVE_ADDITIVE",native_flag,4)==1&&native_flag[0]==L'1';
            if(additive||replacement)
                evrm::log_event("native-additive.gate request-api="+std::to_string(parent->api_version)+" device-shape="+std::to_string(evrm::native_rendering_environment_supported(parent->api_version,features)));
            if ((additive||replacement) && state->deferred && state->avatar && state->avatar->enabled() &&
                evrm::native_rendering_environment_supported(parent->api_version,features) &&
                GetEnvironmentVariableW(L"ENSHROUDED_VRM_SHADERS",shader_path,32768)>0) {
                state->native=std::make_shared<evrm::NativeBackend>(parent->handle,physical,*device,parent->get_proc,next_get_proc,set_loader_data,std::filesystem::path(shader_path),true,replacement,
                    features.descriptor_binding_partially_bound.available&&features.descriptor_binding_partially_bound.enabled,
                    features.complete&&features.mesh_shader.available&&features.mesh_shader.enabled&&std::find(features.enabled_extensions.begin(),features.enabled_extensions.end(),"VK_EXT_mesh_shader")!=features.enabled_extensions.end());
                evrm::log_event(replacement?"native-replacement.enabled development-only; shared-commit; opaque-mask-only":"native-additive.enabled original-body-retained; invalid-history; opaque-mask-only");
            }
        } catch (...) { evrm::log_event("native-additive.initialization.failed original rendering retained"); }
        {
            std::lock_guard<std::mutex> guard(state_mutex);
            devices.emplace(dispatch_key(*device), std::move(state));
        }
        evrm::log_event("device.created");
        return result;
    } catch (...) {
        if (next_destroy) next_destroy(*device, allocator);
        *device = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
}

EVRM_EXPORT void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    const auto state = device_state(device);
    if (!state) return;
    AllQueueLocks queues(state);
    {
        std::lock_guard<std::mutex> guard(state_mutex);
        devices.erase(dispatch_key(device));
    }
    state->native.reset();
    state->avatar.reset();
    state->probe.reset();
    state->deferred.reset();
    state->destroy(device, allocator);
    evrm::log_event("device.destroyed frames=" + std::to_string(state->frame_count.load()));
}

EVRM_EXPORT VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    const auto state = device_state(queue);
    if (!state || !state->present) return VK_ERROR_INITIALIZATION_FAILED;
    const auto mutex = queue_mutex(state, queue);
    std::lock_guard<std::recursive_mutex> queue_guard(*mutex);
    const auto frame = state->frame_count.fetch_add(1) + 1;
    bool consumed = false;
    bool native_pose_complete = false;
    const auto native_buffer_epoch=state->native?state->native->state().buffer_epoch():0;
    if (info && state->avatar && state->probe && state->graphics)
        native_pose_complete=state->avatar->before_present(queue, *info, *state->probe, *state->graphics, consumed,native_buffer_epoch);
    if (state->native && state->avatar && state->probe) {
        uint32_t family=UINT32_MAX;
        const auto publication=native_pose_complete?state->avatar->native_publication():nullptr;
        if(state->probe->queue_family(queue,family))state->native->publish(queue,family,publication,publication?publication->source_buffer_epoch:0);
    }
    if (info && (!state->avatar || !state->avatar->enabled()))
        observe(state, [&](evrm::GpuProbe& probe) { probe.capture(queue, frame, false,
            consumed ? 0 : info->waitSemaphoreCount, consumed ? nullptr : info->pWaitSemaphores, &consumed); });
    VkPresentInfoKHR actual{};
    if (consumed) { actual = *info; actual.waitSemaphoreCount = 0; actual.pWaitSemaphores = nullptr; }
    observe(state, [&](evrm::GpuProbe& probe) { probe.finish_frame(); });
    observe_trace(state, [&](evrm::CommandTraceRuntime& trace) { trace.finish_frame(frame); });
    const auto present_started=std::chrono::steady_clock::now();
    const auto result = state->present(queue, consumed ? &actual : info);
    try{if(state->native){
        double average=-1;
        {std::lock_guard<std::mutex> timing_guard(state->present_timing_mutex);
            state->driver_present_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-present_started).count();
            if(++state->driver_present_samples==300){average=state->driver_present_ms/300;state->driver_present_ms=0;state->driver_present_samples=0;}
        }
        if(average>=0)evrm::log_event("native-replacement.driver-present-ms average="+std::to_string(average));
    }
    if (frame == 1 || frame % 600 == 0)
        evrm::log_event("frame.presented count=" + std::to_string(frame) + " result=" + std::to_string(result));
    }catch(...){} // Diagnostics cannot alter the completed driver Present result.
    return result;
}

#include "runtime_hooks.inl"
#include "graphics_hooks.inl"
#include "command_trace_hooks.inl"
#include "command_forwarders.generated.inl"

EVRM_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) return nullptr;
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) return as_proc(vkGetInstanceProcAddr);
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return as_proc(vkGetDeviceProcAddr);
    if (std::strcmp(name, "vkCreateInstance") == 0) return as_proc(vkCreateInstance);
    if (std::strcmp(name, "vkDestroyInstance") == 0) return as_proc(vkDestroyInstance);
    if (std::strcmp(name, "vkCreateDevice") == 0) return as_proc(vkCreateDevice);
    if (std::strcmp(name, "vkDestroyDevice") == 0) return as_proc(vkDestroyDevice);
    const auto state = instance_state(instance);
    if (!state) return nullptr;
#define EVRM_INSTANCE_HOOK(name_) if (std::strcmp(name, #name_) == 0 && state->get_proc(instance, name)) return as_proc(name_);
    EVRM_DEVICE_HOOK_NAMES(EVRM_INSTANCE_HOOK)
    EVRM_GRAPHICS_HOOK_NAMES(EVRM_INSTANCE_HOOK)
    EVRM_TRACE_HOOK_NAMES(EVRM_INSTANCE_HOOK)
    EVRM_FORWARD_COMMAND_HOOK_NAMES(EVRM_INSTANCE_HOOK)
#undef EVRM_INSTANCE_HOOK
    if (std::strcmp(name, "vkQueuePresentKHR") == 0 && state->get_proc(instance, name)) return as_proc(vkQueuePresentKHR);
    return state->get_proc(instance, name);
}

EVRM_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    if (!name) return nullptr;
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return as_proc(vkGetDeviceProcAddr);
    const auto state = device_state(device);
    if (!state) return nullptr;
#define EVRM_DEVICE_HOOK(name_) if (std::strcmp(name, #name_) == 0 && state->get_proc(device, name)) return as_proc(name_);
    EVRM_DEVICE_HOOK_NAMES(EVRM_DEVICE_HOOK)
    EVRM_GRAPHICS_HOOK_NAMES(EVRM_DEVICE_HOOK)
    EVRM_TRACE_HOOK_NAMES(EVRM_DEVICE_HOOK)
    EVRM_FORWARD_COMMAND_HOOK_NAMES(EVRM_DEVICE_HOOK)
#undef EVRM_DEVICE_HOOK
    if (std::strcmp(name, "vkCreateDescriptorUpdateTemplateKHR") == 0 && state->get_proc(device, "vkCreateDescriptorUpdateTemplate")) return as_proc(vkCreateDescriptorUpdateTemplate);
    if (std::strcmp(name, "vkDestroyDescriptorUpdateTemplateKHR") == 0 && state->get_proc(device, "vkDestroyDescriptorUpdateTemplate")) return as_proc(vkDestroyDescriptorUpdateTemplate);
    if (std::strcmp(name, "vkUpdateDescriptorSetWithTemplateKHR") == 0 && state->get_proc(device, "vkUpdateDescriptorSetWithTemplate")) return as_proc(vkUpdateDescriptorSetWithTemplate);
    if (std::strcmp(name, "vkDestroyDevice") == 0) return as_proc(vkDestroyDevice);
    if (std::strcmp(name, "vkQueuePresentKHR") == 0 && state->present) return as_proc(vkQueuePresentKHR);
    return state->get_proc(device, name);
}

EVRM_EXPORT PFN_vkVoidFunction VKAPI_CALL evrmGetPhysicalDeviceProcAddr(VkInstance instance, const char* name) {
    const auto state = instance_state(instance);
    if (!state || !name) return nullptr;
    return state->get_physical_proc ? state->get_physical_proc(instance, name) : state->get_proc(instance, name);
}

EVRM_EXPORT VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* info) {
    if (!info || info->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || info->loaderLayerInterfaceVersion < 1)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (info->loaderLayerInterfaceVersion > 2) info->loaderLayerInterfaceVersion = 2;
    info->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    info->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    info->pfnGetPhysicalDeviceProcAddr = evrmGetPhysicalDeviceProcAddr;
    return VK_SUCCESS;
}
