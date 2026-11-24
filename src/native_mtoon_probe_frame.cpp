#include "native_mtoon_probe_frame.hpp"
#include "native_mtoon_probe_program.hpp"
#include "native_mtoon_probe_frame_abi.hpp"
#include "native_private_idle.hpp"
#include <stdexcept>
namespace evrm {namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void checked(VkResult result,const char* why){require(result==VK_SUCCESS,why);}
bool power_two(VkDeviceSize value){return value&&!(value&(value-1));}
bool local(VkMemoryPropertyFlags flags){return (flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)&&
    !(flags&(VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT));}
}
#define FRAME_FUNCTIONS(X) \
X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) \
X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateComputePipelines) X(vkDestroyPipeline) \
X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
X(vkCmdFillBuffer) X(vkCmdPipelineBarrier) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch)
namespace {
// The owned output buffer with its memory, and the descriptor pool and set
// that were allocated with the pinned program's output layout.
struct ProbeFrameStorage {
    VkDevice device{};const bool* parent=nullptr;bool uncertain=false;
    std::shared_ptr<NativeMToonProbeProgram> program;
    VkBuffer buffer{};VkDeviceMemory memory{};VkDescriptorPool pool{};VkDescriptorSet set{};VkDeviceSize allocated=0;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool{};PFN_vkDestroyBuffer vkDestroyBuffer{};PFN_vkFreeMemory vkFreeMemory{};
    bool quarantined()const noexcept{return uncertain||(parent&&*parent);}
    // The same three calls, in the same order, as a frame with a program.
    ~ProbeFrameStorage(){if(quarantined())return;
        if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
        if(buffer)vkDestroyBuffer(device,buffer,nullptr);
        if(memory)vkFreeMemory(device,memory,nullptr);
    }
};
}
struct NativeMToonProbeFrameReuse::Impl {
    NativePrivateIdle<ProbeFrameStorage,NativeMToonProbeFrameReuse::idle_limit> idle;std::atomic<std::uint64_t> created{0},reissued{0};
    explicit Impl(NativeMToonProbeFrameReuse::Clock clock):idle(NativeMToonProbeFrameReuse::idle_milliseconds,clock){}
};
NativeMToonProbeFrameReuse::NativeMToonProbeFrameReuse(Clock clock):impl(std::make_shared<Impl>(clock)){}
NativeMToonProbeFrameReuse::~NativeMToonProbeFrameReuse(){retain(false);}
// Entries leave the holder before they are destroyed, so no Vulkan call runs
// under its lock. A quarantined entry's destructor issues no Vulkan call.
void NativeMToonProbeFrameReuse::retain(bool keep)noexcept{if(impl){auto entries=impl->idle.drain(keep);for(auto& entry:entries)entry.reset();}}
void NativeMToonProbeFrameReuse::release_expired()noexcept{if(impl&&impl->idle.size()){auto entries=impl->idle.expire();for(auto& entry:entries)entry.reset();}}
bool NativeMToonProbeFrameReuse::try_idle(std::size_t& storages,VkDeviceSize& bytes)const noexcept{storages=0;bytes=0;if(!impl)return true;
    std::size_t count=0;VkDeviceSize sum=0;if(!impl->idle.try_visit([&](const ProbeFrameStorage& s){++count;sum+=s.allocated;}))return false;
    storages=count;bytes=sum;return true;}
#ifdef EVRM_PRIVATE_REUSE_TESTING
void NativeMToonProbeFrameReuse::test_hold_idle(void(*inside)(void*),void* context)const{impl->idle.test_hold([&]{inside(context);});}
#endif
std::size_t NativeMToonProbeFrameReuse::idle_storages()const noexcept{return impl?impl->idle.size():0;}
VkDeviceSize NativeMToonProbeFrameReuse::idle_bytes()const noexcept{VkDeviceSize bytes=0;if(impl)impl->idle.visit([&](const ProbeFrameStorage& s){bytes+=s.allocated;});return bytes;}
std::uint64_t NativeMToonProbeFrameReuse::created_storages()const noexcept{return impl?impl->created.load(std::memory_order_relaxed):0;}
std::uint64_t NativeMToonProbeFrameReuse::reissued_storages()const noexcept{return impl?impl->reissued.load(std::memory_order_relaxed):0;}
struct NativeMToonProbeFrame::Impl {
    // With storage, the buffer, memory, pool and set below are its handles and
    // are released only through it.
    std::unique_ptr<ProbeFrameStorage> storage;std::shared_ptr<NativeMToonProbeFrameReuse::Impl> reuse;VkDeviceSize allocated=0;
    VkDevice device{};NativeMToonProbeFrameSource source;const bool* parent=nullptr;
    VkPhysicalDeviceLimits limits{};VkBuffer buffer{};VkDeviceMemory memory{};
    VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};
    std::shared_ptr<NativeMToonProbeProgram> program;
    VkDescriptorPool pool{};VkDescriptorSet set{};bool recorded=false,failed=false,uncertain=false;
