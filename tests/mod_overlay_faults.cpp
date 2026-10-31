#define main adapter_fixture_main
#include "mod_overlay.cpp"
#undef main
#include "../src/mod_menu_gpu.hpp"
#include <atomic>
#include <condition_variable>
#include <future>
#include <thread>
namespace {
bool reset_failure=false,idle_failure=false,create_failure=false;
VkDevice fault_device{};
unsigned destroyed_images=0,destroyed_pools=0,freed_memory=0;
std::mutex recovery_mutex;std::condition_variable recovery_condition;bool block_recovery=false,recovery_entered=false,release_recovery=false;
VkResult VKAPI_CALL fault_reset(VkDevice device,uint32_t count,const VkFence* fences){if(reset_failure){reset_failure=false;return VK_ERROR_OUT_OF_HOST_MEMORY;}return reinterpret_cast<PFN_vkResetFences>(native_get(device,"vkResetFences"))(device,count,fences);}
VkResult VKAPI_CALL fault_idle(VkQueue queue){
    const auto result=reinterpret_cast<PFN_vkQueueWaitIdle>(native_get(fault_device,"vkQueueWaitIdle"))(queue);
    if(result==VK_SUCCESS&&idle_failure){idle_failure=false;return VK_ERROR_OUT_OF_HOST_MEMORY;}return result;
}
VkResult VKAPI_CALL fault_create(VkDevice device,const VkFenceCreateInfo* info,const VkAllocationCallbacks* callbacks,VkFence* fence){if(create_failure){create_failure=false;return VK_ERROR_OUT_OF_HOST_MEMORY;}return reinterpret_cast<PFN_vkCreateFence>(native_get(device,"vkCreateFence"))(device,info,callbacks,fence);}
void VKAPI_CALL observe_image_destroy(VkDevice device,VkImage image,const VkAllocationCallbacks* callbacks){++destroyed_images;reinterpret_cast<PFN_vkDestroyImage>(native_get(device,"vkDestroyImage"))(device,image,callbacks);}
void VKAPI_CALL observe_pool_destroy(VkDevice device,VkCommandPool pool,const VkAllocationCallbacks* callbacks){++destroyed_pools;reinterpret_cast<PFN_vkDestroyCommandPool>(native_get(device,"vkDestroyCommandPool"))(device,pool,callbacks);}
void VKAPI_CALL observe_free(VkDevice device,VkDeviceMemory memory,const VkAllocationCallbacks* callbacks){++freed_memory;reinterpret_cast<PFN_vkFreeMemory>(native_get(device,"vkFreeMemory"))(device,memory,callbacks);}
PFN_vkVoidFunction VKAPI_CALL fault_get(VkDevice device,const char* name){
    if(!std::strcmp(name,"vkResetFences"))return reinterpret_cast<PFN_vkVoidFunction>(&fault_reset);
    if(!std::strcmp(name,"vkQueueWaitIdle"))return reinterpret_cast<PFN_vkVoidFunction>(&fault_idle);
    if(!std::strcmp(name,"vkCreateFence"))return reinterpret_cast<PFN_vkVoidFunction>(&fault_create);
    if(!std::strcmp(name,"vkDestroyImage"))return reinterpret_cast<PFN_vkVoidFunction>(&observe_image_destroy);
    if(!std::strcmp(name,"vkDestroyCommandPool"))return reinterpret_cast<PFN_vkVoidFunction>(&observe_pool_destroy);
    if(!std::strcmp(name,"vkFreeMemory"))return reinterpret_cast<PFN_vkVoidFunction>(&observe_free);
    return native_get(device?device:fault_device,name);
}
void recovery_close(){std::unique_lock<std::mutex> guard(recovery_mutex);if(block_recovery){recovery_entered=true;recovery_condition.notify_all();recovery_condition.wait(guard,[]{return release_recovery;});}}
}
int main(int argc,char** argv){try{
    require(argc==3,"fixture folder and fault scenario");Context c(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT,true);native_get=c.get;real_submit=c.fn<PFN_vkQueueSubmit>("vkQueueSubmit");fault_device=c.device;Target one(c),two(c);
    const std::string mode=argv[2];
    if(mode=="font"){
        idle_failure=true;bool failed=false;
        try{evrm::ModMenuGpu gpu(c.instance,c.physical,c.device,c.gipa,&fault_get,nullptr,c.queue,c.family,VK_FORMAT_R8G8B8A8_UNORM,{800,700},{one.image,two.image},VK_IMAGE_LAYOUT_GENERAL,1);}catch(...){failed=true;}
        require(failed&&!idle_failure,"font upload submitted and its completion wait failed");
        require(!destroyed_images&&!destroyed_pools&&!freed_memory,"uncertain font upload retains image/memory/command pool");
        // The injected wrapper actually waited before reporting failure, keeping
        // this regression safe even against the buggy cleanup implementation.
        evrm::ModMenuGpu replacement(c.instance,c.physical,c.device,c.gipa,c.get,nullptr,c.queue,c.family,VK_FORMAT_R8G8B8A8_UNORM,{800,700},{one.image,two.image},VK_IMAGE_LAYOUT_GENERAL,1);
        require(replacement.available(),"new context selects valid loader state after quarantine");replacement.presentation_complete();
        std::cout<<"PASS failed font upload completion quarantines resources and preserves future context selection\n";return 0;
    }
    evrm::ModOverlay overlay(c.instance,c.physical,c.device,c.gipa,&fault_get,nullptr,true,VK_IMAGE_LAYOUT_GENERAL);
    evrm::SwapchainState state;state.swapchain=reinterpret_cast<VkSwapchainKHR>(std::uintptr_t(201));state.format=VK_FORMAT_R8G8B8A8_UNORM;state.extent={800,700};state.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;state.images={one.image,two.image};
    const auto window=reinterpret_cast<HWND>(std::uintptr_t(99));overlay.swapchain_created(state,window,true);overlay.acquired(state.swapchain,0,VK_SUCCESS);
    std::uint32_t index=0;auto original=info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);original.swapchainCount=1;original.pSwapchains=&state.swapchain;original.pImageIndices=&index;evrm::ModMenuStatus status;
    if(mode=="recovery"){
        create_failure=true;block_recovery=true;on_close=&recovery_close;evrm::ModOverlayPresentation decision;
        std::thread failing([&]{overlay.before_present(c.queue,c.family,original,false,true,status,decision);});
        {std::unique_lock<std::mutex> guard(recovery_mutex);if(!recovery_condition.wait_for(guard,std::chrono::seconds(3),[]{return recovery_entered;})){release_recovery=true;guard.unlock();recovery_condition.notify_all();failing.join();throw std::runtime_error("failure reached recovery hook");}}
        auto next=state;next.swapchain=reinterpret_cast<VkSwapchainKHR>(std::uintptr_t(202));std::promise<void> started;auto ready=started.get_future();
        auto creating=std::async(std::launch::async,[&]{started.set_value();overlay.swapchain_created(next,window,true);});ready.wait();
        const bool locked=creating.wait_for(std::chrono::milliseconds(150))==std::future_status::timeout;
        {std::lock_guard<std::mutex> guard(recovery_mutex);release_recovery=true;}recovery_condition.notify_all();failing.join();creating.get();block_recovery=false;on_close=nullptr;
        require(locked,"overlay failure recovery holds state mutex against concurrent swapchain creation");
        overlay.swapchain_destroyed(next.swapchain);overlay.swapchain_destroyed(state.swapchain);
        std::cout<<"PASS initialization failure recovery serializes concurrent swapchain creation\n";return 0;
    }
    require(mode=="reset","known scenario");auto semaphore=info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);VkSemaphore application{};checked(c.fn<PFN_vkCreateSemaphore>("vkCreateSemaphore")(c.device,&semaphore,nullptr,&application),"original wait");
    auto signal=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);signal.signalSemaphoreCount=1;signal.pSignalSemaphores=&application;checked(real_submit(c.queue,1,&signal,VK_NULL_HANDLE),"signal original");original.waitSemaphoreCount=1;original.pWaitSemaphores=&application;
    evrm::ModOverlayPresentation first;overlay.before_present(c.queue,c.family,original,false,true,status,first);require(first.inserted,"first UI consumes original and owns completion");overlay.presented(first,VK_ERROR_OUT_OF_HOST_MEMORY);
    reset_failure=true;evrm::ModOverlayPresentation retry;overlay.before_present(c.queue,c.family,original,false,true,status,retry);
    const bool stopped=retry.stop!=VK_SUCCESS;const bool retained=overlay.retries_present(original);
    require(!reset_failure,"private present-fence reset fault injected");
    // Successful later retry consumes the outstanding UI semaphore exactly once.
    evrm::ModOverlayPresentation last;overlay.before_present(c.queue,c.family,original,false,true,status,last);require(last.inserted,"completion retained for subsequent retry");
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;auto consume=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);consume.waitSemaphoreCount=1;consume.pWaitSemaphores=last.view()->pWaitSemaphores;consume.pWaitDstStageMask=&stage;checked(real_submit(c.queue,1,&consume,last.present_fence),"fake present consumes completion");overlay.presented(last,VK_SUCCESS);
    checked(c.fn<PFN_vkWaitForFences>("vkWaitForFences")(c.device,1,&last.present_fence,VK_TRUE,UINT64_MAX),"retry presentation completed");overlay.swapchain_destroyed(state.swapchain);c.fn<PFN_vkDestroySemaphore>("vkDestroySemaphore")(c.device,application,nullptr);
    require(stopped&&retained,"failed retry preparation stops driver present and retains consumed-wait ownership");std::cout<<"PASS private-fence reset failure stops Present without repeating consumed application waits\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
