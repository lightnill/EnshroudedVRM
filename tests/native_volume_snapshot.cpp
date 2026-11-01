#include "../src/native_volume_snapshot.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {
using namespace evrm;
template<class T> T handle(std::uintptr_t n){return reinterpret_cast<T>(n);}
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::uintptr_t serial=100;
unsigned calls=0,dispatches=0;
bool oversized_allocation=false,fail_alias=false;
VkDeviceSize allocated=0,peak=0;
struct ImageFact {VkFormat format;VkExtent3D extent;};
struct ViewFact {VkImage image;VkFormat format;VkImageUsageFlags usage;};
std::map<VkImage,ImageFact> images;
std::map<VkImageView,ViewFact> views;
std::map<VkDeviceMemory,VkDeviceSize> memories;
VkPipeline last_pipeline{};
VkPipelineLayout last_layout{};
std::array<unsigned,3> groups{};
VkResult VKAPI_CALL create_image(VkDevice,const VkImageCreateInfo* i,const VkAllocationCallbacks*,VkImage* out){
    ++calls;
    require(i->format==VK_FORMAT_R32_UINT||i->format==VK_FORMAT_R32_SFLOAT,"lighting copy expanded to a wide float format");
    require(i->usage==(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT),"unexpected private image usage");
    if(i->format==VK_FORMAT_R32_UINT){
        require(i->flags==VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,"packed image must allow its sampled view");
        const auto* list=static_cast<const VkImageFormatListCreateInfo*>(i->pNext);
        require(list&&list->sType==VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO&&list->viewFormatCount==2,"explicit packed view formats required");
        require(list->pViewFormats[0]==VK_FORMAT_R32_UINT&&list->pViewFormats[1]==VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,"packed format list mismatch");
    }
    *out=handle<VkImage>(++serial);images[*out]={i->format,i->extent};return VK_SUCCESS;
}
void VKAPI_CALL destroy_image(VkDevice,VkImage image,const VkAllocationCallbacks*){images.erase(image);}
void VKAPI_CALL image_requirements(VkDevice,VkImage image,VkMemoryRequirements* r){
    const auto& e=images.at(image).extent;
    r->size=oversized_allocation?NativeVolumeSnapshot::memory_byte_limit:((VkDeviceSize(e.width)*e.height*e.depth*4+255)&~VkDeviceSize(255));
    r->alignment=256;r->memoryTypeBits=1;
}
VkResult VKAPI_CALL allocate(VkDevice,const VkMemoryAllocateInfo* a,const VkAllocationCallbacks*,VkDeviceMemory* out){
    *out=handle<VkDeviceMemory>(++serial);memories[*out]=a->allocationSize;allocated+=a->allocationSize;peak=std::max(peak,allocated);return VK_SUCCESS;
}
void VKAPI_CALL free_memory(VkDevice,VkDeviceMemory memory,const VkAllocationCallbacks*){allocated-=memories.at(memory);memories.erase(memory);}
VkResult VKAPI_CALL bind_memory(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize){return VK_SUCCESS;}
VkResult VKAPI_CALL create_view(VkDevice,const VkImageViewCreateInfo* i,const VkAllocationCallbacks*,VkImageView* out){
    ++calls;
    const auto* use=static_cast<const VkImageViewUsageCreateInfo*>(i->pNext);
    const auto usage=use?use->usage:VkImageUsageFlags(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT);
    if(i->format==VK_FORMAT_E5B9G9R9_UFLOAT_PACK32){
        require(usage==VK_IMAGE_USAGE_SAMPLED_BIT,"packed float view must not inherit storage usage");
        require(images.count(i->image)&&images.at(i->image).format==VK_FORMAT_R32_UINT,"packed output must alias the owned uint image");
        if(fail_alias)return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    if(i->image==handle<VkImage>(1))require(i->format==VK_FORMAT_R32_UINT&&usage==VK_IMAGE_USAGE_SAMPLED_BIT,"source radiance must be read as packed uint bits");
    *out=handle<VkImageView>(++serial);views[*out]={i->image,i->format,usage};return VK_SUCCESS;
}
void VKAPI_CALL destroy_view(VkDevice,VkImageView view,const VkAllocationCallbacks*){views.erase(view);}
#define CREATE(n,T,I) VkResult VKAPI_CALL n(VkDevice,const I*,const VkAllocationCallbacks*,T* out){*out=handle<T>(++serial);return VK_SUCCESS;}
#define DESTROY(n,T) void VKAPI_CALL n(VkDevice,T,const VkAllocationCallbacks*){}
CREATE(create_dsl,VkDescriptorSetLayout,VkDescriptorSetLayoutCreateInfo)
CREATE(create_layout,VkPipelineLayout,VkPipelineLayoutCreateInfo)
CREATE(create_shader,VkShaderModule,VkShaderModuleCreateInfo)
CREATE(create_pool,VkDescriptorPool,VkDescriptorPoolCreateInfo)
DESTROY(destroy_dsl,VkDescriptorSetLayout)
DESTROY(destroy_layout,VkPipelineLayout)
DESTROY(destroy_shader,VkShaderModule)
DESTROY(destroy_pipeline,VkPipeline)
DESTROY(destroy_pool,VkDescriptorPool)
#undef CREATE
#undef DESTROY
VkResult VKAPI_CALL create_pipeline(VkDevice,VkPipelineCache,unsigned,const VkComputePipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline* out){*out=handle<VkPipeline>(++serial);return VK_SUCCESS;}
VkResult VKAPI_CALL sets(VkDevice,const VkDescriptorSetAllocateInfo*,VkDescriptorSet* out){*out=handle<VkDescriptorSet>(++serial);return VK_SUCCESS;}
void VKAPI_CALL writes(VkDevice,unsigned count,const VkWriteDescriptorSet* w,unsigned,const VkCopyDescriptorSet*){
    require(count==4,"snapshot descriptor count");
    require(views.at(w[0].pImageInfo->imageView).format==VK_FORMAT_R32_UINT,"packed input descriptor format");
    require(views.at(w[2].pImageInfo->imageView).format==VK_FORMAT_R32_UINT,"packed storage descriptor format");
    require(views.at(w[3].pImageInfo->imageView).format==VK_FORMAT_R32_SFLOAT,"visibility precision changed");
}
void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,unsigned,const VkMemoryBarrier*,unsigned,const VkBufferMemoryBarrier*,unsigned,const VkImageMemoryBarrier*){}
void VKAPI_CALL pipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline p){last_pipeline=p;}
void VKAPI_CALL descriptors(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout layout,unsigned,unsigned,const VkDescriptorSet*,unsigned,const unsigned*){last_layout=layout;}
void VKAPI_CALL dispatch(VkCommandBuffer,unsigned x,unsigned y,unsigned z){++dispatches;groups={x,y,z};}
PFN_vkVoidFunction VKAPI_CALL get(VkDevice,const char* name){
#define FN(n,f) if(!std::strcmp(name,n))return reinterpret_cast<PFN_vkVoidFunction>(f)
    FN("vkCreateImage",create_image);FN("vkDestroyImage",destroy_image);FN("vkGetImageMemoryRequirements",image_requirements);
    FN("vkAllocateMemory",allocate);FN("vkFreeMemory",free_memory);FN("vkBindImageMemory",bind_memory);
    FN("vkCreateImageView",create_view);FN("vkDestroyImageView",destroy_view);FN("vkCreateDescriptorSetLayout",create_dsl);
    FN("vkDestroyDescriptorSetLayout",destroy_dsl);FN("vkCreatePipelineLayout",create_layout);FN("vkDestroyPipelineLayout",destroy_layout);
    FN("vkCreateShaderModule",create_shader);FN("vkDestroyShaderModule",destroy_shader);FN("vkCreateComputePipelines",create_pipeline);
    FN("vkDestroyPipeline",destroy_pipeline);FN("vkCreateDescriptorPool",create_pool);FN("vkDestroyDescriptorPool",destroy_pool);
    FN("vkAllocateDescriptorSets",sets);FN("vkUpdateDescriptorSets",writes);FN("vkCmdPipelineBarrier",barrier);
    FN("vkCmdBindPipeline",pipeline);FN("vkCmdBindDescriptorSets",descriptors);FN("vkCmdDispatch",dispatch);
#undef FN
    return nullptr;
}
std::array<NativeVolumeSnapshotSource,2> sources(VkExtent3D extent){
    std::array<NativeVolumeSnapshotSource,2> result{};
    for(unsigned i=0;i<2;++i){auto& s=result[i];s.image=handle<VkImage>(i+1);s.generation=1;s.extent=extent;s.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;s.format=i?VK_FORMAT_R16_UNORM:VK_FORMAT_R32_UINT;}
    result[0].flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;result[0].format_count=2;result[0].formats[0]=VK_FORMAT_R32_UINT;result[0].formats[1]=VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    return result;
}
NativeVolumeSnapshotCapabilities caps(){
    NativeVolumeSnapshotCapabilities c;
    c.formats[0]=c.formats[2]=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    c.formats[1]=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    c.formats[3]=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    c.formats[4]=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    auto& l=c.limits;l.maxImageDimension3D=2048;l.maxBoundDescriptorSets=8;l.maxPerStageDescriptorSampledImages=l.maxDescriptorSetSampledImages=l.maxPerStageDescriptorStorageImages=l.maxDescriptorSetStorageImages=8;
    l.maxPerStageResources=32;l.maxComputeWorkGroupInvocations=1024;
    for(unsigned i=0;i<3;++i){l.maxComputeWorkGroupSize[i]=1024;l.maxComputeWorkGroupCount[i]=65535;}
    for(auto& o:c.outputs){o.maxExtent={2048,2048,2048};o.maxMipLevels=1;o.maxArrayLayers=1;o.sampleCounts=VK_SAMPLE_COUNT_1_BIT;o.maxResourceSize=512*1024*1024;}
    c.memory.memoryTypeCount=c.memory.memoryHeapCount=1;c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;c.memory.memoryTypes[0].heapIndex=0;c.memory.memoryHeaps[0].size=512*1024*1024;
    return c;
}
}
int main(int argc,char** argv){try{
    require(argc==2,"scenario required");const std::string mode=argv[1];
    VkExtent3D extent=mode=="laptop"?VkExtent3D{240,150,128}:mode=="1440p"?VkExtent3D{320,180,128}:mode=="over-budget"?VkExtent3D{480,270,128}:VkExtent3D{32,16,8};
    auto source=sources(extent);auto capabilities=caps();
    if(mode=="packed-filter")capabilities.formats.back()&=~VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    oversized_allocation=mode=="actual-allocation";fail_alias=mode=="view-failure";
    const bool rejection=mode=="over-budget"||mode=="actual-allocation"||mode=="packed-filter"||mode=="view-failure";
    bool rejected=false;const std::uint32_t words[5]{0x07230203u,0,0,0,0};
    try{
        evrm::NativeVolumeSnapshot snapshot(handle<VkDevice>(9),get,source,capabilities,words,5);
        require(!rejection,"unsupported or over-budget snapshot was accepted");
        const auto outputs=snapshot.outputs();require(views.at(outputs[0].imageView).format==VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,"radiance sampled output changed");require(views.at(outputs[1].imageView).format==VK_FORMAT_R32_SFLOAT,"visibility sampled output changed");
        const auto expected=VkDeviceSize(extent.width)*extent.height*extent.depth*8;require(peak==expected&&peak<=evrm::NativeVolumeSnapshot::memory_byte_limit,"snapshot exceeded compact memory budget");
        const VkDescriptorSet original=handle<VkDescriptorSet>(11);const auto native=handle<VkPipeline>(12);const auto layout=handle<VkPipelineLayout>(13);
        require(snapshot.record(handle<VkCommandBuffer>(10),native,layout,1,&original,(extent.width+3)/4,(extent.height+3)/4,(extent.depth+3)/4),"snapshot recording declined");
        require(dispatches==1&&groups==std::array<unsigned,3>{(extent.width+3)/4,(extent.height+3)/4,(extent.depth+3)/4}&&last_pipeline==native&&last_layout==layout,"recording did not cover volume or restore native bindings");
        require(!snapshot.record(handle<VkCommandBuffer>(10),native,layout,1,&original,1,1,1),"duplicate recording accepted");
    }catch(const std::runtime_error& error){if(!rejection)throw;
        const auto expected=mode=="over-budget"?"Snapshot extent/budget":mode=="actual-allocation"?"Snapshot allocation budget/requirements":mode=="packed-filter"?"Snapshot packed sampled format unsupported":"Snapshot packed sampled output view";
        require(std::string(error.what())==expected,"unexpected rejection cause");
        rejected=true;std::cout<<"Rejected "<<mode<<": "<<error.what()<<'\n';}
    require(rejected==rejection,"expected rejection was not observed");
    require(!allocated&&images.empty()&&views.empty()&&memories.empty(),"snapshot cleanup leaked resources");
    if(mode=="over-budget"||mode=="packed-filter")require(calls==0&&dispatches==0,"invalid input reached driver allocation");
    if(rejection)require(dispatches==0,"rejected input recorded work");
    std::cout<<"PASS "<<mode<<" peak-bytes="<<peak<<" memory-cap="<<evrm::NativeVolumeSnapshot::memory_byte_limit<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
