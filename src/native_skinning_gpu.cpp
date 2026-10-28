#include "native_skinning_gpu.hpp"
#include "native_prepare_contract.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace evrm {
static_assert(offsetof(NativePreparedFrame,control)==192&&sizeof(NativePreparedFrame)==224,"Skin commit ABI");
namespace {
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
void checked(VkResult r,const char* s){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(s)+": "+std::to_string(r));}
template<class T>T info(VkStructureType s){T t{};t.sType=s;return t;}
struct SkinContext {
    VkDevice device{};PFN_vkGetDeviceProcAddr get{};VkPhysicalDeviceMemoryProperties memory{};VkPhysicalDeviceLimits limits{};
    const bool* quarantine=nullptr;VkDeviceSize allocated=0;
    bool uncertain()const{return quarantine&&*quarantine;}
    template<class T>T fn(const char* s)const{return reinterpret_cast<T>(get(device,s));}
};
struct SkinBuffer {
    std::shared_ptr<SkinContext> context;VkBuffer handle{};VkDeviceMemory memory{};void* mapped=nullptr;VkDeviceSize bytes=0,reserved=0;
    VkDescriptorBufferInfo binding()const{return {handle,0,bytes};}
    ~SkinBuffer(){if(context->uncertain())return;auto& c=*context;if(mapped)c.fn<PFN_vkUnmapMemory>("vkUnmapMemory")(c.device,memory);if(handle)c.fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(c.device,handle,nullptr);if(memory)c.fn<PFN_vkFreeMemory>("vkFreeMemory")(c.device,memory,nullptr);c.allocated-=reserved;}
};
std::shared_ptr<SkinBuffer> buffer(const std::shared_ptr<SkinContext>& c,VkDeviceSize bytes,bool local,VkBufferUsageFlags usage){
    require(!c->uncertain()&&bytes>0,"Skin buffer ownership/size");
    if(usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)require(bytes<=c->limits.maxStorageBufferRange,"Skin descriptor range exceeds device limit");
    auto b=std::make_shared<SkinBuffer>();b->context=c;b->bytes=bytes;
    auto bi=info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);bi.size=bytes;bi.usage=usage;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    checked(c->fn<PFN_vkCreateBuffer>("vkCreateBuffer")(c->device,&bi,nullptr,&b->handle),"Skin buffer");
    VkMemoryRequirements req{};c->fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(c->device,b->handle,&req);
    constexpr VkDeviceSize budget=512ull*1024*1024;
    require(c->allocated<=budget&&req.size<=budget-c->allocated,"Skin buffer aggregate budget");
    const VkMemoryPropertyFlags flags=local?VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT:(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    unsigned type=c->memory.memoryTypeCount;
    for(unsigned i=0;i<c->memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(c->memory.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
    require(type<c->memory.memoryTypeCount,"Skin buffer memory type unavailable");
    auto ai=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);ai.allocationSize=req.size;ai.memoryTypeIndex=type;
    checked(c->fn<PFN_vkAllocateMemory>("vkAllocateMemory")(c->device,&ai,nullptr,&b->memory),"Skin memory");c->allocated+=req.size;b->reserved=req.size;
    checked(c->fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(c->device,b->handle,b->memory,0),"Skin memory bind");
    if(!local){checked(c->fn<PFN_vkMapMemory>("vkMapMemory")(c->device,b->memory,0,bytes,0,&b->mapped),"Skin staging map");std::memset(b->mapped,0,static_cast<std::size_t>(bytes));}
    return b;
}
VkShaderModule shader(const std::shared_ptr<SkinContext>& c,const std::filesystem::path& path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f),"Missing skin shader");auto bytes=f.tellg();require(bytes>=20&&bytes<=1024*1024&&bytes%4==0,"Invalid skin shader size");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(words.data()),bytes);require(bool(f)&&words[0]==0x07230203u,"Invalid skin shader");
    auto ci=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);ci.codeSize=words.size()*4;ci.pCode=words.data();VkShaderModule out{};checked(c->fn<PFN_vkCreateShaderModule>("vkCreateShaderModule")(c->device,&ci,nullptr,&out),"Skin shader");return out;
}
void barrier(const std::shared_ptr<SkinContext>& c,VkCommandBuffer command,VkPipelineStageFlags from,VkPipelineStageFlags to,VkAccessFlags src,VkAccessFlags dst){
    auto b=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);b.srcAccessMask=src;b.dstAccessMask=dst;c->fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,from,to,0,1,&b,0,nullptr,0,nullptr);
}
}
struct NativeSkinningGpu::Impl {
    std::shared_ptr<SkinContext> c;VkDescriptorSetLayout set{};VkPipelineLayout layout{};
    std::array<VkShaderModule,2> modules{};std::array<VkPipeline,2> pipelines{};
    ~Impl(){if(!c||c->uncertain())return;for(auto p:pipelines)if(p)c->fn<PFN_vkDestroyPipeline>("vkDestroyPipeline")(c->device,p,nullptr);for(auto s:modules)if(s)c->fn<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(c->device,s,nullptr);if(layout)c->fn<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(c->device,layout,nullptr);if(set)c->fn<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(c->device,set,nullptr);}
};
// Each lease retains the pipelines' lifetime through an erased shared owner.
// Geometry contains no predecessor and no frame/descriptors, avoiding chains.
struct NativeSkinGpuAsset::Impl {
    std::shared_ptr<void> owner;std::shared_ptr<SkinContext> c;std::shared_ptr<const NativeSkinAsset> source;
    std::shared_ptr<SkinBuffer> vertices,indices,vertex_staging,index_staging;bool uploaded=false,recorded=false;
};
struct NativeSkinGpuGeometry::Impl {
    std::shared_ptr<void> owner;std::shared_ptr<const NativeSkinAsset> source;std::shared_ptr<SkinBuffer> output;
};
struct NativeSkinGpuFrame::Impl {
    std::shared_ptr<void> owner;std::shared_ptr<SkinContext> c;std::shared_ptr<NativeSkinGpuAsset> asset;
    std::shared_ptr<SkinBuffer> palettes,batches,palette_staging,batch_staging,status,uniform;
    std::shared_ptr<const NativeSkinGpuGeometry> geometry,previous;
    VkDescriptorPool pool{};VkDescriptorSet set{};std::array<std::uint32_t,4> control{};bool recorded=false;
    ~Impl(){if(pool&&!c->uncertain())c->fn<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(c->device,pool,nullptr);}
};
NativeSkinGpuAsset::NativeSkinGpuAsset(std::shared_ptr<Impl> p):impl(std::move(p)){}
NativeSkinGpuAsset::~NativeSkinGpuAsset()=default;
VkDescriptorBufferInfo NativeSkinGpuAsset::indices()const noexcept{return impl->indices->binding();}
NativeSkinGpuGeometry::NativeSkinGpuGeometry(std::shared_ptr<Impl> p):impl(std::move(p)){}
NativeSkinGpuGeometry::~NativeSkinGpuGeometry()=default;
VkDescriptorBufferInfo NativeSkinGpuGeometry::binding()const noexcept{return impl->output->binding();}
NativeSkinGpuFrame::NativeSkinGpuFrame(std::shared_ptr<Impl> p):impl(std::move(p)){}
NativeSkinGpuFrame::~NativeSkinGpuFrame()=default;
std::shared_ptr<const NativeSkinGpuGeometry> NativeSkinGpuFrame::geometry()const noexcept{return impl->geometry;}
std::uint32_t NativeSkinGpuFrame::status_after_completion()const noexcept{std::uint32_t v=0;std::memcpy(&v,impl->status->mapped,4);return v;}
NativeSkinningGpu::NativeSkinningGpu(VkDevice device,PFN_vkGetDeviceProcAddr get,const VkPhysicalDeviceMemoryProperties& memory,const VkPhysicalDeviceLimits& limits,const std::filesystem::path& directory,const bool* quarantine){
    auto state=std::make_shared<Impl>();state->c=std::make_shared<SkinContext>();auto c=state->c;c->device=device;c->get=get;c->memory=memory;c->limits=limits;c->quarantine=quarantine;
    require(device&&get&&!c->uncertain()&&limits.maxComputeWorkGroupInvocations>=64&&limits.maxComputeWorkGroupSize[0]>=64&&limits.maxPerStageDescriptorStorageBuffers>=7&&limits.maxDescriptorSetStorageBuffers>=7&&limits.maxPerStageDescriptorUniformBuffers>=1&&limits.maxDescriptorSetUniformBuffers>=1,"Skin compute limits unsupported");
    std::array<VkDescriptorSetLayoutBinding,8> bindings{};for(unsigned i=0;i<8;++i)bindings[i]={i,i==7?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    auto si=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);si.bindingCount=bindings.size();si.pBindings=bindings.data();checked(c->fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device,&si,nullptr,&state->set),"Skin descriptor layout");
    auto li=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);li.setLayoutCount=1;li.pSetLayouts=&state->set;checked(c->fn<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device,&li,nullptr,&state->layout),"Skin pipeline layout");
    const std::array<const char*,2> names{"cs_skin","cs_finalize"};const std::array<const char*,2> files{"native_skinning.comp.spv","native_skinning.finalize.spv"};
    for(unsigned i=0;i<2;++i){state->modules[i]=shader(c,directory/files[i]);auto pi=info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);pi.layout=state->layout;pi.stage=info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=state->modules[i];pi.stage.pName=names[i];checked(c->fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(device,VK_NULL_HANDLE,1,&pi,nullptr,&state->pipelines[i]),"Skin pipeline");}
    impl=std::move(state);
}
NativeSkinningGpu::~NativeSkinningGpu()=default;
std::shared_ptr<NativeSkinGpuAsset> NativeSkinningGpu::asset(std::shared_ptr<const NativeSkinAsset> source){
    require(source&&bool(*source),"Invalid skin GPU asset");auto a=std::make_shared<NativeSkinGpuAsset::Impl>();a->owner=impl;a->c=impl->c;a->source=std::move(source);
    a->vertices=buffer(a->c,a->source->vertices.size()*64,true,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    a->indices=buffer(a->c,a->source->layout->indices.size()*4,true,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    a->vertex_staging=buffer(a->c,a->vertices->bytes,false,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);a->index_staging=buffer(a->c,a->indices->bytes,false,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::memcpy(a->vertex_staging->mapped,a->source->vertices.data(),static_cast<std::size_t>(a->vertices->bytes));std::memcpy(a->index_staging->mapped,a->source->layout->indices.data(),static_cast<std::size_t>(a->indices->bytes));
    return std::shared_ptr<NativeSkinGpuAsset>(new NativeSkinGpuAsset(std::move(a)));
}
void NativeSkinningGpu::upload(VkCommandBuffer command,const std::shared_ptr<NativeSkinGpuAsset>& asset){
    require(command&&asset&&asset->impl->owner==impl&&!asset->impl->recorded&&!impl->c->uncertain(),"Skin upload ownership");auto& a=*asset->impl;a.recorded=true;
    barrier(impl->c,command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferCopy v{0,0,a.vertices->bytes},i{0,0,a.indices->bytes};auto copy=impl->c->fn<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer");copy(command,a.vertex_staging->handle,a.vertices->handle,1,&v);copy(command,a.index_staging->handle,a.indices->handle,1,&i);
    barrier(impl->c,command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
}
void NativeSkinningGpu::finish_upload(const std::shared_ptr<NativeSkinGpuAsset>& asset){
    require(asset&&asset->impl->owner==impl&&asset->impl->recorded&&!impl->c->uncertain(),"Skin upload completion ownership");asset->impl->uploaded=true;asset->impl->vertex_staging.reset();asset->impl->index_staging.reset();
}
std::shared_ptr<NativeSkinGpuFrame> NativeSkinningGpu::frame(const std::shared_ptr<NativeSkinGpuAsset>& asset,const NativeSkinPose& pose,VkDescriptorBufferInfo prepared,std::shared_ptr<const NativeSkinGpuGeometry> previous){
    auto c=impl->c;require(!c->uncertain()&&asset&&asset->impl->owner==impl&&asset->impl->uploaded&&pose&&pose.asset==asset->impl->source&&prepared.buffer&&prepared.offset==0&&prepared.range>=224&&prepared.range!=VK_WHOLE_SIZE,"Skin frame ownership/layout");
    const auto count=pose.asset->vertices.size();require(count>0&&count<=2000000&&pose.batches.size()==pose.asset->nodes.size()&&pose.batches.size()<=2048&&pose.matrices.size()<=1048576&&(count+63)/64<=c->limits.maxComputeWorkGroupCount[0],"Skin frame dispatch limits");
    auto f=std::make_shared<NativeSkinGpuFrame::Impl>();f->owner=impl;f->c=c;f->asset=asset;
    const auto storage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    f->palettes=buffer(c,std::max<std::size_t>(64,pose.matrices.size()*64),true,storage);f->batches=buffer(c,pose.batches.size()*80,true,storage);
    f->palette_staging=buffer(c,f->palettes->bytes,false,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);f->batch_staging=buffer(c,f->batches->bytes,false,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if(!pose.matrices.empty())std::memcpy(f->palette_staging->mapped,pose.matrices.data(),pose.matrices.size()*64);
    std::memcpy(f->batch_staging->mapped,pose.batches.data(),pose.batches.size()*80);f->status=buffer(c,4,false,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto g=std::make_shared<NativeSkinGpuGeometry::Impl>();g->owner=impl;g->source=pose.asset;g->output=buffer(c,count*44,true,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    f->geometry=std::shared_ptr<NativeSkinGpuGeometry>(new NativeSkinGpuGeometry(std::move(g)));
    if(previous&&previous->impl->owner==impl&&previous->impl->source==pose.asset&&previous->binding().range==count*44)f->previous=std::move(previous);
    f->control={static_cast<std::uint32_t>(count),static_cast<std::uint32_t>(pose.batches.size()),static_cast<std::uint32_t>(pose.matrices.size()),f->previous?1u:0u};
    f->uniform=buffer(c,16,false,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);std::memcpy(f->uniform->mapped,f->control.data(),16);
    const std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,7},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}}};auto pi=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pi.maxSets=1;pi.poolSizeCount=sizes.size();pi.pPoolSizes=sizes.data();checked(c->fn<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(c->device,&pi,nullptr,&f->pool),"Skin pool");
    auto ai=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);ai.descriptorPool=f->pool;ai.descriptorSetCount=1;ai.pSetLayouts=&impl->set;checked(c->fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(c->device,&ai,&f->set),"Skin set");
    const std::array<VkDescriptorBufferInfo,8> buffers{asset->impl->vertices->binding(),f->palettes->binding(),f->batches->binding(),f->previous?f->previous->binding():f->geometry->binding(),f->geometry->binding(),f->status->binding(),prepared,f->uniform->binding()};
    std::array<VkWriteDescriptorSet,8> writes{};for(unsigned i=0;i<8;++i){auto& w=writes[i];w=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);w.dstSet=f->set;w.dstBinding=i;w.descriptorType=i==7?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.descriptorCount=1;w.pBufferInfo=&buffers[i];}c->fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(c->device,writes.size(),writes.data(),0,nullptr);
    return std::shared_ptr<NativeSkinGpuFrame>(new NativeSkinGpuFrame(std::move(f)));
}
void NativeSkinningGpu::record(VkCommandBuffer command,const std::shared_ptr<NativeSkinGpuFrame>& frame){
    require(command&&frame&&frame->impl->owner==impl&&!frame->impl->recorded&&!impl->c->uncertain(),"Skin recording ownership");auto c=impl->c;auto& f=*frame->impl;f.recorded=true;
    barrier(c,command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT);
    auto copy=c->fn<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer");VkBufferCopy p{0,0,f.palettes->bytes},b{0,0,f.batches->bytes};copy(command,f.palette_staging->handle,f.palettes->handle,1,&p);copy(command,f.batch_staging->handle,f.batches->handle,1,&b);
    barrier(c,command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    auto bind=c->fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");bind(command,VK_PIPELINE_BIND_POINT_COMPUTE,impl->pipelines[0]);c->fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_COMPUTE,impl->layout,0,1,&f.set,0,nullptr);
    auto dispatch=c->fn<PFN_vkCmdDispatch>("vkCmdDispatch");dispatch(command,(f.control[0]+63)/64,1,1);
    barrier(c,command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    bind(command,VK_PIPELINE_BIND_POINT_COMPUTE,impl->pipelines[1]);dispatch(command,1,1,1);
    barrier(c,command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_HOST_READ_BIT);
}
}
