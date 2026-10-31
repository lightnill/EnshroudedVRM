#define VK_NO_PROTOTYPES
#include "native_skinning_fixture.hpp"
#include "../src/mod_overlay.hpp"
#include "../src/mod_input_win32.hpp"
#include "../src/mod_settings_service.hpp"
#include <memory>
namespace {
bool menu_open=true;unsigned closes=0,attaches=0,ui_submits=0;bool submit_oom=false;std::uint64_t admission=0;
PFN_vkQueueSubmit real_submit=nullptr;
void (*on_close)()=nullptr;
VkResult VKAPI_CALL wrapped_submit(VkQueue queue,uint32_t count,const VkSubmitInfo* submits,VkFence fence){
    if(count==1&&submits&&submits->signalSemaphoreCount==1&&submits->commandBufferCount){++ui_submits;if(submit_oom){submit_oom=false;return VK_ERROR_OUT_OF_HOST_MEMORY;}}
    return real_submit(queue,count,submits,fence);
}
PFN_vkVoidFunction VKAPI_CALL wrapped_get(VkDevice device,const char* name){if(!std::strcmp(name,"vkQueueSubmit"))return reinterpret_cast<PFN_vkVoidFunction>(&wrapped_submit);return native_get(device,name);}
struct Target {Context& c;VkImage image{};VkDeviceMemory memory{};
    Target(Context& context):c(context){auto ci=info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_UNORM;ci.extent={800,700,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        checked(c.fn<PFN_vkCreateImage>("vkCreateImage")(c.device,&ci,nullptr,&image),"adapter target");VkMemoryRequirements req{};c.fn<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(c.device,image,&req);unsigned type=0;while(type<c.memory.memoryTypeCount&&!(req.memoryTypeBits&(1u<<type)))++type;
        auto ai=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);ai.allocationSize=req.size;ai.memoryTypeIndex=type;checked(c.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(c.device,&ai,nullptr,&memory),"adapter image memory");checked(c.fn<PFN_vkBindImageMemory>("vkBindImageMemory")(c.device,image,memory,0),"adapter image bind");
        c.begin();auto barrier=info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);barrier.image=image;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};c.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(c.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);VkClearColorValue black{};c.fn<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(c.command,image,VK_IMAGE_LAYOUT_GENERAL,&black,1,&barrier.subresourceRange);c.submit();}
    ~Target(){c.fn<PFN_vkDestroyImage>("vkDestroyImage")(c.device,image,nullptr);c.fn<PFN_vkFreeMemory>("vkFreeMemory")(c.device,memory,nullptr);}
};
}
namespace evrm {
bool mod_input_attach(HWND,std::uint64_t* token)noexcept{++attaches;++admission;if(token)*token=admission;return true;}bool mod_input_open(HWND)noexcept{return menu_open;}
std::uint64_t mod_input_admission(HWND)noexcept{return admission;}
void mod_input_request_close(HWND,std::uint64_t token)noexcept{if(on_close)on_close();if(!token||token==admission){menu_open=false;++closes;}}
void mod_input_retire(HWND,std::uint64_t token)noexcept{if(!token||token==admission){menu_open=false;++closes;}}
ModInputFrame mod_input_drain(HWND)noexcept{ModInputFrame result;result.open=menu_open;return result;}
ModSettings mod_settings_snapshot()noexcept{return {};}
bool request_mod_settings(const ModSettings&)noexcept{return true;}
void apply_avatar_action(const AvatarMenuAction&,std::uintptr_t)noexcept{}
}
int main(){try{
    (void)&fixture;(void)&observed_get;Context c(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT,true);native_get=c.get;real_submit=c.fn<PFN_vkQueueSubmit>("vkQueueSubmit");Target first(c),second(c);
    evrm::ModOverlay overlay(c.instance,c.physical,c.device,c.gipa,&wrapped_get,nullptr,true,VK_IMAGE_LAYOUT_GENERAL);
    const auto swap=reinterpret_cast<VkSwapchainKHR>(std::uintptr_t(100));evrm::SwapchainState state;state.swapchain=swap;state.format=VK_FORMAT_R8G8B8A8_UNORM;state.extent={800,700};state.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;state.images={first.image,second.image};
    overlay.swapchain_created(state,reinterpret_cast<HWND>(std::uintptr_t(99)),true);evrm::ModMenuStatus status;
    auto fi=info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);VkFence app{};checked(c.fn<PFN_vkCreateFence>("vkCreateFence")(c.device,&fi,nullptr,&app),"app present fence");
    auto si=info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);VkSemaphore original{};checked(c.fn<PFN_vkCreateSemaphore>("vkCreateSemaphore")(c.device,&si,nullptr,&original),"app render done");
    const auto signal=[&]{auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&original;checked(real_submit(c.queue,1,&submit,VK_NULL_HANDLE),"app render signal");};
    const auto present=[&](const evrm::ModOverlayPresentation& decision,VkFence fence){const auto* view=decision.view();VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.waitSemaphoreCount=view->waitSemaphoreCount;submit.pWaitSemaphores=view->pWaitSemaphores;submit.pWaitDstStageMask=&stage;checked(real_submit(c.queue,1,&submit,fence),"fake presentation wait");};
    uint32_t index=0;auto app_fence=info<VkSwapchainPresentFenceInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR);app_fence.swapchainCount=1;app_fence.pFences=&app;
    auto original_info=info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);original_info.pNext=&app_fence;original_info.swapchainCount=1;original_info.pSwapchains=&state.swapchain;original_info.pImageIndices=&index;original_info.waitSemaphoreCount=1;original_info.pWaitSemaphores=&original;
    overlay.acquired(swap,index,VK_SUCCESS);signal();evrm::ModOverlayPresentation one;
    overlay.before_present(c.queue,c.family,original_info,false,true,status,one);require(one.inserted&&one.view()->pNext==original_info.pNext&&one.view()->pWaitSemaphores[0]!=original,"existing app fence/pNext preserved, waits replaced once");
    const auto submitted=ui_submits;overlay.presented(one,VK_ERROR_OUT_OF_HOST_MEMORY);evrm::ModOverlayPresentation retry;
    overlay.before_present(c.queue,c.family,original_info,false,false,status,retry);require(retry.inserted&&ui_submits==submitted&&retry.view()->pWaitSemaphores[0]==one.view()->pWaitSemaphores[0],"OOM retry keeps owned wait even when new rendering admission fails");
    present(retry,app);overlay.presented(retry,VK_SUCCESS);checked(c.fn<PFN_vkWaitForFences>("vkWaitForFences")(c.device,1,&app,VK_TRUE,UINT64_MAX),"app presentation done");
    overlay.fence_changing(app,false);checked(c.fn<PFN_vkResetFences>("vkResetFences")(c.device,1,&app),"app reset observed");
    menu_open=true;index=1;overlay.acquired(swap,index,VK_SUBOPTIMAL_KHR);original_info.pNext=nullptr;original_info.waitSemaphoreCount=0;original_info.pWaitSemaphores=nullptr;evrm::ModOverlayPresentation two;
    overlay.before_present(c.queue,c.family,original_info,true,true,status,two);require(two.inserted&&two.view()->waitSemaphoreCount==1&&two.present_fence!=VK_NULL_HANDLE,"consumed original waits use private completion/fence");
    present(two,two.present_fence);overlay.presented(two,VK_ERROR_OUT_OF_DATE_KHR);overlay.swapchain_destroyed(swap);require(closes>0,"retirement closes input and waits present completion");
    // A fresh swapchain starts with fresh image leases. Submit failure leaves the app wait for its own present.
    menu_open=true;state.swapchain=reinterpret_cast<VkSwapchainKHR>(std::uintptr_t(101));overlay.swapchain_created(state,reinterpret_cast<HWND>(std::uintptr_t(99)),true);index=0;overlay.acquired(state.swapchain,index,VK_SUCCESS);
    original_info.pSwapchains=&state.swapchain;original_info.pNext=&app_fence;original_info.waitSemaphoreCount=1;original_info.pWaitSemaphores=&original;signal();submit_oom=true;evrm::ModOverlayPresentation failed;
    overlay.before_present(c.queue,c.family,original_info,false,true,status,failed);require(!failed.inserted&&failed.view()->pWaitSemaphores[0]==original,"submit failure preserves original semaphore");present(failed,app);overlay.presented(failed,VK_SUCCESS);
    checked(c.fn<PFN_vkWaitForFences>("vkWaitForFences")(c.device,1,&app,VK_TRUE,UINT64_MAX),"failure baseline completes");overlay.swapchain_destroyed(state.swapchain);
    // Even when the menu is closed, an earlier avatar/probe consumer must not
    // consume the same app wait again if the driver's Present returned OOM.
    checked(c.fn<PFN_vkResetFences>("vkResetFences")(c.device,1,&app),"prefix reset");menu_open=false;
    state.swapchain=reinterpret_cast<VkSwapchainKHR>(std::uintptr_t(102));overlay.swapchain_created(state,reinterpret_cast<HWND>(std::uintptr_t(99)),true);overlay.acquired(state.swapchain,0,VK_SUCCESS);
    signal();VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;auto baseline=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);baseline.waitSemaphoreCount=1;baseline.pWaitSemaphores=&original;baseline.pWaitDstStageMask=&stage;
    checked(real_submit(c.queue,1,&baseline,app),"baseline consumes app wait");checked(c.fn<PFN_vkWaitForFences>("vkWaitForFences")(c.device,1,&app,VK_TRUE,UINT64_MAX),"baseline completion");checked(c.fn<PFN_vkResetFences>("vkResetFences")(c.device,1,&app),"baseline fence reset");
    evrm::ModOverlayPresentation prefix;overlay.before_present(c.queue,c.family,original_info,true,true,status,prefix);overlay.presented(prefix,VK_ERROR_OUT_OF_HOST_MEMORY);
    require(overlay.retries_present(original_info),"caller must skip earlier consumers for a prefix retry");evrm::ModOverlayPresentation prefix_retry;
    overlay.before_present(c.queue,c.family,original_info,false,false,status,prefix_retry);require(!prefix_retry.inserted&&prefix_retry.view()->waitSemaphoreCount==0,"closed-menu prefix retry keeps consumed waits empty");
    present(prefix_retry,app);overlay.presented(prefix_retry,VK_SUCCESS);checked(c.fn<PFN_vkWaitForFences>("vkWaitForFences")(c.device,1,&app,VK_TRUE,UINT64_MAX),"prefix retry completion");overlay.swapchain_destroyed(state.swapchain);
    c.fn<PFN_vkDestroySemaphore>("vkDestroySemaphore")(c.device,original,nullptr);c.fn<PFN_vkDestroyFence>("vkDestroyFence")(c.device,app,nullptr);
    std::cout<<"PASS real-GPU/fake-present adapter wait ownership, app/private fences, OOM retry/fallback and retirement\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
