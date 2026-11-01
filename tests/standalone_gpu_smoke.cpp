#define WIN32_LEAN_AND_MEAN
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
template<class T>T init(VkStructureType type){T value{};value.sType=type;return value;}
static void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static void check(VkResult result){need(result==VK_SUCCESS,"Vulkan call failed");}
template<class T>T symbol(HMODULE module,const char* name){const auto p=GetProcAddress(module,name);T result{};static_assert(sizeof p==sizeof result);std::memcpy(&result,&p,sizeof p);return result;}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(GetErrorMode()|0x8003);
    try {
        need(argc==2,"compiled owned shader argument required");
        const auto loader=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);need(loader!=nullptr,"system Vulkan loader unavailable");
        const auto gipa=symbol<PFN_vkGetInstanceProcAddr>(loader,"vkGetInstanceProcAddr");need(gipa!=nullptr,"instance procedure lookup");
        VkInstance instance{};
#define I(name) reinterpret_cast<PFN_##name>(gipa(instance,#name))
        auto app=init<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);app.apiVersion=VK_API_VERSION_1_1;
        auto create=init<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);create.pApplicationInfo=&app;check(I(vkCreateInstance)(&create,nullptr,&instance));
        std::uint32_t count=0;check(I(vkEnumeratePhysicalDevices)(instance,&count,nullptr));need(count>0,"physical device unavailable");
        std::vector<VkPhysicalDevice> physicals(count);check(I(vkEnumeratePhysicalDevices)(instance,&count,physicals.data()));const auto physical=physicals.front();
        I(vkGetPhysicalDeviceQueueFamilyProperties)(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);I(vkGetPhysicalDeviceQueueFamilyProperties)(physical,&count,families.data());
        std::uint32_t family=0;while(family<count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;need(family<count,"compute queue unavailable");
        std::uint32_t extension_count=0;check(I(vkEnumerateDeviceExtensionProperties)(physical,nullptr,&extension_count,nullptr));std::vector<VkExtensionProperties> extensions(extension_count);check(I(vkEnumerateDeviceExtensionProperties)(physical,nullptr,&extension_count,extensions.data()));
        const bool alias=std::any_of(extensions.begin(),extensions.end(),[](const auto& item){return !std::strcmp(item.extensionName,"VK_KHR_device_group");});
        const char* extension="VK_KHR_device_group";const float priority=1;
        auto queue_info=init<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
        auto device_info=init<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;if(alias){device_info.enabledExtensionCount=1;device_info.ppEnabledExtensionNames=&extension;}
        VkDevice device{};check(I(vkCreateDevice)(physical,&device_info,nullptr,&device));const auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance,"vkGetDeviceProcAddr"));
