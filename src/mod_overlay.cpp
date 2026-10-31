#define VK_NO_PROTOTYPES
#include "mod_overlay.hpp"
#include "avatar_runtime.hpp"
#include "mod_menu_gpu.hpp"
#include "mod_settings_service.hpp"
#include "mod_present_lease.hpp"
#include "runtime_log.hpp"
#include <array>
#include <map>
#include <mutex>
#include <cstring>
#include <stdexcept>
#include <atomic>
namespace evrm {
namespace {
template<class T>T info(VkStructureType type){T value{};value.sType=type;return value;}
void check(VkResult result){if(result!=VK_SUCCESS)throw std::runtime_error("menu presentation unavailable");}
struct ImageLease {ModPresentLease ownership;VkFence own_fence{},present_fence{};bool borrowed=false,prefix_retry=false;};
struct Swapchain {
    SwapchainState source;HWND window=nullptr;std::array<ImageLease,16> images{};bool disabled=false,attached=false;std::uint64_t admission=0;
    VkQueue queue{};std::uint32_t family=UINT32_MAX;std::unique_ptr<ModMenuGpu> gpu;
};
struct PresentChain {
    bool valid=true;const VkSwapchainPresentFenceInfoKHR* fence=nullptr;
};
PresentChain inspect_present(const VkPresentInfoKHR& present)noexcept {
    PresentChain result;auto* next=static_cast<const VkBaseInStructure*>(present.pNext);unsigned count=0;
    while(next&&count++<64){
        if(next->sType==VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR){if(result.fence){result.valid=false;break;}
            result.fence=reinterpret_cast<const VkSwapchainPresentFenceInfoKHR*>(next);
            if(result.fence->swapchainCount!=1||!result.fence->pFences){result.valid=false;break;}}
        if(next->sType==VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR||next->sType==VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR)result.valid=false;
        next=next->pNext;
    }
    if(next)result.valid=false;
    return result;
}
}
bool mod_menu_device_supported(const VkDeviceCreateInfo& create)noexcept {
    bool extension=false;
    for(unsigned n=0;n<create.enabledExtensionCount;++n)if(create.ppEnabledExtensionNames&&create.ppEnabledExtensionNames[n]&&
        (!std::strcmp(create.ppEnabledExtensionNames[n],"VK_KHR_swapchain_maintenance1")||!std::strcmp(create.ppEnabledExtensionNames[n],"VK_EXT_swapchain_maintenance1")))extension=true;
    if(!extension)return false;
    const auto* next=static_cast<const VkBaseInStructure*>(create.pNext);
    for(unsigned n=0;next&&n<128;++n,next=next->pNext)if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR)
        return reinterpret_cast<const VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(next)->swapchainMaintenance1==VK_TRUE;
    return false;
}
const VkPresentInfoKHR* ModOverlayPresentation::view()const noexcept {
    if(inserted){present.waitSemaphoreCount=1;present.pWaitSemaphores=&wait;
        if(private_fence){fence_info.pFences=&present_fence;present.pNext=&fence_info;}}
    return &present;
}
struct ModOverlay::Impl {
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr gdpa{};PFN_vkSetDeviceLoaderData loader{};
    bool enabled=false,expanded=false,uncertain=false;VkImageLayout layout{};std::mutex mutex;ModMenuPlacement placement;
    std::atomic<bool> fence_watch{false};
    std::array<VkQueueFlags,64> families{};std::map<VkSwapchainKHR,std::unique_ptr<Swapchain>> swaps;
    template<class F>F fn(const char* name){return reinterpret_cast<F>(gdpa(device,name));}
    void refresh(ImageLease& image){
        if(!image.ownership.waiting_present()||!image.present_fence)return;
        const auto result=fn<PFN_vkGetFenceStatus>("vkGetFenceStatus")(device,image.present_fence);
        if(result==VK_SUCCESS){image.ownership.fence_complete();image.present_fence=VK_NULL_HANDLE;image.borrowed=false;}
        else if(result==VK_ERROR_DEVICE_LOST)image.ownership.device_lost();
        else if(result!=VK_NOT_READY){image.ownership.uncertain();uncertain=true;}
    }
    void close(Swapchain& state)noexcept {if(state.admission)mod_input_request_close(state.window,state.admission);}
    void drain_unpresented(Swapchain& state,unsigned index){
        auto& image=state.images[index];if(!image.ownership.retry_present()||!state.gpu)return;
        check(fn<PFN_vkResetFences>("vkResetFences")(device,1,&image.own_fence));const auto semaphore=state.gpu->completion(index);
        const VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&semaphore;submit.pWaitDstStageMask=&stage;
        const auto queued=fn<PFN_vkQueueSubmit>("vkQueueSubmit")(state.queue,1,&submit,image.own_fence);
        if(queued==VK_ERROR_DEVICE_LOST){image.ownership.device_lost();return;}
        check(queued);const auto result=fn<PFN_vkWaitForFences>("vkWaitForFences")(device,1,&image.own_fence,VK_TRUE,UINT64_MAX);
        if(result==VK_ERROR_DEVICE_LOST)image.ownership.device_lost();else{check(result);image.ownership.drained();}
    }
    bool retire(Swapchain& state)noexcept {
        if(state.admission)mod_input_retire(state.window,state.admission);
        bool safe=true;
        try{
            for(unsigned n=0;n<state.source.images.size();++n){auto& image=state.images[n];image.ownership.retire();
                if(image.ownership.waiting_present()&&image.present_fence){
                    const auto result=fn<PFN_vkWaitForFences>("vkWaitForFences")(device,1,&image.present_fence,VK_TRUE,UINT64_MAX);
                    if(result==VK_SUCCESS)image.ownership.fence_complete();else if(result==VK_ERROR_DEVICE_LOST)image.ownership.device_lost();else image.ownership.uncertain();
                }
                if(image.ownership.retry_present()&&state.gpu){
                    // Present OOM did not enqueue its wait. Drain the UI's own
                    // signal before releasing it; no application wait repeats.
                    drain_unpresented(state,n);
                }
                safe&=image.ownership.safe_to_release();
            }
            if(!safe){uncertain=true;return false;}
            if(state.gpu){state.gpu->presentation_complete();state.gpu.reset();}
            for(auto& image:state.images)if(image.own_fence){fn<PFN_vkDestroyFence>("vkDestroyFence")(device,image.own_fence,nullptr);image.own_fence=VK_NULL_HANDLE;}
            return true;
        }catch(...){uncertain=true;return false;}
    }
};
ModOverlay::ModOverlay(VkInstance instance,VkPhysicalDevice physical,VkDevice device,PFN_vkGetInstanceProcAddr gipa,PFN_vkGetDeviceProcAddr gdpa,PFN_vkSetDeviceLoaderData loader,bool enabled,VkImageLayout layout):impl(std::make_unique<Impl>()){
    impl->instance=instance;impl->physical=physical;impl->device=device;impl->gipa=gipa;impl->gdpa=gdpa;impl->loader=loader;impl->enabled=enabled;impl->layout=layout;
    const auto get=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance,"vkGetPhysicalDeviceQueueFamilyProperties"));uint32_t count=0;if(!get)return;
    get(physical,&count,nullptr);if(count>impl->families.size())return;std::array<VkQueueFamilyProperties,64> properties{};get(physical,&count,properties.data());
    for(unsigned n=0;n<count;++n)impl->families[n]=properties[n].queueFlags;
}
ModOverlay::~ModOverlay(){
    if(!impl)return;
    std::lock_guard<std::mutex> guard(impl->mutex);bool safe=true;
    for(auto& item:impl->swaps)safe&=impl->retire(*item.second);
    if(!safe||impl->uncertain)impl.release();
}
void ModOverlay::swapchain_created(const SwapchainState& source,HWND window,bool eligible)noexcept {
    try{std::lock_guard<std::mutex> guard(impl->mutex);if(!impl->enabled||impl->uncertain||!window||impl->swaps.size()>=8||source.images.empty()||source.images.size()>16)return;
        const auto format=source.format;const bool color=format==VK_FORMAT_R8G8B8A8_UNORM||format==VK_FORMAT_R8G8B8A8_SRGB||format==VK_FORMAT_B8G8R8A8_UNORM||format==VK_FORMAT_B8G8R8A8_SRGB;
        if(!eligible||!color||!(source.usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)||!source.extent.width||!source.extent.height){
            const auto token=mod_input_admission(window);if(token)mod_input_retire(window,token);return;}
        auto state=std::make_unique<Swapchain>();state->source=source;state->window=window;impl->swaps.emplace(source.swapchain,std::move(state));
    }catch(...){log_event("mod-menu.swapchain-unavailable");}
}
void ModOverlay::swapchain_destroyed(VkSwapchainKHR swapchain)noexcept {
    try{std::lock_guard<std::mutex> guard(impl->mutex);const auto found=impl->swaps.find(swapchain);if(found==impl->swaps.end())return;
        if(impl->retire(*found->second))impl->swaps.erase(found);
    }catch(...){}
}
void ModOverlay::acquired(VkSwapchainKHR swapchain,std::uint32_t index,VkResult result)noexcept {
    try{std::lock_guard<std::mutex> guard(impl->mutex);const auto found=impl->swaps.find(swapchain);
        if(found!=impl->swaps.end()&&index<found->second->source.images.size())found->second->images[index].ownership.acquired(result);
    }catch(...){}
}
bool ModOverlay::retries_present(const VkPresentInfoKHR& present)noexcept {
    try{std::lock_guard<std::mutex> guard(impl->mutex);if(!present.pSwapchains||!present.pImageIndices)return false;
        for(unsigned n=0;n<present.swapchainCount&&n<64;++n){const auto found=impl->swaps.find(present.pSwapchains[n]);
            if(found!=impl->swaps.end()&&present.pImageIndices[n]<found->second->source.images.size()){
                const auto& image=found->second->images[present.pImageIndices[n]];if(image.prefix_retry||image.ownership.retry_present())return true;}}
    }catch(...){}return false;
}
void ModOverlay::before_present(VkQueue queue,std::uint32_t family,const VkPresentInfoKHR& original,bool consumed,bool accessible,const ModMenuStatus& status,ModOverlayPresentation& output)noexcept {
    output={};output.present=original;output.consumed=consumed;if(consumed){output.present.waitSemaphoreCount=0;output.present.pWaitSemaphores=nullptr;}
    const bool any_retry=retries_present(original);
    try{
        std::lock_guard<std::mutex> guard(impl->mutex);
        try{
        if(original.swapchainCount!=1||!original.pSwapchains||!original.pImageIndices){for(auto& item:impl->swaps)impl->close(*item.second);if(any_retry)output.stop=VK_ERROR_UNKNOWN;return;}
        const auto found=impl->swaps.find(original.pSwapchains[0]);if(found==impl->swaps.end())return;auto& state=*found->second;
        const auto index=original.pImageIndices[0];if(index>=state.source.images.size())return;auto& image=state.images[index];
        const bool retry=image.ownership.retry_present();
        // A prior UI submission already consumed the application waits. Keep
        // an error result until every fallible retry preparation has succeeded.
        if(retry){output.stop=VK_ERROR_OUT_OF_HOST_MEMORY;output.consumed=true;output.present.waitSemaphoreCount=0;output.present.pWaitSemaphores=nullptr;}
        if(image.prefix_retry&&!retry){output.present.waitSemaphoreCount=0;output.present.pWaitSemaphores=nullptr;output.consumed=true;return;}
        const auto chain=inspect_present(original);
        if(!retry&&(!chain.valid||state.disabled||impl->uncertain||family>=impl->families.size()||!(impl->families[family]&VK_QUEUE_GRAPHICS_BIT)||!accessible)){impl->close(state);return;}
        if(!retry&&!state.attached){state.attached=mod_input_attach(state.window,&state.admission);if(!state.attached){state.disabled=true;return;}}
        if(!retry&&mod_input_admission(state.window)!=state.admission)return;
        if(!retry&&!mod_input_open(state.window))return;
        if(!retry&&state.queue&&state.queue!=queue){impl->close(state);return;}
        impl->refresh(image);
        if(!retry&&!image.ownership.can_draw(true)){impl->close(state);return;}
        VkFence fence=chain.fence?chain.fence->pFences[0]:VK_NULL_HANDLE;
        const bool owned=!fence;
        if(owned&&chain.fence&&original.pNext!=chain.fence){
            if(retry){impl->drain_unpresented(state,index);output.present.waitSemaphoreCount=0;output.present.pWaitSemaphores=nullptr;output.consumed=true;output.stop=VK_SUCCESS;}
            impl->close(state);return;}
        ModInputFrame input;bool have_input=false;
        if(!state.gpu){
            input=mod_input_drain(state.window);have_input=true;if(!input.open)return;
            state.queue=queue;state.family=family;
            for(unsigned n=0;n<state.source.images.size();++n){auto create=info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);check(impl->fn<PFN_vkCreateFence>("vkCreateFence")(impl->device,&create,nullptr,&state.images[n].own_fence));}
            state.gpu=std::make_unique<ModMenuGpu>(impl->instance,impl->physical,impl->device,impl->gipa,impl->gdpa,impl->loader,queue,family,state.source.format,state.source.extent,state.source.images,impl->layout,input.dpi);
        }
        if(!retry&&!state.gpu->available()){impl->close(state);return;}
        if(owned){fence=image.own_fence;const auto result=impl->fn<PFN_vkResetFences>("vkResetFences")(impl->device,1,&fence);if(retry&&result!=VK_SUCCESS)output.stop=result;check(result);}
        if(!retry){
            if(!have_input)input=mod_input_drain(state.window);
            if(!input.open)return;
            auto settings=mod_settings_snapshot();bool close=false;
            const auto result=state.gpu->draw(index,output.present.waitSemaphoreCount,output.present.pWaitSemaphores,settings,status,impl->expanded,close,input,&impl->placement);
            if(result.changed)request_mod_settings(settings);
            if(close)impl->close(state);
            if(result.avatar_action.kind!=AvatarActionKind::None)apply_avatar_action(result.avatar_action,reinterpret_cast<std::uintptr_t>(state.window));
            if(result.result==VK_NOT_READY&&!result.submitted){impl->close(state);return;}
            const auto action=image.ownership.submit_result(result.result);
            if(action==ModPresentAction::Stop){output.stop=result.result;impl->uncertain=true;impl->close(state);return;}
            if(!result.submitted){impl->close(state);return;}
        }
        // No allocation or other fallible operation follows successful submit.
        image.present_fence=fence;image.borrowed=!owned;output.wait=state.gpu->completion(index);output.present_fence=fence;output.inserted=true;output.consumed=true;
        if(!owned)impl->fence_watch.store(true,std::memory_order_release);
        if(owned){output.private_fence=true;output.fence_info=info<VkSwapchainPresentFenceInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR);output.fence_info.swapchainCount=1;
            output.fence_info.pNext=chain.fence?chain.fence->pNext:original.pNext;}
        output.stop=VK_SUCCESS;
        }catch(...){for(auto& item:impl->swaps){item.second->disabled=true;impl->close(*item.second);}log_event("mod-menu.presentation-unavailable");}
    }catch(...){output.stop=VK_ERROR_OUT_OF_HOST_MEMORY;}
}
void ModOverlay::presented(const ModOverlayPresentation& presentation,VkResult result)noexcept {
    try{std::lock_guard<std::mutex> guard(impl->mutex);const auto& info=presentation.present;
        if(info.swapchainCount!=1||!info.pSwapchains||!info.pImageIndices)return;
        const auto found=impl->swaps.find(info.pSwapchains[0]);if(found==impl->swaps.end())return;
        auto& state=*found->second;const auto index=info.pImageIndices[0];if(index>=state.source.images.size())return;
        auto& image=state.images[index];
        if(presentation.inserted){image.ownership.present_result(result);image.prefix_retry=false;}
        else{image.ownership.original_presented();image.prefix_retry=presentation.consumed&&(result==VK_ERROR_OUT_OF_HOST_MEMORY||result==VK_ERROR_OUT_OF_DEVICE_MEMORY);}
        if(result<0)impl->close(state);
    }catch(...){}
}
void ModOverlay::fence_changing(VkFence fence,bool destroying)noexcept {
    if(!impl->fence_watch.load(std::memory_order_acquire))return;
    try{std::lock_guard<std::mutex> guard(impl->mutex);for(auto& item:impl->swaps)for(auto& image:item.second->images)
        if(image.borrowed&&image.present_fence==fence&&image.ownership.waiting_present()){
            impl->refresh(image);if(destroying&&image.ownership.waiting_present()){image.ownership.uncertain();image.present_fence=VK_NULL_HANDLE;impl->uncertain=true;impl->close(*item.second);}}
    }catch(...){}
}
}
