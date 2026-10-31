#define VK_NO_PROTOTYPES
#include "mod_menu_gpu.hpp"
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/backends/imgui_impl_vulkan.h"
#include <mutex>
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace evrm {
namespace {
template<class T>T info(VkStructureType type){T value{};value.sType=type;return value;}
void checked(VkResult result){if(result!=VK_SUCCESS)throw std::runtime_error("menu Vulkan operation failed");}
void backend_result(VkResult result){if(result<0)checked(result);}
struct Slot {VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};bool submitted=false;};
struct Image {VkImageView view{};VkFramebuffer framebuffer{};VkSemaphore completion{};};
struct MenuGpuContext;
std::mutex gui_mutex;MenuGpuContext* loaded=nullptr;
struct MenuGpuContext {
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr gdpa{};PFN_vkSetDeviceLoaderData loader{};
    VkQueue queue{};VkExtent2D extent{};VkRenderPass pass{};ImGuiContext* gui=nullptr;
    std::vector<Slot> slots;std::vector<Image> images;ModMenuPlacement placement;
    std::uint64_t rendered=0;float dpi=1;bool presentation_safe=true,uncertain=false,broken=false,font_upload_pending=false;
    template<class F>F fn(const char* name)const{return reinterpret_cast<F>(gdpa(device,name));}
    static VkResult VKAPI_CALL allocate(VkDevice device,const VkCommandBufferAllocateInfo* allocation,VkCommandBuffer* commands){
        if(!loaded||loaded->device!=device)return VK_ERROR_INITIALIZATION_FAILED;
        const auto result=loaded->fn<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device,allocation,commands);
        if(result==VK_SUCCESS&&loaded->loader)for(unsigned n=0;n<allocation->commandBufferCount;++n){const auto set=loaded->loader(device,commands[n]);if(set!=VK_SUCCESS)return set;}
        return result;
    }
    void select(){
        ImGui::SetCurrentContext(gui);if(loaded==this)return;
        loaded=this;
        if(!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_1,[](const char* name,void* raw)->PFN_vkVoidFunction{
            auto& state=*static_cast<MenuGpuContext*>(raw);
            if(!std::strcmp(name,"vkAllocateCommandBuffers"))return reinterpret_cast<PFN_vkVoidFunction>(&allocate);
            const auto device=state.gdpa(state.device,name);return device?device:state.gipa(state.instance,name);
        },this)){loaded=nullptr;throw std::runtime_error("menu Vulkan functions unavailable");}
    }
    bool ready(){
        if(uncertain||broken||slots.empty())return false;
        const auto& slot=slots[(rendered+1)%slots.size()];
        if(!slot.submitted)return true;
        const auto result=fn<PFN_vkGetFenceStatus>("vkGetFenceStatus")(device,slot.fence);if(result==VK_SUCCESS)return true;
        if(result!=VK_NOT_READY)uncertain=true;
        return false;
    }
    bool finish()noexcept {
        for(const auto& slot:slots)if(slot.submitted){const auto result=fn<PFN_vkWaitForFences>("vkWaitForFences")(device,1,&slot.fence,VK_TRUE,UINT64_MAX);
            if(result!=VK_SUCCESS&&result!=VK_ERROR_DEVICE_LOST)return false;}
        return true;
    }
    void cleanup()noexcept {
        if(uncertain||!presentation_safe||!finish())return;
        try{
            if(gui){select();if(ImGui::GetIO().BackendRendererUserData)ImGui_ImplVulkan_Shutdown();ImGui::DestroyContext(gui);gui=nullptr;if(loaded==this)loaded=nullptr;}
            for(auto& image:images){if(image.framebuffer)fn<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(device,image.framebuffer,nullptr);
                if(image.view)fn<PFN_vkDestroyImageView>("vkDestroyImageView")(device,image.view,nullptr);
                if(image.completion)fn<PFN_vkDestroySemaphore>("vkDestroySemaphore")(device,image.completion,nullptr);}
            for(auto& slot:slots){if(slot.fence)fn<PFN_vkDestroyFence>("vkDestroyFence")(device,slot.fence,nullptr);if(slot.pool)fn<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device,slot.pool,nullptr);}
            if(pass)fn<PFN_vkDestroyRenderPass>("vkDestroyRenderPass")(device,pass,nullptr);
        }catch(...){uncertain=true;}
    }
};
}
struct ModMenuGpu::Impl:MenuGpuContext{};
ModMenuGpu::ModMenuGpu(VkInstance instance,VkPhysicalDevice physical,VkDevice device,PFN_vkGetInstanceProcAddr gipa,PFN_vkGetDeviceProcAddr gdpa,
    PFN_vkSetDeviceLoaderData loader,VkQueue queue,std::uint32_t family,VkFormat format,VkExtent2D extent,const std::vector<VkImage>& targets,VkImageLayout layout,float dpi):impl(std::make_unique<Impl>()){
    std::lock_guard<std::mutex> guard(gui_mutex);auto& state=*impl;
    state.instance=instance;state.physical=physical;state.device=device;state.gipa=gipa;state.gdpa=gdpa;state.loader=loader;state.queue=queue;state.extent=extent;state.dpi=std::clamp(dpi,1.f,3.f);
    if(targets.empty()||targets.size()>16||!extent.width||!extent.height)throw std::runtime_error("unsupported menu targets");
    try{
        VkAttachmentDescription attachment{};attachment.format=format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachment.initialLayout=attachment.finalLayout=layout;
        const VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
        const std::array<VkSubpassDependency,2> dependencies={{{VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0},
            {0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0,0}}};
        auto pass=info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&subpass;pass.dependencyCount=dependencies.size();pass.pDependencies=dependencies.data();
        checked(state.fn<PFN_vkCreateRenderPass>("vkCreateRenderPass")(device,&pass,nullptr,&state.pass));
        state.images.resize(targets.size());state.slots.resize(std::max<std::size_t>(2,targets.size()));
        for(unsigned n=0;n<targets.size();++n){auto& image=state.images[n];auto view=info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);view.image=targets[n];view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            checked(state.fn<PFN_vkCreateImageView>("vkCreateImageView")(device,&view,nullptr,&image.view));auto frame=info<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);frame.renderPass=state.pass;frame.attachmentCount=1;frame.pAttachments=&image.view;frame.width=extent.width;frame.height=extent.height;frame.layers=1;
            checked(state.fn<PFN_vkCreateFramebuffer>("vkCreateFramebuffer")(device,&frame,nullptr,&image.framebuffer));auto semaphore=info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);checked(state.fn<PFN_vkCreateSemaphore>("vkCreateSemaphore")(device,&semaphore,nullptr,&image.completion));}
        for(auto& slot:state.slots){auto pool=info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);pool.queueFamilyIndex=family;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            checked(state.fn<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device,&pool,nullptr,&slot.pool));auto allocation=info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);allocation.commandPool=slot.pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
            checked(state.fn<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device,&allocation,&slot.command));if(loader)checked(loader(device,slot.command));
            auto fence=info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;checked(state.fn<PFN_vkCreateFence>("vkCreateFence")(device,&fence,nullptr,&slot.fence));}
        state.gui=ImGui::CreateContext();configure_mod_menu(state.dpi,format==VK_FORMAT_R8G8B8A8_SRGB||format==VK_FORMAT_B8G8R8A8_SRGB);state.select();
        ImGui_ImplVulkan_InitInfo init{};init.ApiVersion=VK_API_VERSION_1_1;init.Instance=instance;init.PhysicalDevice=physical;init.Device=device;init.QueueFamily=family;init.Queue=queue;init.RenderPass=state.pass;
        init.MinImageCount=2;init.ImageCount=state.slots.size();init.MSAASamples=VK_SAMPLE_COUNT_1_BIT;init.DescriptorPoolSize=2;init.CheckVkResultFn=&backend_result;
        if(!ImGui_ImplVulkan_Init(&init))throw std::runtime_error("menu renderer unavailable");
        // The unmodified backend submits its own font upload, independently of
        // our frame slots. Only its successful completion return proves it safe.
        state.font_upload_pending=true;
        if(!ImGui_ImplVulkan_CreateFontsTexture())throw std::runtime_error("menu font upload unavailable");
        state.font_upload_pending=false;
    }catch(...){
        if(state.font_upload_pending){state.uncertain=true;impl.release();}
        else state.cleanup();
        throw;
    }
}
ModMenuGpu::~ModMenuGpu(){
    std::lock_guard<std::mutex> guard(gui_mutex);if(!impl)return;
    if(impl->uncertain||!impl->presentation_safe||!impl->finish()){impl.release();return;}
    impl->cleanup();
}
bool ModMenuGpu::available()noexcept {try{std::lock_guard<std::mutex> guard(gui_mutex);return impl&&impl->ready();}catch(...){return false;}}
VkSemaphore ModMenuGpu::completion(std::uint32_t image)const noexcept{return impl&&image<impl->images.size()?impl->images[image].completion:VK_NULL_HANDLE;}
void ModMenuGpu::presentation_complete()noexcept {if(impl)impl->presentation_safe=true;}
ModMenuGpuResult ModMenuGpu::draw(std::uint32_t image,std::uint32_t waits,const VkSemaphore* semaphores,ModSettings& settings,const ModMenuStatus& status,bool& expanded,bool& close,const ModInputFrame& input,ModMenuPlacement* placement)noexcept {
    ModMenuGpuResult result;
    try{
        std::lock_guard<std::mutex> guard(gui_mutex);auto& state=*impl;
        if(image>=state.images.size()||waits>64||(waits&&!semaphores)||!state.ready()){result.result=VK_NOT_READY;return result;}
        state.select();auto& io=ImGui::GetIO();io.DisplaySize={state.extent.width/state.dpi,state.extent.height/state.dpi};io.DisplayFramebufferScale={state.dpi,state.dpi};io.DeltaTime=1.f/60;
        auto& menu_placement=placement?*placement:state.placement;
        feed_mod_menu_input(input.input,input.x/state.dpi,input.y/state.dpi,menu_placement,state.dpi);ImGui_ImplVulkan_NewFrame();ImGui::NewFrame();
        result.changed=draw_mod_menu(settings,status,expanded,close,menu_placement,nullptr,&result.avatar_action);ImGui::Render();auto* data=ImGui::GetDrawData();
        if(!data||data->TotalVtxCount>32768||data->TotalIdxCount>131072||data->CmdListsCount>16)throw std::runtime_error("menu draw limit");
        auto& slot=state.slots[(state.rendered+1)%state.slots.size()];slot.submitted=false;
        checked(state.fn<PFN_vkResetCommandPool>("vkResetCommandPool")(state.device,slot.pool,0));auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checked(state.fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(slot.command,&begin));auto pass=info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);pass.renderPass=state.pass;pass.framebuffer=state.images[image].framebuffer;pass.renderArea.extent=state.extent;
        state.fn<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass")(slot.command,&pass,VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(data,slot.command);++state.rendered;
        state.fn<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass")(slot.command);checked(state.fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(slot.command));
        checked(state.fn<PFN_vkResetFences>("vkResetFences")(state.device,1,&slot.fence));
        std::array<VkPipelineStageFlags,64> stages{};stages.fill(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.waitSemaphoreCount=waits;submit.pWaitSemaphores=semaphores;submit.pWaitDstStageMask=stages.data();submit.commandBufferCount=1;submit.pCommandBuffers=&slot.command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&state.images[image].completion;
        result.result=state.fn<PFN_vkQueueSubmit>("vkQueueSubmit")(state.queue,1,&submit,slot.fence);
        if(result.result==VK_SUCCESS){slot.submitted=true;state.presentation_safe=false;result.submitted=true;result.completion=state.images[image].completion;}
        else if(result.result!=VK_ERROR_OUT_OF_HOST_MEMORY&&result.result!=VK_ERROR_OUT_OF_DEVICE_MEMORY)state.uncertain=true;
    }catch(...){if(impl)impl->broken=true;result.result=VK_ERROR_OUT_OF_HOST_MEMORY;close=true;}
    return result;
}
}
