#define VK_NO_PROTOTYPES
#include "debug_report_gpu.hpp"
#include "debug_report_service.hpp"
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/backends/imgui_impl_vulkan.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace evrm {namespace {
template<class T>T info(VkStructureType type){T value{};value.sType=type;return value;}
void check(VkResult result){if(result!=VK_SUCCESS)throw DebugReportCode::Unavailable;}
struct Buffer {VkBuffer handle{};VkDeviceMemory memory{};void* mapped=nullptr;};
std::atomic<std::uint64_t> next_source{0};
}
struct DebugReportGpu::Impl {
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr gdpa{};
    VkFormat format{};VkExtent2D extent{};std::vector<VkImage> targets;VkImageLayout layout{};bool supported=false,poisoned=false;
    VkPhysicalDeviceMemoryProperties memory{};VkSampler sampler{};
    Buffer capture,staging;VkDescriptorSetLayout capture_set_layout{};VkDescriptorPool capture_pool{};VkDescriptorSet capture_set{};
    VkPipelineLayout pipeline_layout{};VkPipeline pipeline{};VkShaderModule shader{};std::vector<VkImageView> views;
    VkFence capture_fence{};std::uint64_t capture_job=0;std::uint32_t width=0,height=0;bool recorded=false;
    std::uint64_t source=0,failed_preview_job=0;
    VkImage preview_image{};VkDeviceMemory preview_memory{};VkImageView preview_view{};VkDescriptorSet preview_set{};
    std::uint64_t preview_job=0;std::string preview_id;std::uint32_t preview_width=0,preview_height=0;bool upload_pending=false,upload_recorded=false;
    struct Shown {std::uint64_t job=0;std::array<char,65> id{};std::size_t length=0;};std::vector<Shown> shown;
    template<class T>T fn(const char* name)const{const auto raw=gdpa(device,name);if(!raw)throw DebugReportCode::Unavailable;return reinterpret_cast<T>(raw);}
    std::uint32_t memory_type(std::uint32_t bits,VkMemoryPropertyFlags flags){for(unsigned i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;throw DebugReportCode::Unsupported;}
    void buffer(Buffer& out,VkDeviceSize bytes,VkBufferUsageFlags usage){
        auto create=info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);create.size=bytes;create.usage=usage;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(fn<PFN_vkCreateBuffer>("vkCreateBuffer")(device,&create,nullptr,&out.handle));VkMemoryRequirements requirements{};fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device,out.handle,&requirements);
        if(requirements.size>16*1024*1024)throw DebugReportCode::Limit;
        auto allocate=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(fn<PFN_vkAllocateMemory>("vkAllocateMemory")(device,&allocate,nullptr,&out.memory));check(fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device,out.handle,out.memory,0));check(fn<PFN_vkMapMemory>("vkMapMemory")(device,out.memory,0,VK_WHOLE_SIZE,0,&out.mapped));
    }
    void free_buffer(Buffer& value){if(value.mapped)fn<PFN_vkUnmapMemory>("vkUnmapMemory")(device,value.memory);if(value.handle)fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device,value.handle,nullptr);if(value.memory)fn<PFN_vkFreeMemory>("vkFreeMemory")(device,value.memory,nullptr);value={};}
    void clear_capture(){
        free_buffer(capture);for(auto view:views)if(view)fn<PFN_vkDestroyImageView>("vkDestroyImageView")(device,view,nullptr);views.clear();
        if(pipeline)fn<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device,pipeline,nullptr);
        if(shader)fn<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device,shader,nullptr);
        if(pipeline_layout)fn<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device,pipeline_layout,nullptr);
        if(capture_pool)fn<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(device,capture_pool,nullptr);
        if(capture_set_layout)fn<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device,capture_set_layout,nullptr);
        pipeline={};shader={};pipeline_layout={};capture_pool={};capture_set_layout={};capture_set={};capture_fence={};capture_job=0;recorded=false;
    }
    void clear_preview(){
        if(preview_set)ImGui_ImplVulkan_RemoveTexture(preview_set);
        if(preview_view)fn<PFN_vkDestroyImageView>("vkDestroyImageView")(device,preview_view,nullptr);
        if(preview_image)fn<PFN_vkDestroyImage>("vkDestroyImage")(device,preview_image,nullptr);
        if(preview_memory)fn<PFN_vkFreeMemory>("vkFreeMemory")(device,preview_memory,nullptr);
        free_buffer(staging);preview_set={};preview_view={};preview_image={};preview_memory={};preview_job=0;preview_id.clear();upload_pending=upload_recorded=false;
    }
    void ensure_sampler(){if(sampler)return;auto create=info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);create.magFilter=create.minFilter=VK_FILTER_NEAREST;create.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;create.addressModeU=create.addressModeV=create.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;create.maxLod=0;check(fn<PFN_vkCreateSampler>("vkCreateSampler")(device,&create,nullptr,&sampler));}
};
DebugReportGpu::DebugReportGpu(VkInstance instance,VkPhysicalDevice physical,VkDevice device,PFN_vkGetInstanceProcAddr gipa,PFN_vkGetDeviceProcAddr gdpa,
    VkFormat format,VkExtent2D extent,std::vector<VkImage> targets,VkImageLayout layout,bool supported):impl_(std::make_unique<Impl>()){
    auto& s=*impl_;s.instance=instance;s.physical=physical;s.device=device;s.gipa=gipa;s.gdpa=gdpa;s.format=format;s.extent=extent;s.targets=std::move(targets);s.layout=layout;s.supported=supported;s.shown.resize(s.targets.size());
    s.source=next_source.fetch_add(1)+1;if(!s.source)throw DebugReportCode::Limit;
    const auto get=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"));if(!get)throw DebugReportCode::Unavailable;get(physical,&s.memory);
}
DebugReportGpu::~DebugReportGpu(){invalidate();try{impl_->clear_preview();impl_->clear_capture();if(impl_->sampler)impl_->fn<PFN_vkDestroySampler>("vkDestroySampler")(impl_->device,impl_->sampler,nullptr);}catch(...) {}}
void DebugReportGpu::invalidate()noexcept{debug_report_capture_retired(impl_->source);}
void DebugReportGpu::poll(const DebugReportStatus& status)noexcept{try{auto& s=*impl_;if(!s.capture_fence||s.poisoned)return;
    const auto result=s.fn<PFN_vkGetFenceStatus>("vkGetFenceStatus")(s.device,s.capture_fence);if(result==VK_NOT_READY)return;
    if(result!=VK_SUCCESS){debug_report_capture_failed(s.capture_job,DebugReportCode::Unavailable);s.poisoned=true;return;}
    if(status.job==s.capture_job&&(status.phase==DebugReportPhase::Collecting||status.phase==DebugReportPhase::AwaitingPixels)){
        auto image=std::make_shared<DebugReportImage>();image->width=s.width;image->height=s.height;const auto* bytes=static_cast<const std::uint8_t*>(s.capture.mapped);image->rgba.assign(bytes,bytes+std::size_t(s.width)*s.height*4);debug_report_submit_pixels(s.capture_job,std::move(image),s.source);}
    s.clear_capture();
}catch(...){debug_report_capture_failed(impl_->capture_job,DebugReportCode::Unavailable);}}
bool DebugReportGpu::prepare_capture(const DebugReportStatus& status)noexcept{auto& s=*impl_;try{
    if(s.capture_job&&!s.capture_fence&&!s.recorded&&status.job!=s.capture_job)s.clear_capture();
    if(!status.capture_requested||!status.capture_program||s.poisoned)return false;
    if(s.capture_job)return s.capture_job==status.job&&!s.capture_fence&&!s.recorded;
    if(!s.supported||s.targets.empty()||s.targets.size()>16||!s.extent.width||!s.extent.height)throw DebugReportCode::Unsupported;
    s.capture_job=status.job;s.ensure_sampler();const double scale=std::min({1.,1920./s.extent.width,1080./s.extent.height});s.width=std::max(1u,std::uint32_t(std::floor(s.extent.width*scale)));s.height=std::max(1u,std::uint32_t(std::floor(s.extent.height*scale)));
    s.buffer(s.capture,std::uint64_t(s.width)*s.height*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    s.views.resize(s.targets.size());for(std::size_t i=0;i<s.targets.size();++i){auto create=info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);create.image=s.targets[i];create.viewType=VK_IMAGE_VIEW_TYPE_2D;create.format=s.format;create.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};check(s.fn<PFN_vkCreateImageView>("vkCreateImageView")(s.device,&create,nullptr,&s.views[i]));}
    const VkDescriptorSetLayoutBinding bindings[]={{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    auto set=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);set.bindingCount=3;set.pBindings=bindings;check(s.fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(s.device,&set,nullptr,&s.capture_set_layout));
    const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};auto pool=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool.maxSets=1;pool.poolSizeCount=3;pool.pPoolSizes=sizes;check(s.fn<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(s.device,&pool,nullptr,&s.capture_pool));
    auto allocate=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);allocate.descriptorPool=s.capture_pool;allocate.descriptorSetCount=1;allocate.pSetLayouts=&s.capture_set_layout;check(s.fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(s.device,&allocate,&s.capture_set));
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,16};auto layout=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);layout.setLayoutCount=1;layout.pSetLayouts=&s.capture_set_layout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;check(s.fn<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(s.device,&layout,nullptr,&s.pipeline_layout));
    const auto& words=*status.capture_program;if(words.size()<5||words.size()>16384||words.front()!=0x07230203u)throw DebugReportCode::Invalid;
    auto shader=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);shader.codeSize=words.size()*4;shader.pCode=words.data();check(s.fn<PFN_vkCreateShaderModule>("vkCreateShaderModule")(s.device,&shader,nullptr,&s.shader));
    auto pipeline=info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);pipeline.layout=s.pipeline_layout;pipeline.stage=info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=s.shader;pipeline.stage.pName="main";check(s.fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(s.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&s.pipeline));
    return true;
}catch(DebugReportCode code){debug_report_capture_failed(status.job,code);}catch(...){debug_report_capture_failed(status.job,DebugReportCode::Unavailable);}try{s.clear_capture();}catch(...){}return false;}
bool DebugReportGpu::record_capture(VkCommandBuffer command,std::uint32_t image)noexcept{try{auto& s=*impl_;if(!s.capture_job||image>=s.views.size()||!debug_report_capture_started(s.capture_job,s.source))return false;
    VkDescriptorImageInfo source{{},s.views[image],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},sampler{s.sampler,{},VK_IMAGE_LAYOUT_UNDEFINED};VkDescriptorBufferInfo buffer{s.capture.handle,0,std::uint64_t(s.width)*s.height*4};std::array<VkWriteDescriptorSet,3> writes{};
    for(unsigned n=0;n<3;++n){writes[n]=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);writes[n].dstSet=s.capture_set;writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=n==0?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:n==1?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;}
    writes[0].pImageInfo=&source;writes[1].pImageInfo=&sampler;writes[2].pBufferInfo=&buffer;s.fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(s.device,3,writes.data(),0,nullptr);
    auto barrier=info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);barrier.image=s.targets[image];barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.oldLayout=s.layout;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    s.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    s.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_COMPUTE,s.pipeline);s.fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_COMPUTE,s.pipeline_layout,0,1,&s.capture_set,0,nullptr);
    const std::uint32_t push[]={s.width,s.height,s.format==VK_FORMAT_R8G8B8A8_SRGB||s.format==VK_FORMAT_B8G8R8A8_SRGB?1u:0u,0};s.fn<PFN_vkCmdPushConstants>("vkCmdPushConstants")(command,s.pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,push);s.fn<PFN_vkCmdDispatch>("vkCmdDispatch")(command,(s.width+7)/8,(s.height+7)/8,1);
    barrier.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.newLayout=s.layout;barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    auto host=info<VkBufferMemoryBarrier>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);host.buffer=s.capture.handle;host.offset=0;host.size=VK_WHOLE_SIZE;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    s.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,1,&barrier);s.recorded=true;return true;
}catch(...){debug_report_capture_failed(impl_->capture_job,DebugReportCode::Unavailable);return false;}}
void DebugReportGpu::prepare_preview(const DebugReportStatus& status,bool complete)noexcept{auto& s=*impl_;try{
    const bool wanted=status.phase==DebugReportPhase::Preview&&status.preview;
    if(wanted&&status.capture_source&&status.capture_source!=s.source){debug_report_capture_retired(status.capture_source);return;}
    if(s.preview_job&&(!wanted||status.job!=s.preview_job||status.image_id!=s.preview_id)){if(!complete)return;s.clear_preview();}
    if(!wanted||s.preview_job||s.poisoned||s.failed_preview_job==status.job)return;
    const auto& image=*status.preview;if(!image.width||!image.height||image.width>1920||image.height>1080||image.rgba.size()!=std::uint64_t(image.width)*image.height*4)return;
    s.ensure_sampler();s.preview_job=status.job;s.preview_id=status.image_id;s.buffer(s.staging,image.rgba.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);std::memcpy(s.staging.mapped,image.rgba.data(),image.rgba.size());
    s.preview_width=image.width;s.preview_height=image.height;
    auto create=info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);create.imageType=VK_IMAGE_TYPE_2D;create.format=s.format==VK_FORMAT_R8G8B8A8_SRGB||s.format==VK_FORMAT_B8G8R8A8_SRGB?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;create.extent={image.width,image.height,1};create.mipLevels=create.arrayLayers=1;create.samples=VK_SAMPLE_COUNT_1_BIT;create.tiling=VK_IMAGE_TILING_OPTIMAL;create.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(s.fn<PFN_vkCreateImage>("vkCreateImage")(s.device,&create,nullptr,&s.preview_image));VkMemoryRequirements requirements{};s.fn<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(s.device,s.preview_image,&requirements);if(requirements.size>16*1024*1024)throw DebugReportCode::Limit;
    auto allocate=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=s.memory_type(requirements.memoryTypeBits,0);check(s.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(s.device,&allocate,nullptr,&s.preview_memory));check(s.fn<PFN_vkBindImageMemory>("vkBindImageMemory")(s.device,s.preview_image,s.preview_memory,0));
    auto view=info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);view.image=s.preview_image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=create.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};check(s.fn<PFN_vkCreateImageView>("vkCreateImageView")(s.device,&view,nullptr,&s.preview_view));
    s.preview_set=ImGui_ImplVulkan_AddTexture(s.sampler,s.preview_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);if(!s.preview_set)throw DebugReportCode::Unavailable;s.upload_pending=true;
}catch(...){s.failed_preview_job=status.job;debug_report_capture_failed(status.job,DebugReportCode::Unavailable);try{s.clear_preview();}catch(...){} }}
void DebugReportGpu::record_preview_upload(VkCommandBuffer command)noexcept{try{auto& s=*impl_;if(!s.upload_pending||!s.preview_set)return;
    auto barrier=info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);barrier.image=s.preview_image;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    s.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={s.preview_width,s.preview_height,1};s.fn<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(command,s.staging.handle,s.preview_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;s.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);s.upload_recorded=true;
}catch(...) {}}
std::uint64_t DebugReportGpu::preview_texture(const DebugReportStatus& status)const noexcept{const auto& s=*impl_;return !s.upload_pending&&s.preview_job==status.job&&s.preview_id==status.image_id&&status.phase==DebugReportPhase::Preview?std::uint64_t(reinterpret_cast<std::uintptr_t>(s.preview_set)):0;}
void DebugReportGpu::submitted(VkFence fence,bool success,std::uint32_t image,bool visible)noexcept{try{auto& s=*impl_;
    if(s.recorded){if(success)s.capture_fence=fence;else{debug_report_capture_failed(s.capture_job,DebugReportCode::Unavailable);s.clear_capture();}s.recorded=false;}
    if(s.upload_recorded&&success)s.upload_pending=false;
    s.upload_recorded=false;if(image<s.shown.size()){auto& shown=s.shown[image];shown.job=success&&visible?s.preview_job:0;shown.length=shown.job?std::min(s.preview_id.size(),shown.id.size()-1):0;std::memcpy(shown.id.data(),s.preview_id.data(),shown.length);}
}catch(...) {}}
void DebugReportGpu::presented(std::uint32_t image,VkResult result)noexcept{try{auto& s=*impl_;if(image<s.shown.size()&&(result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR)){const auto& shown=s.shown[image];if(shown.job)debug_report_preview_presented(shown.job,std::string_view(shown.id.data(),shown.length));}}catch(...) {}}
}
