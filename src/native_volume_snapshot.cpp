#include "native_volume_snapshot.hpp"
#include <stdexcept>
namespace evrm { namespace {
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void checked(VkResult r,const char* why){require(r==VK_SUCCESS,why);}
bool power_two(VkDeviceSize n){return n&&!(n&(n-1));}
bool pure(VkMemoryPropertyFlags f){return (f&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)&&!(f&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT));}
void validate(const std::array<NativeVolumeSnapshotSource,2>& s,const NativeVolumeSnapshotCapabilities& c){
    const auto e=s[0].extent;const std::uint64_t voxels=std::uint64_t(e.width)*e.height*e.depth;
    require(e.width&&e.height&&e.depth&&e.width<=256&&e.height<=256&&e.depth<=128&&voxels<=NativeVolumeSnapshot::voxel_limit,"Snapshot extent/budget");
    require(s[0].image&&s[1].image&&s[0].image!=s[1].image&&s[0].generation&&s[1].generation,"Snapshot image identity");
    for(const auto& a:s)require(a.type==VK_IMAGE_TYPE_3D&&a.extent.width==e.width&&a.extent.height==e.height&&a.extent.depth==e.depth&&a.samples==VK_SAMPLE_COUNT_1_BIT&&a.mip_levels==1&&a.array_layers==1&&a.usage==(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT),"Snapshot source shape");
    require(s[0].format==VK_FORMAT_R32_UINT&&s[0].flags==VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT&&s[0].format_count==2,"Snapshot mutable R32 source");
    require((s[0].formats[0]==VK_FORMAT_R32_UINT&&s[0].formats[1]==VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)||(s[0].formats[1]==VK_FORMAT_R32_UINT&&s[0].formats[0]==VK_FORMAT_E5B9G9R9_UFLOAT_PACK32),"Snapshot allowed packed view");
    require(s[1].format==VK_FORMAT_R16_UNORM&&!s[1].flags&&!s[1].format_count,"Snapshot visibility source");
    require((c.formats[0]&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)&&(c.formats[1]&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),"Snapshot source sampling unsupported");
    constexpr auto output=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    for(unsigned i=0;i<2;++i){const auto& p=c.outputs[i];require((c.formats[i+2]&output)==output&&p.maxExtent.width>=e.width&&p.maxExtent.height>=e.height&&p.maxExtent.depth>=e.depth&&p.maxMipLevels&&p.maxArrayLayers&&(p.sampleCounts&VK_SAMPLE_COUNT_1_BIT)&&p.maxResourceSize>=voxels*(i?4:16),"Snapshot output format/size unsupported");}
    const auto& l=c.limits;require(l.maxImageDimension3D>=e.width&&l.maxImageDimension3D>=e.height&&l.maxImageDimension3D>=e.depth&&l.maxBoundDescriptorSets&&l.maxPerStageDescriptorSampledImages>=2&&l.maxDescriptorSetSampledImages>=2&&l.maxPerStageDescriptorStorageImages>=2&&l.maxDescriptorSetStorageImages>=2&&l.maxPerStageResources>=4&&l.maxComputeWorkGroupInvocations>=64,"Snapshot device limits");
    const std::array<std::uint32_t,3> dimensions{e.width,e.height,e.depth};for(unsigned i=0;i<3;++i)require(l.maxComputeWorkGroupSize[i]>=4&&l.maxComputeWorkGroupCount[i]>=(dimensions[i]+3)/4,"Snapshot compute limits");
    require(c.memory.memoryTypeCount&&c.memory.memoryTypeCount<=VK_MAX_MEMORY_TYPES&&c.memory.memoryHeapCount&&c.memory.memoryHeapCount<=VK_MAX_MEMORY_HEAPS,"Snapshot memory metadata");bool found=false;
    for(unsigned i=0;i<c.memory.memoryTypeCount;++i)if(pure(c.memory.memoryTypes[i].propertyFlags)&&c.memory.memoryTypes[i].heapIndex<c.memory.memoryHeapCount)found=true;
    require(found,"Snapshot pure device-local memory unavailable");
}
}
#define SNAPSHOT_FUNCTIONS(X) \
X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) \
X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
X(vkCmdPipelineBarrier) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch)
struct NativeVolumeSnapshot::Impl {
    VkDevice device{};VkExtent3D extent{};std::uint32_t set_limit=0;bool recorded=false,failed=false,uncertain=false;
#define DECLARE(n) PFN_##n n{};
    SNAPSHOT_FUNCTIONS(DECLARE)
#undef DECLARE
    std::array<VkImageView,2> source_views{},views{};std::array<VkImage,2> images{};std::array<VkDeviceMemory,2> memory{};
    VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};VkDescriptorPool pool{};VkDescriptorSet set{};
    ~Impl(){if(uncertain)return;
        if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(shader)vkDestroyShaderModule(device,shader,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(dsl)vkDestroyDescriptorSetLayout(device,dsl,nullptr);
        for(unsigned i=0;i<2;++i){if(source_views[i])vkDestroyImageView(device,source_views[i],nullptr);if(views[i])vkDestroyImageView(device,views[i],nullptr);if(images[i])vkDestroyImage(device,images[i],nullptr);if(memory[i])vkFreeMemory(device,memory[i],nullptr);}
    }
};
NativeVolumeSnapshot::NativeVolumeSnapshot(VkDevice device,PFN_vkGetDeviceProcAddr get,const std::array<NativeVolumeSnapshotSource,2>& sources,const NativeVolumeSnapshotCapabilities& caps,const std::uint32_t* words,std::size_t word_count){
    validate(sources,caps);require(device&&get&&words&&word_count>=5&&word_count<=4*1024*1024,"Snapshot shader/driver inputs");require(words[0]==0x07230203,"Snapshot shader header");
    impl=std::make_unique<Impl>();auto& p=*impl;p.device=device;p.extent=sources[0].extent;p.set_limit=caps.limits.maxBoundDescriptorSets<32?caps.limits.maxBoundDescriptorSets:32;
#define LOAD(n) p.n=reinterpret_cast<PFN_##n>(get(device,#n));require(p.n!=nullptr,"Snapshot missing driver entry");
    SNAPSHOT_FUNCTIONS(LOAD)
#undef LOAD
    for(unsigned i=0;i<2;++i){VkImageViewUsageCreateInfo use{};use.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;use.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageViewCreateInfo v{};v.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;v.pNext=&use;v.image=sources[i].image;v.viewType=VK_IMAGE_VIEW_TYPE_3D;v.format=i?VK_FORMAT_R16_UNORM:VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};checked(p.vkCreateImageView(device,&v,nullptr,&p.source_views[i]),"Snapshot sampled source view");}
    VkDeviceSize allocated=0;
    for(unsigned i=0;i<2;++i){VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.imageType=VK_IMAGE_TYPE_3D;image.format=i?VK_FORMAT_R32_SFLOAT:VK_FORMAT_R32G32B32A32_SFLOAT;image.extent=p.extent;image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;checked(p.vkCreateImage(device,&image,nullptr,&p.images[i]),"Snapshot owned image");
        VkMemoryRequirements r{};p.vkGetImageMemoryRequirements(device,p.images[i],&r);require(r.size&&r.size<=memory_byte_limit-allocated&&power_two(r.alignment)&&r.alignment<=memory_byte_limit,"Snapshot allocation budget/requirements");allocated+=r.size;
        unsigned type=VK_MAX_MEMORY_TYPES;for(unsigned n=0;n<caps.memory.memoryTypeCount;++n){const auto& t=caps.memory.memoryTypes[n];if((r.memoryTypeBits&(1u<<n))&&pure(t.propertyFlags)&&t.heapIndex<caps.memory.memoryHeapCount&&caps.memory.memoryHeaps[t.heapIndex].size>=r.size){type=n;break;}}
        require(type<VK_MAX_MEMORY_TYPES,"Snapshot owned memory type");VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=r.size;a.memoryTypeIndex=type;checked(p.vkAllocateMemory(device,&a,nullptr,&p.memory[i]),"Snapshot owned allocation");checked(p.vkBindImageMemory(device,p.images[i],p.memory[i],0),"Snapshot owned binding");
        VkImageViewCreateInfo v{};v.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;v.image=p.images[i];v.viewType=VK_IMAGE_VIEW_TYPE_3D;v.format=image.format;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};checked(p.vkCreateImageView(device,&v,nullptr,&p.views[i]),"Snapshot owned output view");}
    const std::array<VkDescriptorSetLayoutBinding,4> bindings={{{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
    VkDescriptorSetLayoutCreateInfo dsl{};dsl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;dsl.bindingCount=4;dsl.pBindings=bindings.data();checked(p.vkCreateDescriptorSetLayout(device,&dsl,nullptr,&p.dsl),"Snapshot private DSL");
    VkPipelineLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;layout.pSetLayouts=&p.dsl;checked(p.vkCreatePipelineLayout(device,&layout,nullptr,&p.layout),"Snapshot private layout");
    VkShaderModuleCreateInfo shader{};shader.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;shader.codeSize=word_count*4;shader.pCode=words;checked(p.vkCreateShaderModule(device,&shader,nullptr,&p.shader),"Snapshot private shader");
    VkComputePipelineCreateInfo pipeline{};pipeline.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline.layout=p.layout;pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=p.shader;pipeline.stage.pName="cs_snapshot";checked(p.vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline,nullptr,&p.pipeline),"Snapshot private compute pipeline");
    const std::array<VkDescriptorPoolSize,2> sizes={{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2}}};VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=1;pool.poolSizeCount=2;pool.pPoolSizes=sizes.data();checked(p.vkCreateDescriptorPool(device,&pool,nullptr,&p.pool),"Snapshot private pool");
    VkDescriptorSetAllocateInfo sets{};sets.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;sets.descriptorPool=p.pool;sets.descriptorSetCount=1;sets.pSetLayouts=&p.dsl;checked(p.vkAllocateDescriptorSets(device,&sets,&p.set),"Snapshot private descriptor set");
    const std::array<VkDescriptorImageInfo,4> info={{{nullptr,p.source_views[0],VK_IMAGE_LAYOUT_GENERAL},{nullptr,p.source_views[1],VK_IMAGE_LAYOUT_GENERAL},{nullptr,p.views[0],VK_IMAGE_LAYOUT_GENERAL},{nullptr,p.views[1],VK_IMAGE_LAYOUT_GENERAL}}};std::array<VkWriteDescriptorSet,4> writes{};
    for(unsigned i=0;i<4;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=p.set;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=i<2?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&info[i];}p.vkUpdateDescriptorSets(device,4,writes.data(),0,nullptr);
}
NativeVolumeSnapshot::~NativeVolumeSnapshot()=default;
bool NativeVolumeSnapshot::record(VkCommandBuffer command,VkPipeline original,VkPipelineLayout compatible,std::uint32_t count,const VkDescriptorSet* sets,std::uint32_t x,std::uint32_t y,std::uint32_t z)noexcept{
    if(!impl)return false;
    auto& p=*impl;
    if(p.failed||p.recorded||p.uncertain||!command||!original||!compatible||!count||count>p.set_limit||!sets||x!=(p.extent.width+3)/4||y!=(p.extent.height+3)/4||z!=(p.extent.depth+3)/4){p.failed=true;return false;}
    for(unsigned i=0;i<count;++i)if(!sets[i]){p.failed=true;return false;}
    p.recorded=true;
    std::array<VkImageMemoryBarrier,2> images{};for(unsigned i=0;i<2;++i){auto& b=images[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=p.images[i];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,2,images.data());
    VkMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,1,&p.set,0,nullptr);p.vkCmdDispatch(command,x,y,z);
    barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    for(auto& b:images){b.oldLayout=VK_IMAGE_LAYOUT_GENERAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;}
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,2,images.data());
    p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,original);p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,compatible,0,count,sets,0,nullptr);return true;
}
std::array<VkDescriptorImageInfo,2> NativeVolumeSnapshot::outputs()const noexcept{
    if(!impl)return {};
    return {{{nullptr,impl->views[0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{nullptr,impl->views[1],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
}
void NativeVolumeSnapshot::quarantine()noexcept{if(impl)impl->uncertain=true;}
#undef SNAPSHOT_FUNCTIONS
}
