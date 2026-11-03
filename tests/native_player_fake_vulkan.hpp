#pragma once
namespace fake_player_gpu {
template<class T>T handle(std::uintptr_t n){return reinterpret_cast<T>(n);}
std::uintptr_t serial=1000;
std::map<VkBuffer,VkDeviceSize> sizes;
std::map<VkDeviceMemory,std::vector<unsigned char>> memory;
std::map<VkBuffer,VkDeviceMemory> backing;
std::map<VkDescriptorSet,std::map<unsigned,VkDescriptorBufferInfo>> descriptors;
VkDescriptorSet bound{};unsigned mode=0,dispatches=0,destroys=0;
VkResult VKAPI_CALL buffer(VkDevice,const VkBufferCreateInfo* i,const VkAllocationCallbacks*,VkBuffer* out){*out=handle<VkBuffer>(++serial);sizes[*out]=i->size;return VK_SUCCESS;}
void VKAPI_CALL requirements(VkDevice,VkBuffer b,VkMemoryRequirements* out){*out={sizes.at(b),16,1};}
VkResult VKAPI_CALL allocate(VkDevice,const VkMemoryAllocateInfo* i,const VkAllocationCallbacks*,VkDeviceMemory* out){*out=handle<VkDeviceMemory>(++serial);memory[*out].resize(i->allocationSize);return VK_SUCCESS;}
VkResult VKAPI_CALL bind(VkDevice,VkBuffer b,VkDeviceMemory m,VkDeviceSize){backing[b]=m;return VK_SUCCESS;}
VkResult VKAPI_CALL map(VkDevice,VkDeviceMemory m,VkDeviceSize o,VkDeviceSize,VkMemoryMapFlags,void** out){*out=memory.at(m).data()+o;return VK_SUCCESS;}
void VKAPI_CALL unmap(VkDevice,VkDeviceMemory){}
void VKAPI_CALL destroy_buffer(VkDevice,VkBuffer b,const VkAllocationCallbacks*){sizes.erase(b);backing.erase(b);++destroys;}
void VKAPI_CALL free(VkDevice,VkDeviceMemory m,const VkAllocationCallbacks*){memory.erase(m);++destroys;}
VkResult VKAPI_CALL set_layout(VkDevice,const VkDescriptorSetLayoutCreateInfo*,const VkAllocationCallbacks*,VkDescriptorSetLayout* out){*out=handle<VkDescriptorSetLayout>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL layout(VkDevice,const VkPipelineLayoutCreateInfo*,const VkAllocationCallbacks*,VkPipelineLayout* out){*out=handle<VkPipelineLayout>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL shader(VkDevice,const VkShaderModuleCreateInfo*,const VkAllocationCallbacks*,VkShaderModule* out){*out=handle<VkShaderModule>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL pipelines(VkDevice,VkPipelineCache,unsigned n,const VkComputePipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline* out){while(n--)*out++=handle<VkPipeline>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL pool(VkDevice,const VkDescriptorPoolCreateInfo*,const VkAllocationCallbacks*,VkDescriptorPool* out){*out=handle<VkDescriptorPool>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL sets(VkDevice,const VkDescriptorSetAllocateInfo* i,VkDescriptorSet* out){for(unsigned n=0;n<i->descriptorSetCount;++n)out[n]=handle<VkDescriptorSet>(++serial);return VK_SUCCESS;}
void VKAPI_CALL update(VkDevice,unsigned n,const VkWriteDescriptorSet* w,unsigned,const VkCopyDescriptorSet*){for(unsigned i=0;i<n;++i)if(w[i].pBufferInfo)descriptors[w[i].dstSet][w[i].dstBinding]=*w[i].pBufferInfo;}
template<class T>void VKAPI_CALL destroy(VkDevice,T,const VkAllocationCallbacks*){++destroys;}
void VKAPI_CALL bind_pipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline){}
void VKAPI_CALL bind_sets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,unsigned,unsigned n,const VkDescriptorSet* s,unsigned,const unsigned*){if(n)bound=s[0];}
void VKAPI_CALL push(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags,unsigned,unsigned,const void* p){std::memcpy(&mode,p,4);}
void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,unsigned,const VkMemoryBarrier*,unsigned,const VkBufferMemoryBarrier*,unsigned,const VkImageMemoryBarrier*){}
void VKAPI_CALL dispatch(VkCommandBuffer,unsigned,unsigned,unsigned){++dispatches;const auto f=descriptors.at(bound).at(0),g=descriptors.at(bound).at(1);auto* permit=reinterpret_cast<unsigned*>(memory.at(backing.at(f.buffer)).data()+f.offset+192);auto* gate=reinterpret_cast<unsigned*>(memory.at(backing.at(g.buffer)).data()+g.offset);if(!mode)*gate&=*permit;else *permit=*gate;}
VkResult VKAPI_CALL idle(VkDevice){return VK_SUCCESS;}
unsigned images_created=0,images_destroyed=0,submits=0;bool pending=false;VkResult submit_result=VK_SUCCESS,idle_result=VK_SUCCESS;
VkResult VKAPI_CALL image(VkDevice,const VkImageCreateInfo*,const VkAllocationCallbacks*,VkImage* out){*out=handle<VkImage>(++serial);++images_created;return VK_SUCCESS;}
void VKAPI_CALL image_requirements(VkDevice,VkImage,VkMemoryRequirements* out){*out={4096,16,1};}
VkResult VKAPI_CALL image_bind(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize){return VK_SUCCESS;}
VkResult VKAPI_CALL image_view(VkDevice,const VkImageViewCreateInfo*,const VkAllocationCallbacks*,VkImageView* out){*out=handle<VkImageView>(++serial);return VK_SUCCESS;}
void VKAPI_CALL destroy_image(VkDevice,VkImage,const VkAllocationCallbacks*){++images_destroyed;}
VkResult VKAPI_CALL sampler(VkDevice,const VkSamplerCreateInfo*,const VkAllocationCallbacks*,VkSampler* out){*out=handle<VkSampler>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL command_pool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* out){*out=handle<VkCommandPool>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL commands(VkDevice,const VkCommandBufferAllocateInfo* i,VkCommandBuffer* out){for(unsigned n=0;n<i->commandBufferCount;++n)out[n]=handle<VkCommandBuffer>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL fence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){*out=handle<VkFence>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL begin(VkCommandBuffer,const VkCommandBufferBeginInfo*){return VK_SUCCESS;}
VkResult VKAPI_CALL end(VkCommandBuffer){return VK_SUCCESS;}
VkResult VKAPI_CALL submit(VkQueue,unsigned,const VkSubmitInfo*,VkFence){++submits;return submit_result;}
VkResult VKAPI_CALL status(VkDevice,VkFence){return pending?VK_NOT_READY:VK_SUCCESS;}
VkResult VKAPI_CALL queue_idle(VkQueue){return idle_result;}
void VKAPI_CALL copy_image(VkCommandBuffer,VkBuffer,VkImage,VkImageLayout,unsigned,const VkBufferImageCopy*){}
void VKAPI_CALL copy_buffer(VkCommandBuffer,VkBuffer from,VkBuffer to,unsigned count,const VkBufferCopy* copies){for(unsigned n=0;n<count;++n){const auto& c=copies[n];auto& a=memory.at(backing.at(from));auto& b=memory.at(backing.at(to));if(c.srcOffset+c.size>a.size()||c.dstOffset+c.size>b.size())throw std::runtime_error("fixture buffer copy bounds");std::memcpy(b.data()+c.dstOffset,a.data()+c.srcOffset,c.size);}}
void VKAPI_CALL physical_memory(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* out){*out={};out->memoryTypeCount=1;out->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;}
void VKAPI_CALL physical_properties(VkPhysicalDevice,VkPhysicalDeviceProperties* out){*out={};out->apiVersion=VK_API_VERSION_1_3;out->limits.minUniformBufferOffsetAlignment=16;out->limits.maxStorageBufferRange=64*1024*1024;}
PFN_vkVoidFunction VKAPI_CALL instance_get(VkInstance,const char* name){if(!std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties"))return reinterpret_cast<PFN_vkVoidFunction>(physical_memory);if(!std::strcmp(name,"vkGetPhysicalDeviceProperties"))return reinterpret_cast<PFN_vkVoidFunction>(physical_properties);return nullptr;}
PFN_vkVoidFunction VKAPI_CALL get(VkDevice,const char* name){
#define ENTRY(n,f) if(!std::strcmp(name,#n))return reinterpret_cast<PFN_vkVoidFunction>(f)
ENTRY(vkCmdCopyBuffer,copy_buffer);
ENTRY(vkCreateImage,image);ENTRY(vkGetImageMemoryRequirements,image_requirements);ENTRY(vkBindImageMemory,image_bind);ENTRY(vkCreateImageView,image_view);ENTRY(vkDestroyImage,destroy_image);ENTRY(vkDestroyImageView,destroy<VkImageView>);ENTRY(vkCreateSampler,sampler);ENTRY(vkDestroySampler,destroy<VkSampler>);ENTRY(vkCreateCommandPool,command_pool);ENTRY(vkDestroyCommandPool,destroy<VkCommandPool>);ENTRY(vkAllocateCommandBuffers,commands);ENTRY(vkCreateFence,fence);ENTRY(vkDestroyFence,destroy<VkFence>);ENTRY(vkBeginCommandBuffer,begin);ENTRY(vkEndCommandBuffer,end);ENTRY(vkQueueSubmit,submit);ENTRY(vkGetFenceStatus,status);ENTRY(vkQueueWaitIdle,queue_idle);ENTRY(vkCmdCopyBufferToImage,copy_image);
ENTRY(vkCreateBuffer,buffer);ENTRY(vkGetBufferMemoryRequirements,requirements);ENTRY(vkAllocateMemory,allocate);ENTRY(vkBindBufferMemory,bind);ENTRY(vkMapMemory,map);ENTRY(vkUnmapMemory,unmap);ENTRY(vkDestroyBuffer,destroy_buffer);ENTRY(vkFreeMemory,free);ENTRY(vkCreateDescriptorSetLayout,set_layout);ENTRY(vkCreatePipelineLayout,layout);ENTRY(vkCreateShaderModule,shader);ENTRY(vkCreateComputePipelines,pipelines);ENTRY(vkCreateDescriptorPool,pool);ENTRY(vkAllocateDescriptorSets,sets);ENTRY(vkUpdateDescriptorSets,update);ENTRY(vkDestroyDescriptorPool,destroy<VkDescriptorPool>);ENTRY(vkDestroyDescriptorSetLayout,destroy<VkDescriptorSetLayout>);ENTRY(vkDestroyPipelineLayout,destroy<VkPipelineLayout>);ENTRY(vkDestroyShaderModule,destroy<VkShaderModule>);ENTRY(vkDestroyPipeline,destroy<VkPipeline>);ENTRY(vkCmdBindPipeline,bind_pipeline);ENTRY(vkCmdBindDescriptorSets,bind_sets);ENTRY(vkCmdPushConstants,push);ENTRY(vkCmdPipelineBarrier,barrier);ENTRY(vkCmdDispatch,dispatch);ENTRY(vkDeviceWaitIdle,idle);
#undef ENTRY
return nullptr;
}
}
