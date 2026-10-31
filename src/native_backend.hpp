#pragma once
#include "native_state.hpp"
#include "native_publication.hpp"
#include "avatar_service.hpp"
#include "native_avatar_assets.hpp"
#include <vulkan/vk_layer.h>
#include <filesystem>
#include <mutex>
#ifdef EVRM_NATIVE_BACKEND_TESTING
#include "native_owner.hpp"
#include "native_menu_owner.hpp"
#endif

namespace evrm {
class NativeClipPipelines;
class NativeBufferMemory;
class NativeImageMemory;
#ifdef EVRM_NATIVE_BACKEND_TESTING
// CPU fixture only. Normal production builds have no owner override API.
struct NativeBackendTestOwner {
    virtual ~NativeBackendTestOwner()=default;
    virtual bool capture(NativeOwnerCandidate&,std::string&,NativeOwnerObservationStats* stats=nullptr) const noexcept=0;
    virtual bool revalidate(NativeOwnerCandidate&,std::string&,NativeOwnerObservationStats* stats=nullptr) const noexcept=0;
    virtual bool capture_menu(NativeMenuOwnerCandidate& out,std::string& why)const noexcept{out={};why.clear();return false;}
    virtual bool revalidate_menu(NativeMenuOwnerCandidate& out,std::string& why)const noexcept{out={};why.clear();return false;}
};
#endif
// Explicit native-pass experiments. Replacement additionally requires current
// CPU ownership, matched per-primary coverage and the shared GPU commit.
class NativeBackend {
public:
    NativeBackend(VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,PFN_vkSetDeviceLoaderData,const std::filesystem::path&,bool independent_blend_enabled=false,bool replacement=false,bool partially_bound_enabled=false,bool mesh_enabled=false);
    ~NativeBackend();
    NativeState& state() noexcept;
    NativeClipPipelines* clipping() noexcept;
    NativeBufferMemory* buffer_memory() noexcept;
    NativeImageMemory* image_memory() noexcept;
    bool mouth_gpu_unavailable()const noexcept;
    // Host source construction and native destroy/free use the same mutex.
    // try_only is for nonblocking coordination; the ordinary hook waits.
    std::unique_lock<std::mutex> native_source_guard(bool try_only=false);
#ifdef EVRM_NATIVE_BACKEND_TESTING
    void test_owner(std::shared_ptr<const NativeBackendTestOwner>) noexcept;
#endif
    void publish(VkQueue,std::uint32_t family,std::shared_ptr<const NativeAvatarPublication>,std::uint64_t source_buffer_epoch=0)noexcept;
    NativeAvatarUploadState prepare_model(VkQueue,std::uint32_t,const AvatarCandidatePtr&)noexcept;
    bool activate_model(const AvatarCandidate&,bool(*)(void*,std::uint64_t)noexcept,void*)noexcept;
    void pipeline_layout_created(VkPipelineLayout,const VkPipelineLayoutCreateInfo&)noexcept;
    void pipeline_layout_destroyed(VkPipelineLayout)noexcept;
    void command_begin(VkCommandBuffer,const VkCommandBufferBeginInfo&)noexcept;
    void allocated(const VkCommandBufferAllocateInfo&,const VkCommandBuffer*)noexcept;
    void command_reset(VkCommandBuffer)noexcept;
    void pool_reset(VkCommandPool)noexcept;
    void pool_destroyed(VkCommandPool)noexcept;
    void command_freed(std::uint32_t,const VkCommandBuffer*)noexcept;
    void incoming_rendering(VkCommandBuffer,const VkRenderingInfo&)noexcept;
    void rendering_unavailable(VkCommandBuffer)noexcept;
    void before_rendering(VkCommandBuffer,const VkRenderingInfo&,const char* release_trigger=nullptr)noexcept;
    void before_end_rendering(VkCommandBuffer)noexcept;
    void end_rendering(VkCommandBuffer)noexcept;
    bool pose_capture_boundary(VkCommandBuffer)noexcept;
    void render_current(VkCommandBuffer)noexcept;
    void draw(VkCommandBuffer,VkBuffer,VkDeviceSize,VkBuffer,VkDeviceSize,std::uint32_t,std::uint32_t)noexcept;
    void dispatch(VkCommandBuffer,bool capture_owner=true,std::uint32_t x=0,std::uint32_t y=0,std::uint32_t z=0)noexcept;
    void barriers(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,
                  std::uint32_t,const VkMemoryBarrier*,std::uint32_t,const VkBufferMemoryBarrier*,
                  std::uint32_t,const VkImageMemoryBarrier*)noexcept;
    void barriers2(VkCommandBuffer,const VkDependencyInfo*)noexcept;
    void unsupported_draw(VkCommandBuffer)noexcept;
    void uncovered_draw(VkCommandBuffer,const char* command_name=nullptr)noexcept;
    void before_native_draw(VkCommandBuffer)noexcept;
    void after_native_draw(VkCommandBuffer)noexcept;
    void before_attachment_clear(VkCommandBuffer)noexcept;
    void scope_unsupported(VkCommandBuffer,const char* reason=nullptr)noexcept;
    // VK_SUCCESS forwards the original call. After an uncertain native submit
    // failure, a retry of its private CB retains that original error until the
    // original queue is proven complete; no potentially pending slot is edited.
    VkResult before_submit(VkQueue,std::uint32_t,const VkSubmitInfo*)noexcept;
    VkResult before_submit2(VkQueue,std::uint32_t,const VkSubmitInfo2*)noexcept;
    void submission_result(VkQueue,VkResult)noexcept;
    void command_end(VkCommandBuffer,VkResult)noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
