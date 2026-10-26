#define VK_NO_PROTOTYPES
#include "graphics_state.hpp"
#include "runtime_log.hpp"
#include "sha256.hpp"
#include <map>
#include <set>
#include <unordered_map>
#include <sstream>

namespace evrm {
namespace {
struct Attachment { VkFormat format{}; VkImageLayout initial{}, final_layout{}; };
struct Pass { std::vector<Attachment> attachments; int depth = -1; uint32_t colors = 0; VkImageLayout depth_layout{}; };
struct Framebuffer { VkRenderPass pass{}; std::vector<VkImageView> attachments; };
struct Shader { bool character=false,player_anchor=false; };
struct Pipeline { bool character = false,player_anchor=false; VkCompareOp compare{VK_COMPARE_OP_GREATER_OR_EQUAL};VkPipelineLayout layout{}; };
enum class EventKind { begin_pass, end_pass, begin_dynamic, end_dynamic, character, barrier };
struct Event {
    EventKind kind{};
    VkRenderPass pass{}; VkFramebuffer framebuffer{}; VkImageView view{};
    VkImage image{}; VkImageLayout layout{};
    VkCompareOp compare{VK_COMPARE_OP_GREATER_OR_EQUAL};
    uint32_t destination_family = VK_QUEUE_FAMILY_IGNORED;
};
}
struct GraphicsState::Impl {
    VkDevice device{}; PFN_vkGetDeviceProcAddr gdpa{};
    mutable std::mutex mutex;
    std::unordered_map<VkImage, VkImageCreateInfo> images;
    std::unordered_map<VkImage, std::vector<uint32_t>> image_families;
    std::unordered_map<VkImage, uint64_t> image_generations;
    std::unordered_map<VkImage, uint32_t> owners;
    std::unordered_map<VkQueue, uint32_t> queues;
    uint64_t generation = 0;
    std::unordered_map<VkImageView, VkImageViewCreateInfo> views;
    std::unordered_map<VkSwapchainKHR, SwapchainState> swapchains;
    std::unordered_map<VkRenderPass, Pass> passes;
    std::unordered_map<VkFramebuffer, Framebuffer> framebuffers;
    std::unordered_map<VkShaderModule, Shader> shaders;
    std::unordered_map<VkPipeline, Pipeline> pipelines;
    std::unordered_map<VkCommandBuffer, std::vector<Event>> commands;
    std::unordered_map<VkImage, VkImageLayout> layouts;
    SceneDepth depth;
    std::set<std::pair<VkImage,VkImageLayout>> logged_depths;
    unsigned pipeline_logs = 0;
    void process(const std::vector<Event>& events, uint32_t family) {
        VkRenderPass pass{}; VkFramebuffer framebuffer{}; VkImageView dynamic_depth{};
        auto attachment_image = [&](VkImageView view) {
            const auto found=views.find(view); return found==views.end() ? VkImage{} : found->second.image;
        };
        for (const auto& event : events) {
            switch (event.kind) {
            case EventKind::begin_pass: {
                pass=event.pass; framebuffer=event.framebuffer; dynamic_depth={};
                const auto p=passes.find(pass); const auto f=framebuffers.find(framebuffer);
                if(f!=framebuffers.end())for(auto view:f->second.attachments)owners[attachment_image(view)]=family;
                if (p!=passes.end() && f!=framebuffers.end() && p->second.depth>=0 && static_cast<std::size_t>(p->second.depth)<f->second.attachments.size())
                    layouts[attachment_image(f->second.attachments[p->second.depth])]=p->second.depth_layout;
                break;
            }
            case EventKind::end_pass: {
                const auto p=passes.find(pass); const auto f=framebuffers.find(framebuffer);
                if (p!=passes.end() && f!=framebuffers.end()) {
                    const auto count=std::min(p->second.attachments.size(), f->second.attachments.size());
                    for (std::size_t i=0;i<count;++i) layouts[attachment_image(f->second.attachments[i])]=p->second.attachments[i].final_layout;
                }
                pass={}; framebuffer={}; break;
            }
            case EventKind::begin_dynamic: dynamic_depth=event.view; layouts[attachment_image(event.view)]=event.layout;owners[attachment_image(event.view)]=family;break;
            case EventKind::end_dynamic: dynamic_depth={}; break;
            case EventKind::barrier:
                layouts[event.image]=event.layout;
                if(event.destination_family!=VK_QUEUE_FAMILY_IGNORED)owners[event.image]=event.destination_family;
                else if(!owners.count(event.image))owners[event.image]=family;
                break;
            case EventKind::character: {
                VkImageView view=dynamic_depth;
                const auto p=passes.find(pass); const auto f=framebuffers.find(framebuffer);
                if (!view && p!=passes.end() && f!=framebuffers.end() && p->second.colors>=2 && p->second.depth>=0 &&
                    static_cast<std::size_t>(p->second.depth)<f->second.attachments.size()) view=f->second.attachments[p->second.depth];
                const auto image=attachment_image(view); const auto image_ci=images.find(image);
                if (image && image_ci!=images.end()) {
                    depth.image=image; depth.format=image_ci->second.format; depth.extent=image_ci->second.extent;
                    depth.usage=image_ci->second.usage; depth.samples=image_ci->second.samples; depth.compare=event.compare;
                    depth.generation=image_generations.at(image);depth.sharing_mode=image_ci->second.sharingMode;depth.queue_families=image_families[image];
                    owners[image]=family;
                }
                break;
            }
            }
        }
        if (depth) {
            const auto found=layouts.find(depth.image);
            depth.layout=found==layouts.end()?VK_IMAGE_LAYOUT_UNDEFINED:found->second;
            if (logged_depths.size()<32 && logged_depths.insert({depth.image,depth.layout}).second) {
                log_event("graphics.scene-depth format="+std::to_string(depth.format)+" extent="+std::to_string(depth.extent.width)+"x"+
                    std::to_string(depth.extent.height)+" usage="+std::to_string(depth.usage)+" layout="+std::to_string(depth.layout)+
                    " samples="+std::to_string(depth.samples)+" compare="+std::to_string(depth.compare));
            }
        }
    }
};
GraphicsState::GraphicsState(VkDevice device, PFN_vkGetDeviceProcAddr gdpa):impl(std::make_unique<Impl>()) { impl->device=device;impl->gdpa=gdpa; }
GraphicsState::~GraphicsState()=default;
void GraphicsState::image_created(VkImage image, const VkImageCreateInfo& info) {
    std::lock_guard<std::mutex> guard(impl->mutex); auto copy=info;copy.pNext=nullptr;copy.pQueueFamilyIndices=nullptr;impl->images[image]=copy;
    impl->image_generations[image]=++impl->generation;
    impl->image_families[image].clear();
    if(info.queueFamilyIndexCount&&info.pQueueFamilyIndices)impl->image_families[image].assign(info.pQueueFamilyIndices,info.pQueueFamilyIndices+info.queueFamilyIndexCount);
    impl->owners.erase(image);
}
void GraphicsState::image_destroyed(VkImage image) { std::lock_guard<std::mutex> guard(impl->mutex);impl->images.erase(image);impl->layouts.erase(image);impl->owners.erase(image);impl->image_generations.erase(image);impl->image_families.erase(image);if(impl->depth.image==image)impl->depth={}; }
void GraphicsState::view_created(VkImageView view,const VkImageViewCreateInfo& info) { std::lock_guard<std::mutex> guard(impl->mutex);auto copy=info;copy.pNext=nullptr;impl->views[view]=copy; }
void GraphicsState::view_destroyed(VkImageView view) { std::lock_guard<std::mutex> guard(impl->mutex);impl->views.erase(view); }
void GraphicsState::swapchain_created(VkSwapchainKHR swapchain,const VkSwapchainCreateInfoKHR& info) {
    SwapchainState state;state.swapchain=swapchain;state.format=info.imageFormat;state.extent=info.imageExtent;
    state.usage=info.imageUsage;state.sharing_mode=info.imageSharingMode;
    if(info.queueFamilyIndexCount&&info.pQueueFamilyIndices)state.queue_families.assign(info.pQueueFamilyIndices,info.pQueueFamilyIndices+info.queueFamilyIndexCount);
    const auto get=reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(impl->gdpa(impl->device,"vkGetSwapchainImagesKHR"));
    uint32_t count=0;if(!get||get(impl->device,swapchain,&count,nullptr)!=VK_SUCCESS)return;
    state.images.resize(count);if(get(impl->device,swapchain,&count,state.images.data())!=VK_SUCCESS)return;
    std::lock_guard<std::mutex> guard(impl->mutex);state.generation=++impl->generation;
    for(auto image:state.images){impl->owners.erase(image);impl->layouts.erase(image);}
    impl->swapchains[swapchain]=std::move(state);
    log_event("graphics.swapchain format="+std::to_string(info.imageFormat)+" extent="+std::to_string(info.imageExtent.width)+"x"+std::to_string(info.imageExtent.height)+" usage="+std::to_string(info.imageUsage));
}
void GraphicsState::swapchain_destroyed(VkSwapchainKHR swapchain) {
    std::lock_guard<std::mutex> guard(impl->mutex);const auto found=impl->swapchains.find(swapchain);
    if(found!=impl->swapchains.end())for(auto image:found->second.images){impl->owners.erase(image);impl->layouts.erase(image);}
    impl->swapchains.erase(swapchain);
}
void GraphicsState::renderpass_created(VkRenderPass handle,const VkRenderPassCreateInfo& info) {
    Pass pass;for(uint32_t i=0;i<info.attachmentCount;++i)pass.attachments.push_back({info.pAttachments[i].format,info.pAttachments[i].initialLayout,info.pAttachments[i].finalLayout});
    if(info.subpassCount==1) { const auto& s=info.pSubpasses[0];pass.colors=s.colorAttachmentCount;
        if(s.pDepthStencilAttachment&&s.pDepthStencilAttachment->attachment!=VK_ATTACHMENT_UNUSED) {pass.depth=static_cast<int>(s.pDepthStencilAttachment->attachment);pass.depth_layout=s.pDepthStencilAttachment->layout;} }
    std::lock_guard<std::mutex> guard(impl->mutex);impl->passes[handle]=std::move(pass);
}
void GraphicsState::renderpass2_created(VkRenderPass handle,const VkRenderPassCreateInfo2& info) {
    Pass pass;for(uint32_t i=0;i<info.attachmentCount;++i)pass.attachments.push_back({info.pAttachments[i].format,info.pAttachments[i].initialLayout,info.pAttachments[i].finalLayout});
    if(info.subpassCount==1) { const auto& s=info.pSubpasses[0];pass.colors=s.colorAttachmentCount;
        if(s.pDepthStencilAttachment&&s.pDepthStencilAttachment->attachment!=VK_ATTACHMENT_UNUSED) {pass.depth=static_cast<int>(s.pDepthStencilAttachment->attachment);pass.depth_layout=s.pDepthStencilAttachment->layout;} }
    std::lock_guard<std::mutex> guard(impl->mutex);impl->passes[handle]=std::move(pass);
}
void GraphicsState::renderpass_destroyed(VkRenderPass pass) {std::lock_guard<std::mutex> guard(impl->mutex);impl->passes.erase(pass);}
void GraphicsState::framebuffer_created(VkFramebuffer handle,const VkFramebufferCreateInfo& info) {
    Framebuffer framebuffer;framebuffer.pass=info.renderPass;
    if(!(info.flags&VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT)&&info.attachmentCount&&info.pAttachments)framebuffer.attachments.assign(info.pAttachments,info.pAttachments+info.attachmentCount);
    std::lock_guard<std::mutex> guard(impl->mutex);impl->framebuffers[handle]=std::move(framebuffer);
}
void GraphicsState::framebuffer_destroyed(VkFramebuffer framebuffer){std::lock_guard<std::mutex> guard(impl->mutex);impl->framebuffers.erase(framebuffer);}
void GraphicsState::shader_created(VkShaderModule module,const VkShaderModuleCreateInfo& info) {
    struct Decoration { uint32_t set=~0u,binding=~0u; };std::map<uint32_t,Decoration> ids;
    const bool readable=info.pCode&&info.codeSize>=20&&info.codeSize%4==0;
    for(std::size_t at=5;readable&&at<info.codeSize/4;) { const auto w=info.pCode[at],count=w>>16,op=w&65535u;
        if(!count||count>info.codeSize/4-at)break;
        if(op==71&&count>=4){auto& d=ids[info.pCode[at+1]];if(info.pCode[at+2]==33)d.binding=info.pCode[at+3];if(info.pCode[at+2]==34)d.set=info.pCode[at+3];}at+=count;
    }
    bool skin=false,instances=false,camera=false;
    for(const auto& item:ids){const auto d=item.second;skin=skin||(d.set==0&&d.binding==5);instances=instances||(d.set==2&&d.binding==4);camera=camera||(d.set==1&&d.binding==16);}
    Shader shader;shader.character=skin&&instances&&camera;
    // Same-build DeferredLighting stripped modules, verified offline to read
    // playerWorldPosition at set2/binding24, byte256 of the 2048-byte block.
    // Exact bytes also fix cs_main's compute execution model and 8x8x1 size.
    static constexpr detail::Sha256Digest normal={0x50,0x17,0x7f,0x15,0x31,0x5c,0xb9,0x5c,0x82,0x32,0x0a,0x0c,0x8f,0xce,0x5a,0x66,0xda,0xec,0x96,0x52,0x78,0x6f,0x12,0x60,0x1c,0xac,0x48,0xe1,0x35,0x5f,0xf5,0x37};
    static constexpr detail::Sha256Digest debug={0x05,0xdd,0x03,0x26,0x1e,0x58,0x9f,0x63,0x64,0x86,0xff,0xd0,0xa7,0x1c,0x9b,0xb3,0x51,0x0a,0x20,0xd0,0x32,0x61,0x20,0x15,0x29,0x72,0x79,0x8a,0x6b,0xd0,0x1c,0x20};
    if(module&&readable&&(info.codeSize==74292||info.codeSize==86252)) {
        const auto digest=detail::sha256(info.pCode,info.codeSize);
        shader.player_anchor=(info.codeSize==74292&&digest==normal)||(info.codeSize==86252&&digest==debug);
    }
    std::lock_guard<std::mutex> guard(impl->mutex);impl->shaders[module]=shader;
}
void GraphicsState::shader_destroyed(VkShaderModule shader){std::lock_guard<std::mutex> guard(impl->mutex);impl->shaders.erase(shader);}
void GraphicsState::pipelines_created(uint32_t count,const VkGraphicsPipelineCreateInfo* infos,const VkPipeline* handles) {
    std::lock_guard<std::mutex> guard(impl->mutex);
    for(uint32_t i=0;i<count;++i)if(handles[i]) {Pipeline pipeline;const auto& info=infos[i];pipeline.layout=info.layout;
        for(uint32_t s=0;s<info.stageCount;++s)if(info.pStages[s].stage==VK_SHADER_STAGE_VERTEX_BIT){const auto shader=impl->shaders.find(info.pStages[s].module);if(shader!=impl->shaders.end()&&shader->second.character)pipeline.character=true;}
        if(info.pDepthStencilState)pipeline.compare=info.pDepthStencilState->depthCompareOp;
        impl->pipelines[handles[i]]=pipeline;
        if(pipeline.character&&impl->pipeline_logs++<32)log_event("graphics.character-pipeline subpass="+std::to_string(info.subpass)+" compare="+std::to_string(pipeline.compare));
    }
}
void GraphicsState::pipeline_destroyed(VkPipeline pipeline){std::lock_guard<std::mutex> guard(impl->mutex);impl->pipelines.erase(pipeline);}
void GraphicsState::compute_pipelines_created(uint32_t count,const VkComputePipelineCreateInfo* infos,const VkPipeline* handles) {
    if(!infos||!handles)return;
    std::lock_guard<std::mutex> guard(impl->mutex);
    for(uint32_t i=0;i<count;++i)if(handles[i]) {
        const auto& info=infos[i];Pipeline pipeline;pipeline.layout=info.layout;
        const auto shader=impl->shaders.find(info.stage.module);
        pipeline.player_anchor=info.stage.stage==VK_SHADER_STAGE_COMPUTE_BIT&&info.stage.pName&&std::strcmp(info.stage.pName,"cs_main")==0&&
            (!info.stage.pSpecializationInfo||info.stage.pSpecializationInfo->mapEntryCount==0)&&shader!=impl->shaders.end()&&shader->second.player_anchor;
        impl->pipelines[handles[i]]=pipeline;
    }
}
bool GraphicsState::player_anchor_pipeline(VkPipeline pipeline) const {std::lock_guard<std::mutex> guard(impl->mutex);const auto found=impl->pipelines.find(pipeline);return found!=impl->pipelines.end()&&found->second.player_anchor;}
bool GraphicsState::model_pipeline(VkPipeline pipeline) const {std::lock_guard<std::mutex> guard(impl->mutex);const auto found=impl->pipelines.find(pipeline);return found!=impl->pipelines.end()&&found->second.character;}
VkPipelineLayout GraphicsState::pipeline_layout(VkPipeline pipeline) const {std::lock_guard<std::mutex> guard(impl->mutex);const auto found=impl->pipelines.find(pipeline);return found==impl->pipelines.end()?VK_NULL_HANDLE:found->second.layout;}
void GraphicsState::command_begin(VkCommandBuffer command){std::lock_guard<std::mutex> guard(impl->mutex);impl->commands[command].clear();}
void GraphicsState::command_freed(uint32_t count,const VkCommandBuffer* commands){std::lock_guard<std::mutex> guard(impl->mutex);for(uint32_t i=0;i<count;++i)impl->commands.erase(commands[i]);}
void GraphicsState::begin_pass(VkCommandBuffer command,const VkRenderPassBeginInfo& info){std::lock_guard<std::mutex> guard(impl->mutex);Event e;e.kind=EventKind::begin_pass;e.pass=info.renderPass;e.framebuffer=info.framebuffer;impl->commands[command].push_back(e);}
void GraphicsState::end_pass(VkCommandBuffer command){std::lock_guard<std::mutex> guard(impl->mutex);Event e;e.kind=EventKind::end_pass;impl->commands[command].push_back(e);}
void GraphicsState::begin_rendering(VkCommandBuffer command,const VkRenderingInfo& info){std::lock_guard<std::mutex> guard(impl->mutex);Event e;e.kind=EventKind::begin_dynamic;if(info.pDepthAttachment){e.view=info.pDepthAttachment->imageView;e.layout=info.pDepthAttachment->imageLayout;}impl->commands[command].push_back(e);}
void GraphicsState::end_rendering(VkCommandBuffer command){std::lock_guard<std::mutex> guard(impl->mutex);Event e;e.kind=EventKind::end_dynamic;impl->commands[command].push_back(e);}
void GraphicsState::pipeline_bound(VkCommandBuffer command,VkPipelineBindPoint point,VkPipeline handle){if(point!=VK_PIPELINE_BIND_POINT_GRAPHICS)return;std::lock_guard<std::mutex> guard(impl->mutex);const auto p=impl->pipelines.find(handle);if(p!=impl->pipelines.end()&&p->second.character){Event e;e.kind=EventKind::character;e.compare=p->second.compare;impl->commands[command].push_back(e);}}
void GraphicsState::barriers(VkCommandBuffer command,uint32_t count,const VkImageMemoryBarrier* barriers){std::lock_guard<std::mutex> guard(impl->mutex);for(uint32_t i=0;i<count;++i){Event e;e.kind=EventKind::barrier;e.image=barriers[i].image;e.layout=barriers[i].newLayout;e.destination_family=barriers[i].dstQueueFamilyIndex;impl->commands[command].push_back(e);}}
void GraphicsState::barriers2(VkCommandBuffer command,const VkDependencyInfo& info){std::lock_guard<std::mutex> guard(impl->mutex);for(uint32_t i=0;i<info.imageMemoryBarrierCount;++i){Event e;e.kind=EventKind::barrier;e.image=info.pImageMemoryBarriers[i].image;e.layout=info.pImageMemoryBarriers[i].newLayout;e.destination_family=info.pImageMemoryBarriers[i].dstQueueFamilyIndex;impl->commands[command].push_back(e);}}
void GraphicsState::execute(VkCommandBuffer command,uint32_t count,const VkCommandBuffer* children){std::lock_guard<std::mutex> guard(impl->mutex);auto& dest=impl->commands[command];for(uint32_t i=0;i<count;++i){const auto child=impl->commands.find(children[i]);if(child!=impl->commands.end())dest.insert(dest.end(),child->second.begin(),child->second.end());}}

void GraphicsState::queue_observed(VkQueue queue,uint32_t family) {
    std::lock_guard<std::mutex> guard(impl->mutex);impl->queues[queue]=family;
}
void GraphicsState::submitted(VkQueue queue,uint32_t count,const VkSubmitInfo* submits) {
    std::lock_guard<std::mutex> guard(impl->mutex);
    const auto family=impl->queues.count(queue)?impl->queues.at(queue):VK_QUEUE_FAMILY_IGNORED;
    for(uint32_t i=0;i<count;++i)for(uint32_t c=0;c<submits[i].commandBufferCount;++c) {
        const auto found=impl->commands.find(submits[i].pCommandBuffers[c]);
        if(found!=impl->commands.end())impl->process(found->second,family);
    }
}
void GraphicsState::submitted2(VkQueue queue,uint32_t count,const VkSubmitInfo2* submits) {
    std::lock_guard<std::mutex> guard(impl->mutex);
    const auto family=impl->queues.count(queue)?impl->queues.at(queue):VK_QUEUE_FAMILY_IGNORED;
    for(uint32_t i=0;i<count;++i)for(uint32_t c=0;c<submits[i].commandBufferInfoCount;++c) {
        const auto found=impl->commands.find(submits[i].pCommandBufferInfos[c].commandBuffer);
        if(found!=impl->commands.end())impl->process(found->second,family);
    }
}
SceneDepth GraphicsState::scene_depth() const {
    std::lock_guard<std::mutex> guard(impl->mutex);auto depth=impl->depth;
    const auto owner=impl->owners.find(depth.image);
    depth.owner_queue_family=owner==impl->owners.end()?VK_QUEUE_FAMILY_IGNORED:owner->second;
    return depth;
}
SwapchainState GraphicsState::swapchain(VkSwapchainKHR swapchain) const {
    std::lock_guard<std::mutex> guard(impl->mutex);
    const auto found=impl->swapchains.find(swapchain);if(found==impl->swapchains.end())return {};
    auto out=found->second;
    for(auto image:out.images){const auto owner=impl->owners.find(image);out.image_owners.push_back(owner==impl->owners.end()?VK_QUEUE_FAMILY_IGNORED:owner->second);}
    return out;
}

}
