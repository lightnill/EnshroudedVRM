#define VK_NO_PROTOTYPES
#include "../src/mod_overlay.hpp"
#include "../src/mod_menu_gpu.hpp"
#include "../src/mod_settings_service.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace {
bool opened=true, gpu_ready=true, gpu_failed=false; unsigned closes=0, drains=0; std::uint64_t admission=1; VkResult fence_status=VK_SUCCESS, draw_status=VK_SUCCESS;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void VKAPI_CALL families(VkPhysicalDevice,uint32_t* count,VkQueueFamilyProperties* props){if(!props){*count=1;return;} props[0].queueFlags=VK_QUEUE_GRAPHICS_BIT;}
VkResult VKAPI_CALL create_fence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* fence){*fence=reinterpret_cast<VkFence>(100);return VK_SUCCESS;}
VkResult VKAPI_CALL reset_fences(VkDevice,uint32_t,const VkFence*){return VK_SUCCESS;}
VkResult VKAPI_CALL status_fence(VkDevice,VkFence){return fence_status;}
VkResult VKAPI_CALL wait_fences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){return VK_SUCCESS;}
void VKAPI_CALL destroy_fence(VkDevice,VkFence,const VkAllocationCallbacks*){}
PFN_vkVoidFunction VKAPI_CALL gipa(VkInstance,const char* name){return !std::strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties")?reinterpret_cast<PFN_vkVoidFunction>(families):nullptr;}
PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice,const char* name){
#define FN(n, f) if(!std::strcmp(name,n))return reinterpret_cast<PFN_vkVoidFunction>(f)
FN("vkCreateFence",create_fence);FN("vkResetFences",reset_fences);FN("vkGetFenceStatus",status_fence);FN("vkWaitForFences",wait_fences);FN("vkDestroyFence",destroy_fence);
#undef FN
return nullptr;}
}
namespace evrm {
bool mod_input_attach(HWND,std::uint64_t* p)noexcept{*p=admission;return true;} bool mod_input_open(HWND)noexcept{return opened;} std::uint64_t mod_input_admission(HWND)noexcept{return admission;}
void mod_input_request_close(HWND,std::uint64_t token)noexcept{if(token==admission){opened=false;++closes;}} void mod_input_retire(HWND,std::uint64_t token)noexcept{if(token==admission)opened=false;}
ModInputFrame mod_input_drain(HWND)noexcept{++drains;ModInputFrame f;f.open=opened;return f;} ModSettings mod_settings_snapshot()noexcept{return {};}
bool request_mod_settings(const ModSettings&)noexcept{return true;} void apply_avatar_action(const AvatarMenuAction&,std::uintptr_t)noexcept{}
struct ModMenuGpu::Impl{};
ModMenuGpu::ModMenuGpu(VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,PFN_vkSetDeviceLoaderData,VkQueue,std::uint32_t,VkFormat,VkExtent2D,const std::vector<VkImage>&,VkImageLayout,float):impl(new Impl){}
ModMenuGpu::~ModMenuGpu()=default;
ModMenuGpuReadiness ModMenuGpu::readiness()noexcept{return gpu_failed?ModMenuGpuReadiness::Failed:(gpu_ready?ModMenuGpuReadiness::Ready:ModMenuGpuReadiness::Busy);}
ModMenuGpuResult ModMenuGpu::draw(std::uint32_t,std::uint32_t,const VkSemaphore*,ModSettings&,const ModMenuStatus&,bool&,bool&,const ModInputFrame&,ModMenuPlacement*)noexcept{ModMenuGpuResult r;r.result=draw_status;r.submitted=draw_status==VK_SUCCESS;return r;}
VkSemaphore ModMenuGpu::completion(std::uint32_t)const noexcept{return reinterpret_cast<VkSemaphore>(101);} void ModMenuGpu::presentation_complete()noexcept{}
}
int main(int argc,char** argv){try{
require(argc==2,"case required"); evrm::ModOverlay overlay(nullptr,nullptr,nullptr,gipa,gdpa,nullptr,true); evrm::SwapchainState state;
state.swapchain=reinterpret_cast<VkSwapchainKHR>(1);state.format=VK_FORMAT_R8G8B8A8_UNORM;state.extent={800,600};state.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;state.images={reinterpret_cast<VkImage>(2)};
overlay.swapchain_created(state,reinterpret_cast<HWND>(3),true); uint32_t index=0; VkSemaphore original=reinterpret_cast<VkSemaphore>(4);
VkPresentInfoKHR p{};p.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;p.swapchainCount=1;p.pSwapchains=&state.swapchain;p.pImageIndices=&index;p.waitSemaphoreCount=1;p.pWaitSemaphores=&original;
evrm::ModMenuStatus status;evrm::ModOverlayPresentation first;auto q=reinterpret_cast<VkQueue>(5);
overlay.acquired(state.swapchain,0,VK_SUCCESS);overlay.before_present(q,0,p,false,true,status,first);require(first.inserted,"initial frame submitted");
if(!std::strcmp(argv[1],"stale-retire")||!std::strcmp(argv[1],"stale-close")){
overlay.presented(first,VK_SUCCESS);admission=2;
if(!std::strcmp(argv[1],"stale-retire"))overlay.swapchain_destroyed(state.swapchain);
else{p.swapchainCount=0;evrm::ModOverlayPresentation stale;overlay.before_present(q,0,p,false,true,status,stale);}
require(opened&&closes==0,"stale admission must not close the active menu");
std::cout<<"PASS "<<argv[1]<<" leaves current menu open\n";return 0;}
if(!std::strcmp(argv[1],"retry")){overlay.presented(first,VK_ERROR_OUT_OF_HOST_MEMORY);gpu_ready=false;evrm::ModOverlayPresentation retry;overlay.before_present(q,0,p,false,false,status,retry);require(retry.inserted&&retry.consumed&&retry.view()->waitSemaphoreCount==1&&retry.view()->pWaitSemaphores[0]==first.wait,"OOM retry retains UI completion wait");overlay.presented(retry,VK_SUCCESS);std::cout<<"PASS OOM retry uses completion wait\n";return 0;}
overlay.presented(first,VK_SUCCESS);overlay.acquired(state.swapchain,0,VK_SUCCESS);
if(!std::strcmp(argv[1],"present"))fence_status=VK_NOT_READY;
else if(!std::strcmp(argv[1],"gpu"))gpu_ready=false;
else if(!std::strcmp(argv[1],"draw"))draw_status=VK_NOT_READY;
else if(!std::strcmp(argv[1],"fatal"))gpu_failed=true;
else if(!std::strcmp(argv[1],"present-fatal"))fence_status=VK_ERROR_DEVICE_LOST;
else if(!std::strcmp(argv[1],"consumed"))gpu_ready=false;
else throw std::runtime_error("unknown case");
unsigned before=drains;evrm::ModOverlayPresentation busy;overlay.before_present(q,0,p,!std::strcmp(argv[1],"consumed"),true,status,busy);
if(!std::strcmp(argv[1],"fatal")||!std::strcmp(argv[1],"present-fatal")){require(!opened&&closes==1,"fatal state must close menu");std::cout<<"PASS "<<argv[1]<<" closes menu\n";return 0;}
if(!std::strcmp(argv[1],"consumed")){require(opened&&!busy.inserted&&busy.consumed&&busy.view()->waitSemaphoreCount==0,"already-consumed wait remains empty");std::cout<<"PASS consumed wait remains empty\n";return 0;}
require(opened&&closes==0,"transient busy must keep menu open");require(!busy.inserted&&!busy.consumed&&busy.view()->waitSemaphoreCount==1&&busy.view()->pWaitSemaphores[0]==original,"busy forwards original wait exactly once");
if(std::strcmp(argv[1],"draw"))require(drains==before,"busy must retain queued input");
overlay.presented(busy,VK_SUCCESS);fence_status=VK_SUCCESS;gpu_ready=true;draw_status=VK_SUCCESS;overlay.acquired(state.swapchain,0,VK_SUCCESS);evrm::ModOverlayPresentation resumed;overlay.before_present(q,0,p,false,true,status,resumed);require(resumed.inserted&&opened,"menu resumes drawing");overlay.presented(resumed,VK_SUCCESS);overlay.swapchain_destroyed(state.swapchain);
std::cout<<"PASS "<<argv[1]<<" busy keeps menu open, forwards waits and resumes\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