#define DECLARE(n) PFN_##n n{};
    FRAME_FUNCTIONS(DECLARE)
#undef DECLARE
    bool quarantined()const noexcept{return uncertain||(parent&&*parent);}
    ~Impl(){if(quarantined()){if(storage)storage->uncertain=true;return;}
        if(storage){
            // This is the existing destruction point: the original command and
            // every consumer of the output have already retired. Whatever the
            // holder does not keep is destroyed here, outside its lock.
            if(reuse){auto unkept=reuse->idle.keep(std::move(storage));unkept.reset();}
            return;
        }
        if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
        if(!program){if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);if(shader)vkDestroyShaderModule(device,shader,nullptr);
            if(layout)vkDestroyPipelineLayout(device,layout,nullptr);if(dsl)vkDestroyDescriptorSetLayout(device,dsl,nullptr);}
        if(buffer)vkDestroyBuffer(device,buffer,nullptr);
        if(memory)vkFreeMemory(device,memory,nullptr);
    }
};
NativeMToonProbeFrame::NativeMToonProbeFrame(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonProbeFrameSource& source,
    const NativeMToonProbeCopyCapabilities& caps,const std::uint32_t* words,std::size_t count,const bool* parent)
    :NativeMToonProbeFrame(device,get,source,caps,words,count,parent,{},{}){}
NativeMToonProbeFrame::NativeMToonProbeFrame(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonProbeFrameSource& source,
    const NativeMToonProbeCopyCapabilities& caps,std::shared_ptr<NativeMToonProbeProgram> program,const bool* parent,
    std::shared_ptr<NativeMToonProbeFrameReuse> reuse)
    :NativeMToonProbeFrame(device,get,source,caps,nullptr,0,parent,std::move(program),std::move(reuse)){}
