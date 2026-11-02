#define VK_NO_PROTOTYPES
#include "../src/debug_report_gpu.hpp"
#include "../src/debug_report_service.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::uintptr_t serial=10;unsigned creates=0,dispatches=0,copies=0,acks=0,received=0,failures=0;bool allocation_failure=false;VkResult fence_state=VK_NOT_READY;
std::map<VkBuffer,VkDeviceSize> sizes;std::map<VkDeviceMemory,std::vector<std::uint8_t>> memories;VkDeviceSize peak=0;
std::shared_ptr<const evrm::DebugReportImage> pixels;std::uint64_t acknowledged=0;std::string acknowledged_id;
std::uint64_t bound_source=0,retired_source=0;bool source_match=true;
template<class T>T handle(){return reinterpret_cast<T>(++serial);}
#define CREATE(name, Type, Info) VkResult VKAPI_CALL name(VkDevice,const Info*,const VkAllocationCallbacks*,Type* out){++creates;*out=handle<Type>();return VK_SUCCESS;}
CREATE(sampler,VkSampler,VkSamplerCreateInfo) CREATE(view,VkImageView,VkImageViewCreateInfo) CREATE(set_layout,VkDescriptorSetLayout,VkDescriptorSetLayoutCreateInfo)
CREATE(pool,VkDescriptorPool,VkDescriptorPoolCreateInfo) CREATE(pipeline_layout,VkPipelineLayout,VkPipelineLayoutCreateInfo) CREATE(shader,VkShaderModule,VkShaderModuleCreateInfo) CREATE(image,VkImage,VkImageCreateInfo)
#undef CREATE
#define DESTROY(name, Type) void VKAPI_CALL name(VkDevice,Type,const VkAllocationCallbacks*){}
DESTROY(destroy_sampler,VkSampler) DESTROY(destroy_view,VkImageView) DESTROY(destroy_set,VkDescriptorSetLayout) DESTROY(destroy_pool,VkDescriptorPool)
DESTROY(destroy_layout,VkPipelineLayout) DESTROY(destroy_shader,VkShaderModule) DESTROY(destroy_pipeline,VkPipeline) DESTROY(destroy_image,VkImage)
#undef DESTROY
VkResult VKAPI_CALL create_buffer(VkDevice,const VkBufferCreateInfo* info,const VkAllocationCallbacks*,VkBuffer* out){++creates;*out=handle<VkBuffer>();sizes[*out]=info->size;peak=std::max(peak,info->size);return VK_SUCCESS;}
void VKAPI_CALL requirements(VkDevice,VkBuffer buffer,VkMemoryRequirements* out){out->size=sizes[buffer];out->alignment=64;out->memoryTypeBits=1;}
void VKAPI_CALL image_requirements(VkDevice,VkImage,VkMemoryRequirements* out){out->size=16;out->alignment=64;out->memoryTypeBits=1;}
VkResult VKAPI_CALL allocate(VkDevice,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks*,VkDeviceMemory* out){if(allocation_failure){allocation_failure=false;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}*out=handle<VkDeviceMemory>();memories[*out].resize(std::size_t(info->allocationSize),123);return VK_SUCCESS;}
VkResult VKAPI_CALL map(VkDevice,VkDeviceMemory memory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void** out){*out=memories[memory].data();return VK_SUCCESS;}
void VKAPI_CALL unmap(VkDevice,VkDeviceMemory){}
void VKAPI_CALL free_memory(VkDevice,VkDeviceMemory memory,const VkAllocationCallbacks*){memories.erase(memory);}
void VKAPI_CALL destroy_buffer(VkDevice,VkBuffer buffer,const VkAllocationCallbacks*){sizes.erase(buffer);}
VkResult VKAPI_CALL bind_buffer(VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize){return VK_SUCCESS;}
VkResult VKAPI_CALL bind_image(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize){return VK_SUCCESS;}
VkResult VKAPI_CALL descriptor(VkDevice,const VkDescriptorSetAllocateInfo*,VkDescriptorSet* out){*out=handle<VkDescriptorSet>();return VK_SUCCESS;}
VkResult VKAPI_CALL pipeline(VkDevice,VkPipelineCache,uint32_t,const VkComputePipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline* out){*out=handle<VkPipeline>();return VK_SUCCESS;}
void VKAPI_CALL update(VkDevice,uint32_t,const VkWriteDescriptorSet*,uint32_t,const VkCopyDescriptorSet*){}
void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*){}
void VKAPI_CALL bind_pipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline){}
void VKAPI_CALL bind_descriptors(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,uint32_t,uint32_t,const VkDescriptorSet*,uint32_t,const uint32_t*){}
void VKAPI_CALL push(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags,uint32_t,uint32_t,const void*){}
void VKAPI_CALL dispatch(VkCommandBuffer,uint32_t x,uint32_t y,uint32_t z){need(x==240&&y==135&&z==1,"capture exceeded bounded dispatch");++dispatches;}
void VKAPI_CALL copy(VkCommandBuffer,VkBuffer,VkImage,VkImageLayout,uint32_t,const VkBufferImageCopy*){++copies;}
VkResult VKAPI_CALL fence(VkDevice,VkFence){return fence_state;}
void VKAPI_CALL memory_properties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* out){out->memoryTypeCount=1;out->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;}
PFN_vkVoidFunction VKAPI_CALL gipa(VkInstance,const char* name){return !std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties")?reinterpret_cast<PFN_vkVoidFunction>(&memory_properties):nullptr;}
PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice,const char* name){
#define FN(n,f) if(!std::strcmp(name,n))return reinterpret_cast<PFN_vkVoidFunction>(&f)
FN("vkCreateSampler",sampler);FN("vkCreateImageView",view);FN("vkCreateDescriptorSetLayout",set_layout);FN("vkCreateDescriptorPool",pool);FN("vkCreatePipelineLayout",pipeline_layout);FN("vkCreateShaderModule",shader);FN("vkCreateImage",image);
FN("vkDestroySampler",destroy_sampler);FN("vkDestroyImageView",destroy_view);FN("vkDestroyDescriptorSetLayout",destroy_set);FN("vkDestroyDescriptorPool",destroy_pool);FN("vkDestroyPipelineLayout",destroy_layout);FN("vkDestroyShaderModule",destroy_shader);FN("vkDestroyPipeline",destroy_pipeline);FN("vkDestroyImage",destroy_image);
FN("vkCreateBuffer",create_buffer);FN("vkGetBufferMemoryRequirements",requirements);FN("vkGetImageMemoryRequirements",image_requirements);FN("vkAllocateMemory",allocate);FN("vkMapMemory",map);FN("vkUnmapMemory",unmap);FN("vkFreeMemory",free_memory);FN("vkDestroyBuffer",destroy_buffer);FN("vkBindBufferMemory",bind_buffer);FN("vkBindImageMemory",bind_image);
FN("vkAllocateDescriptorSets",descriptor);FN("vkCreateComputePipelines",pipeline);FN("vkUpdateDescriptorSets",update);FN("vkCmdPipelineBarrier",barrier);FN("vkCmdBindPipeline",bind_pipeline);FN("vkCmdBindDescriptorSets",bind_descriptors);FN("vkCmdPushConstants",push);FN("vkCmdDispatch",dispatch);FN("vkCmdCopyBufferToImage",copy);FN("vkGetFenceStatus",fence);
#undef FN
throw std::runtime_error(std::string("unexpected GPU operation: ")+name);}
}
VkDescriptorSet ImGui_ImplVulkan_AddTexture(VkSampler,VkImageView,VkImageLayout){return handle<VkDescriptorSet>();}
void ImGui_ImplVulkan_RemoveTexture(VkDescriptorSet){}
namespace evrm {
bool debug_report_capture_started(std::uint64_t,std::uint64_t source)noexcept{bound_source=source;return source!=0;}
void debug_report_capture_retired(std::uint64_t source)noexcept{retired_source=source;}
void debug_report_capture_failed(std::uint64_t,DebugReportCode)noexcept{++failures;}
void debug_report_submit_pixels(std::uint64_t,std::shared_ptr<const DebugReportImage> image,std::uint64_t source)noexcept{source_match=source_match&&source==bound_source;++received;pixels=std::move(image);}
void debug_report_preview_presented(std::uint64_t job,std::string_view id)noexcept{++acks;acknowledged=job;acknowledged_id=id;}
}
int main(){try{
    {evrm::DebugReportGpu gpu({}, {},handle<VkDevice>(),gipa,gdpa,VK_FORMAT_B8G8R8A8_SRGB,{3840,2160},{handle<VkImage>(),handle<VkImage>()},VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,true);
    evrm::DebugReportStatus status;gpu.poll(status);need(!gpu.prepare_capture(status)&&creates==0,"idle report allocated GPU resources");
    status.job=1;status.phase=evrm::DebugReportPhase::AwaitingPixels;status.capture_requested=true;status.capture_program=std::make_shared<const std::vector<std::uint32_t>>(std::vector<std::uint32_t>{0x07230203,0,0,0,0});
    const auto command=handle<VkCommandBuffer>(),dummy_command=command;(void)dummy_command;const auto submitted_fence=handle<VkFence>();
    need(gpu.prepare_capture(status)&&gpu.record_capture(command,0),"capture recording failed");gpu.submitted(submitted_fence,true,0,false);gpu.poll(status);need(received==0,"uncompleted GPU pixels read");
    status.phase=evrm::DebugReportPhase::Cancelled;fence_state=VK_SUCCESS;gpu.poll(status);need(received==0&&sizes.empty(),"cancelled GPU pixels forwarded or retained");
    status.job=2;status.phase=evrm::DebugReportPhase::AwaitingPixels;need(gpu.prepare_capture(status)&&gpu.record_capture(command,1),"second capture failed");gpu.submitted(submitted_fence,false,1,false);need(received==0&&failures==1,"failed submit read pixels");
    status.job=3;need(gpu.prepare_capture(status)&&gpu.record_capture(command,0),"third capture failed");gpu.submitted(submitted_fence,true,0,false);gpu.poll(status);gpu.poll(status);
    need(received==1&&pixels&&pixels->width==1920&&pixels->height==1080&&pixels->rgba.size()==8294400&&peak<=8294400,"capture size or completed-once handoff wrong");
    status.phase=evrm::DebugReportPhase::Preview;status.image_id=std::string(64,'a');auto small=std::make_shared<evrm::DebugReportImage>();small->width=2;small->height=1;small->rgba.resize(8);status.preview=small;
    gpu.prepare_preview(status,true);need(gpu.preview_texture(status)==0,"preview exposed before upload submission");gpu.record_preview_upload(command);need(copies==1,"preview was not uploaded in caller command");gpu.submitted(submitted_fence,true,0,false);need(gpu.preview_texture(status)!=0,"exact PNG preview unavailable");gpu.presented(0,VK_SUCCESS);need(acks==0,"invisible preview approved");
    gpu.submitted(submitted_fence,true,0,true);gpu.presented(0,VK_ERROR_OUT_OF_HOST_MEMORY);need(acks==0,"failed present approved screenshot");gpu.presented(0,VK_SUCCESS);need(acks==1&&acknowledged==3&&acknowledged_id==status.image_id,"successful preview identity lost");
    status.job=4;status.image_id=std::string(64,'b');gpu.prepare_preview(status,false);need(!gpu.preview_texture(status),"stale texture shown for next job");gpu.prepare_preview(status,true);gpu.record_preview_upload(command);gpu.submitted(submitted_fence,true,0,false);need(gpu.preview_texture(status),"retired preview did not advance");
    status.phase=evrm::DebugReportPhase::Cancelled;gpu.prepare_preview(status,true);need(!gpu.preview_texture(status)&&sizes.empty(),"idle preview buffers retained");
    status.job=9;status.phase=evrm::DebugReportPhase::Preview;allocation_failure=true;gpu.prepare_preview(status,true);const auto attempted=creates;
    gpu.prepare_preview(status,true);need(creates==attempted,"failed preview retried allocations on the next frame");
    status.job=5;status.phase=evrm::DebugReportPhase::AwaitingPixels;allocation_failure=true;need(!gpu.prepare_capture(status)&&failures==3&&sizes.empty(),"GPU allocation failure not isolated/cleaned");}
    need(memories.empty()&&dispatches==3&&source_match&&retired_source==bound_source&&bound_source!=0,"GPU resources or source retirement leaked");
    {evrm::DebugReportGpu replacement({}, {},handle<VkDevice>(),gipa,gdpa,VK_FORMAT_B8G8R8A8_SRGB,{3840,2160},{handle<VkImage>()},VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,true);
        evrm::DebugReportStatus stale;stale.job=99;stale.phase=evrm::DebugReportPhase::Preview;stale.capture_source=bound_source;auto image=std::make_shared<evrm::DebugReportImage>();image->width=image->height=1;image->rgba.resize(4);stale.preview=image;const auto before=creates;
        replacement.prepare_preview(stale,true);need(creates==before&&!replacement.preview_texture(stale)&&retired_source==bound_source,"new graphics context reconstructed stale preview");}
    std::cout<<"PASS fake-GPU screenshot bounds, fence-before-read, cancellation, failed-submit isolation, exact preview identity and retirement; no submit/wait APIs\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