#define D(name) reinterpret_cast<PFN_##name>(gdpa(device,#name))
        VkQueue queue{};D(vkGetDeviceQueue)(device,family,0,&queue);
        auto buffer_info=init<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);buffer_info.size=256;buffer_info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;buffer_info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer{};check(D(vkCreateBuffer)(device,&buffer_info,nullptr,&buffer));VkMemoryRequirements requirements{};D(vkGetBufferMemoryRequirements)(device,buffer,&requirements);
        VkPhysicalDeviceMemoryProperties memory{};I(vkGetPhysicalDeviceMemoryProperties)(physical,&memory);std::uint32_t type=0;
        for(;type<memory.memoryTypeCount;++type)if((requirements.memoryTypeBits&(1u<<type))&&(memory.memoryTypes[type].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))break;
        need(type<memory.memoryTypeCount,"coherent host-visible memory unavailable");auto allocation=init<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;VkDeviceMemory storage{};
        check(D(vkAllocateMemory)(device,&allocation,nullptr,&storage));check(D(vkBindBufferMemory)(device,buffer,storage,0));void* mapped=nullptr;check(D(vkMapMemory)(device,storage,0,VK_WHOLE_SIZE,0,&mapped));auto* values=static_cast<std::uint32_t*>(mapped);
        for(unsigned i=0;i<32;++i)values[i]=i;
        values[32]=4;values[33]=1;values[34]=1;
        std::ifstream input(std::filesystem::path(argv[1]),std::ios::binary|std::ios::ate);need(bool(input),"owned shader unavailable");const auto size=input.tellg();need(size>0 && size%4==0,"owned shader length");std::vector<std::uint32_t> words(std::size_t(size)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),size);
        auto shader_info=init<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);shader_info.codeSize=std::size_t(size);shader_info.pCode=words.data();VkShaderModule shader{};check(D(vkCreateShaderModule)(device,&shader_info,nullptr,&shader));
        const VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};auto layout_info=init<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);layout_info.bindingCount=1;layout_info.pBindings=&binding;VkDescriptorSetLayout layout{};check(D(vkCreateDescriptorSetLayout)(device,&layout_info,nullptr,&layout));
        auto pipeline_layout_info=init<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);pipeline_layout_info.setLayoutCount=1;pipeline_layout_info.pSetLayouts=&layout;VkPipelineLayout pipeline_layout{};check(D(vkCreatePipelineLayout)(device,&pipeline_layout_info,nullptr,&pipeline_layout));
        auto pipeline_info=init<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);pipeline_info.flags=VK_PIPELINE_CREATE_DISPATCH_BASE_BIT;pipeline_info.layout=pipeline_layout;pipeline_info.stage=init<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_info.stage.module=shader;pipeline_info.stage.pName="cs_main";
        VkPipeline pipeline{};check(D(vkCreateComputePipelines)(device,VK_NULL_HANDLE,1,&pipeline_info,nullptr,&pipeline));
        const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};auto pool_info=init<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool_info.maxSets=1;pool_info.poolSizeCount=1;pool_info.pPoolSizes=&pool_size;VkDescriptorPool pool{};check(D(vkCreateDescriptorPool)(device,&pool_info,nullptr,&pool));
        auto set_info=init<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);set_info.descriptorPool=pool;set_info.descriptorSetCount=1;set_info.pSetLayouts=&layout;VkDescriptorSet set{};check(D(vkAllocateDescriptorSets)(device,&set_info,&set));
        const VkDescriptorBufferInfo descriptor{buffer,0,128};auto write=init<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&descriptor;D(vkUpdateDescriptorSets)(device,1,&write,0,nullptr);
        auto command_pool_info=init<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);command_pool_info.queueFamilyIndex=family;command_pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool command_pool{};check(D(vkCreateCommandPool)(device,&command_pool_info,nullptr,&command_pool));
        auto command_info=init<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);command_info.commandPool=command_pool;command_info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_info.commandBufferCount=1;VkCommandBuffer command{};check(D(vkAllocateCommandBuffers)(device,&command_info,&command));
        const auto barrier=[&](VkAccessFlags source,VkAccessFlags destination,VkPipelineStageFlags from,VkPipelineStageFlags to){auto memory_barrier=init<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);memory_barrier.srcAccessMask=source;memory_barrier.dstAccessMask=destination;D(vkCmdPipelineBarrier)(command,from,to,0,1,&memory_barrier,0,nullptr,0,nullptr);};
        const auto begin=[&]{auto info=init<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);check(D(vkBeginCommandBuffer)(command,&info));D(vkCmdBindPipeline)(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);D(vkCmdBindDescriptorSets)(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,nullptr);};
        const auto submit=[&]{barrier(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT);check(D(vkEndCommandBuffer)(command));auto info=init<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);info.commandBufferCount=1;info.pCommandBuffers=&command;check(D(vkQueueSubmit)(queue,1,&info,VK_NULL_HANDLE));check(D(vkQueueWaitIdle)(queue));};
        begin();barrier(VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);D(vkCmdDispatch)(command,4,1,1);submit();
        for(unsigned i=0;i<32;++i)need(values[i]==3*i+7,"direct compute readback mismatch");
        check(D(vkResetCommandBuffer)(command,0));begin();barrier(VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_INDIRECT_COMMAND_READ_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
        D(vkCmdFillBuffer)(command,buffer,0,128,5);const std::uint32_t replacement[]={2,3,4,5};D(vkCmdUpdateBuffer)(command,buffer,0,sizeof replacement,replacement);
        barrier(VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        need(D(vkCmdDispatchBase)!=nullptr,"core base dispatch unavailable");D(vkCmdDispatchBase)(command,1,0,0,2,1,1);
        if(alias){barrier(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);need(D(vkCmdDispatchBaseKHR)!=nullptr,"KHR base alias unavailable");D(vkCmdDispatchBaseKHR)(command,0,0,0,1,1,1);}
        barrier(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);D(vkCmdDispatchIndirect)(command,buffer,128);submit();
        for(unsigned i=0;i<32;++i){auto expected=i<4?i+2:5;if(i>=8 && i<24)expected=expected*3+7;if(alias && i<8)expected=expected*3+7;expected=expected*3+7;need(values[i]==expected,"base/indirect/fill/update readback mismatch");}
        D(vkUnmapMemory)(device,storage);D(vkDestroyCommandPool)(device,command_pool,nullptr);D(vkDestroyDescriptorPool)(device,pool,nullptr);D(vkDestroyPipeline)(device,pipeline,nullptr);D(vkDestroyPipelineLayout)(device,pipeline_layout,nullptr);D(vkDestroyDescriptorSetLayout)(device,layout,nullptr);D(vkDestroyShaderModule)(device,shader,nullptr);D(vkDestroyBuffer)(device,buffer,nullptr);D(vkFreeMemory)(device,storage,nullptr);D(vkDestroyDevice)(device,nullptr);I(vkDestroyInstance)(instance,nullptr);
        std::cout<<"PASS actual DLL/system loader GPU readback, direct/base/indirect dispatch, fill/update; KHR base alias="<<alias<<"; no game, window or capture\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