NativeMToonProbeFrame::NativeMToonProbeFrame(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonProbeFrameSource& source,
    const NativeMToonProbeCopyCapabilities& caps,const std::uint32_t* words,std::size_t count,const bool* parent,std::shared_ptr<NativeMToonProbeProgram> program,
    std::shared_ptr<NativeMToonProbeFrameReuse> reuse){
    const auto& l=caps.limits;const auto& permit=source.source_permit;const auto& update=source.post_update_parameters;const auto& first=source.first_update_parameters;
    require(device&&get&&parent&&!*parent&&(program?(program->device()==device&&program->adopt(parent)):(words&&count>=5&&count<=1024*1024&&words[0]==0x07230203)),"Current probe frame input");
    require(source.coefficient_layout&&source.classifier_layout&&source.coefficient_set&&source.classifier_set&&
        source.coefficient_set!=source.classifier_set&&source.coefficient_layout!=source.classifier_layout&&permit.buffer&&permit.range==16&&
        power_two(l.minUniformBufferOffsetAlignment)&&permit.offset%l.minUniformBufferOffsetAlignment==0&&permit.offset<=~VkDeviceSize{0}-16&&
        update.buffer&&update.range==480&&update.offset%l.minUniformBufferOffsetAlignment==0&&update.offset<=~VkDeviceSize{0}-480&&
        first.buffer&&first.range==480&&first.offset%l.minUniformBufferOffsetAlignment==0&&first.offset<=~VkDeviceSize{0}-480,"Current probe frame source/permit/updates");
    require(l.maxBoundDescriptorSets>=3&&l.maxStorageBufferRange>=NativeMToonProbeFrameAbi::bytes&&l.maxUniformBufferRange>=480&&
        l.maxPerStageDescriptorStorageBuffers>=12&&l.maxDescriptorSetStorageBuffers>=12&&l.maxPerStageDescriptorUniformBuffers>=5&&
        l.maxDescriptorSetUniformBuffers>=5&&l.maxPerStageDescriptorSampledImages&&l.maxDescriptorSetSampledImages&&
        l.maxPerStageDescriptorStorageImages>=3&&l.maxDescriptorSetStorageImages>=3&&l.maxPerStageResources>=21&&
        l.maxComputeWorkGroupInvocations>=64&&l.maxComputeWorkGroupSize[0]>=64&&l.maxComputeWorkGroupSize[1]&&l.maxComputeWorkGroupSize[2]&&
        l.maxComputeWorkGroupCount[0]>=512&&l.maxComputeWorkGroupCount[1]&&l.maxComputeWorkGroupCount[2]&&l.maxImageDimension3D>=128&&
        (caps.coefficient_format_features&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT),"Current probe frame device limits");
    const auto& m=caps.memory;
    require(m.memoryTypeCount&&m.memoryTypeCount<=VK_MAX_MEMORY_TYPES&&m.memoryHeapCount&&m.memoryHeapCount<=VK_MAX_MEMORY_HEAPS,"Current probe frame memory metadata");
    impl=std::make_unique<Impl>();auto& p=*impl;p.device=device;p.source=source;p.parent=parent;p.limits=l;p.program=std::move(program);
#define LOAD(n) p.n=reinterpret_cast<PFN_##n>(get(device,#n));require(p.n!=nullptr,"Current probe frame function " #n);
    FRAME_FUNCTIONS(LOAD)
#undef LOAD
    // Only a frame with a shared program can use retired storage: its set
    // layout outlives the frame, and the storage pins that program.
    if(reuse&&reuse->impl&&p.program){p.reuse=reuse->impl;
        p.storage=p.reuse->idle.take([&](const ProbeFrameStorage& s){return s.device==device&&s.parent==parent&&s.program==p.program&&!s.quarantined();});}
    if(p.storage){p.buffer=p.storage->buffer;p.memory=p.storage->memory;p.allocated=p.storage->allocated;}
    else {
    VkBufferCreateInfo buffer{};buffer.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;buffer.size=NativeMToonProbeFrameAbi::bytes;
    buffer.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    checked(p.vkCreateBuffer(device,&buffer,nullptr,&p.buffer),"Current probe frame buffer");
    VkMemoryRequirements r{};p.vkGetBufferMemoryRequirements(device,p.buffer,&r);p.allocated=r.size;
    require(r.size>=buffer.size&&r.size<=memory_byte_limit&&power_two(r.alignment)&&r.alignment<=memory_byte_limit,"Current probe frame allocation budget");
    unsigned type=VK_MAX_MEMORY_TYPES;for(unsigned i=0;i<m.memoryTypeCount;++i){const auto& t=m.memoryTypes[i];
        if((r.memoryTypeBits&(1u<<i))&&local(t.propertyFlags)&&t.heapIndex<m.memoryHeapCount&&m.memoryHeaps[t.heapIndex].size>=r.size){type=i;break;}}
    require(type<VK_MAX_MEMORY_TYPES,"Current probe frame memory type");
    VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=r.size;allocation.memoryTypeIndex=type;
    checked(p.vkAllocateMemory(device,&allocation,nullptr,&p.memory),"Current probe frame memory");
    checked(p.vkBindBufferMemory(device,p.buffer,p.memory,0),"Current probe frame binding");
    }
    if(p.program){p.dsl=p.program->output_layout();p.layout=p.program->layout();p.pipeline=p.program->pipeline();}
    else {
    const std::array<VkDescriptorSetLayoutBinding,4> bindings{{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
    VkDescriptorSetLayoutCreateInfo dsl{};dsl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;dsl.bindingCount=4;dsl.pBindings=bindings.data();
    checked(p.vkCreateDescriptorSetLayout(device,&dsl,nullptr,&p.dsl),"Current probe frame private layout");
    const std::array<VkDescriptorSetLayout,3> layouts{source.coefficient_layout,source.classifier_layout,p.dsl};
    VkPipelineLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=3;layout.pSetLayouts=layouts.data();
    checked(p.vkCreatePipelineLayout(device,&layout,nullptr,&p.layout),"Current probe frame pipeline layout");
    VkShaderModuleCreateInfo shader{};shader.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;shader.codeSize=count*4;shader.pCode=words;
    checked(p.vkCreateShaderModule(device,&shader,nullptr,&p.shader),"Current probe frame shader");
    VkComputePipelineCreateInfo pipeline{};pipeline.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline.layout=p.layout;
    pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=p.shader;pipeline.stage.pName="cs_probe_frame";
    checked(p.vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline,nullptr,&p.pipeline),"Current probe frame pipeline");
    }
    if(p.storage){p.pool=p.storage->pool;p.set=p.storage->set;}
    else {
    const std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3}}};
    VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=1;pool.poolSizeCount=2;pool.pPoolSizes=sizes.data();
    checked(p.vkCreateDescriptorPool(device,&pool,nullptr,&p.pool),"Current probe frame descriptor pool");
    VkDescriptorSetAllocateInfo set{};set.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;set.descriptorPool=p.pool;set.descriptorSetCount=1;set.pSetLayouts=&p.dsl;
    checked(p.vkAllocateDescriptorSets(device,&set,&p.set),"Current probe frame descriptor set");
    }
    // All four descriptors are written for every frame: the permit is new each
    // time and the set may come from an earlier frame.
    const VkDescriptorBufferInfo output{p.buffer,0,NativeMToonProbeFrameAbi::bytes};std::array<VkWriteDescriptorSet,4> writes{};
    const VkDescriptorBufferInfo* inputs[]{&output,&source.source_permit,&source.post_update_parameters,&source.first_update_parameters};
    for(unsigned i=0;i<4;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=p.set;w.dstBinding=i;w.descriptorCount=1;
        w.descriptorType=i?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=inputs[i];}
    p.vkUpdateDescriptorSets(device,4,writes.data(),0,nullptr);
    // New handles move under storage only once the frame is complete. If this
    // allocation fails the frame keeps them and destroys them as before.
    if(p.reuse)(p.storage?p.reuse->reissued:p.reuse->created).fetch_add(1,std::memory_order_relaxed);
    if(p.reuse&&!p.storage){
        try{auto s=std::make_unique<ProbeFrameStorage>();s->device=device;s->parent=parent;s->program=p.program;s->buffer=p.buffer;s->memory=p.memory;s->pool=p.pool;s->set=p.set;s->allocated=p.allocated;
            s->vkDestroyDescriptorPool=p.vkDestroyDescriptorPool;s->vkDestroyBuffer=p.vkDestroyBuffer;s->vkFreeMemory=p.vkFreeMemory;p.storage=std::move(s);
        }catch(...){}
    }
}
NativeMToonProbeFrame::~NativeMToonProbeFrame()=default;
bool NativeMToonProbeFrame::record(VkCommandBuffer command,VkPipeline original,VkPipelineLayout restore,std::uint32_t count,const VkDescriptorSet* sets)noexcept{
    if(!impl)return false;auto& p=*impl;if(p.recorded||p.failed||p.quarantined())return false;
    bool valid=command&&original&&restore&&count&&count<=p.limits.maxBoundDescriptorSets&&sets;
    if(valid)for(unsigned i=0;i<count;++i)if(!sets[i])valid=false;
    if(!valid){p.failed=true;return false;}p.recorded=true;
    p.vkCmdFillBuffer(command,p.buffer,0,NativeMToonProbeFrameAbi::bytes,0);
    VkMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT;
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    const std::array<VkDescriptorSet,3> inputs{p.source.coefficient_set,p.source.classifier_set,p.set};
    p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,3,inputs.data(),0,nullptr);
    p.vkCmdDispatch(command,512,1,1);
    barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,original);p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,restore,0,count,sets,0,nullptr);
    return true;
}
VkDescriptorBufferInfo NativeMToonProbeFrame::output()const noexcept{
    return impl&&impl->recorded&&!impl->failed&&!impl->quarantined()?VkDescriptorBufferInfo{impl->buffer,0,NativeMToonProbeFrameAbi::bytes}:VkDescriptorBufferInfo{};
}
void NativeMToonProbeFrame::quarantine()noexcept{if(impl)impl->uncertain=true;}
#undef FRAME_FUNCTIONS
} // namespace evrm
