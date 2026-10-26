#include "native_clip_pipelines.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

namespace evrm { namespace {
using U=std::uint64_t;
constexpr VkShaderStageFlags core_stages=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT|
    VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT|VK_SHADER_STAGE_GEOMETRY_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT;
// Descriptor/push accessibility is a bitset, not a shader identity. Native
// interfaces are reused unchanged. Private set5 adds mesh accessibility only
// when the original device explicitly enabled VK_EXT_mesh_shader.
constexpr VkShaderStageFlags recognized_accessibility_stages=core_stages|VK_SHADER_STAGE_MESH_BIT_EXT|
    VK_SHADER_STAGE_RAYGEN_BIT_KHR|VK_SHADER_STAGE_ANY_HIT_BIT_KHR|VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR|
    VK_SHADER_STAGE_MISS_BIT_KHR|VK_SHADER_STAGE_INTERSECTION_BIT_KHR|VK_SHADER_STAGE_CALLABLE_BIT_KHR;
static_assert(recognized_accessibility_stages==0x3fbfu,"recognized native accessibility stage bits");
bool accessibility_stages_known(VkShaderStageFlags flags){return flags==VK_SHADER_STAGE_ALL||!(flags&~recognized_accessibility_stages);}
bool entry(const char* text){if(!text)return false;for(unsigned i=0;i<128;++i){if(!text[i])return i!=0;}return false;}
enum Count {uniform,storage,dynamic_uniform,dynamic_storage,vertex_uniform,vertex_storage,vertex_resources,mesh_uniform,mesh_storage,mesh_resources,count_size};
struct Counts {std::array<U,count_size> v{};};
enum class Reason {unobserved,ready,disabled,capacity,layout_shape,layout_flags,layout_chain,binding_flags,binding_duplicate,binding_stages,binding_bounds,descriptor_type,missing_prefix,limits,push,shader_shape,create_failed,create_blocked,graphics_shape,rendering_shape,stage_shape,entry_mismatch,missing_shader,missing_layout};
const char* name(Reason r)noexcept{switch(r){
#define REASON(a,b) case Reason::a:return b
    REASON(unobserved,"unobserved");REASON(ready,"ready");REASON(disabled,"disabled");REASON(capacity,"capacity");REASON(layout_shape,"layout-shape");REASON(layout_flags,"layout-flags");REASON(layout_chain,"layout-chain");REASON(binding_flags,"binding-flags");REASON(binding_duplicate,"binding-duplicate");REASON(binding_stages,"binding-stages");REASON(binding_bounds,"binding-bounds");REASON(descriptor_type,"descriptor-type");REASON(missing_prefix,"missing-prefix");REASON(limits,"limits");REASON(push,"push");REASON(shader_shape,"shader-shape");REASON(create_failed,"create-failed");REASON(create_blocked,"create-blocked");REASON(graphics_shape,"graphics-shape");REASON(rendering_shape,"rendering-shape");REASON(stage_shape,"stage-shape");REASON(entry_mismatch,"entry-mismatch");REASON(missing_shader,"missing-shader");REASON(missing_layout,"missing-layout");
#undef REASON
    }return "unobserved";}
struct Fact {Reason reason=Reason::unobserved;std::uint32_t binding=UINT32_MAX,type=0,flags=0;VkShaderStageFlags stage_mask=0;
    std::uint32_t push_range=UINT32_MAX,push_count=0,push_offset=0,push_size=0,push_max_size=0;
    VkResult result=VK_NOT_READY;bool attempted=false,stage_mask_known=false;};
struct LayoutFact {Fact own,prefix;VkDescriptorSetLayout missing{};std::uint32_t index=UINT32_MAX;};
struct PipelineFact {Fact own,shader;LayoutFact layout;VkPipelineLayout original_layout{};VkShaderModule original_shader{};bool known_shader=false;};
bool metadata(const VkDescriptorSetLayoutCreateInfo& info,Counts& out,Fact& fact){
    fact.reason=info.flags?Reason::layout_flags:Reason::layout_shape;fact.flags=info.flags;
    if(info.sType!=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO||info.flags||info.bindingCount>4096||
       (info.bindingCount&&!info.pBindings))return false;
    const VkDescriptorSetLayoutBindingFlagsCreateInfo* flags=nullptr;
    if(info.pNext){
        fact.reason=Reason::layout_chain;
        flags=static_cast<const VkDescriptorSetLayoutBindingFlagsCreateInfo*>(info.pNext);
        if(flags->sType!=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO||flags->pNext||
           flags->bindingCount!=info.bindingCount||(info.bindingCount&&!flags->pBindingFlags))return false;
    }
    std::set<std::uint32_t> bindings;U total=0;
    for(std::uint32_t i=0;i<info.bindingCount;++i){
        const auto& b=info.pBindings[i];const U n=b.descriptorCount;
        fact.binding=b.binding;fact.type=b.descriptorType;fact.flags=flags?flags->pBindingFlags[i]:0;
        fact.stage_mask=b.stageFlags;fact.stage_mask_known=true;
        if(flags&&(flags->pBindingFlags[i]&~VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT)){fact.reason=Reason::binding_flags;return false;}
        if(!bindings.insert(b.binding).second){fact.reason=Reason::binding_duplicate;return false;}
        if(!accessibility_stages_known(b.stageFlags)){fact.reason=Reason::binding_stages;return false;}
        if(n>(1u<<20)||total+n>(1u<<20)){fact.reason=Reason::binding_bounds;return false;}
        total+=n;bool resource=true,u=false,s=false,du=false,ds=false;
        switch(b.descriptorType){
        case VK_DESCRIPTOR_TYPE_SAMPLER:resource=false;break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:u=true;break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:s=true;break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:u=true;du=true;break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:s=true;ds=true;break;
        default:fact.reason=Reason::descriptor_type;return false;
        }
        if(u)out.v[uniform]+=n;
        if(s)out.v[storage]+=n;
        if(du)out.v[dynamic_uniform]+=n;
        if(ds)out.v[dynamic_storage]+=n;
        if(b.stageFlags&VK_SHADER_STAGE_VERTEX_BIT){
            if(u)out.v[vertex_uniform]+=n;
            if(s)out.v[vertex_storage]+=n;
            if(resource)out.v[vertex_resources]+=n;
        }
        if(b.stageFlags&VK_SHADER_STAGE_MESH_BIT_EXT){
            if(u)out.v[mesh_uniform]+=n;
            if(s)out.v[mesh_storage]+=n;
            if(resource)out.v[mesh_resources]+=n;
        }
    }
    fact.reason=Reason::ready;return true;
}
bool admitted(const Counts& c,const VkPhysicalDeviceLimits& l,bool mesh){
    return c.v[uniform]<=l.maxDescriptorSetUniformBuffers&&c.v[storage]<=l.maxDescriptorSetStorageBuffers&&
        c.v[dynamic_uniform]<=l.maxDescriptorSetUniformBuffersDynamic&&c.v[dynamic_storage]<=l.maxDescriptorSetStorageBuffersDynamic&&
        c.v[vertex_uniform]<=l.maxPerStageDescriptorUniformBuffers&&c.v[vertex_storage]<=l.maxPerStageDescriptorStorageBuffers&&
        c.v[vertex_resources]<=l.maxPerStageResources&&(!mesh||(c.v[mesh_uniform]<=l.maxPerStageDescriptorUniformBuffers&&
        c.v[mesh_storage]<=l.maxPerStageDescriptorStorageBuffers&&c.v[mesh_resources]<=l.maxPerStageResources));
}
struct Lifetime {
    VkDevice device{};std::mutex mutex;bool quarantined=false;
    PFN_vkCreateDescriptorSetLayout create_dsl{};PFN_vkDestroyDescriptorSetLayout destroy_dsl{};
    PFN_vkCreatePipelineLayout create_layout{};PFN_vkDestroyPipelineLayout destroy_layout{};
    PFN_vkCreateShaderModule create_shader{};PFN_vkDestroyShaderModule destroy_shader{};
    PFN_vkCreateGraphicsPipelines create_pipeline{};PFN_vkDestroyPipeline destroy_pipeline{};
    bool ready()const{return create_dsl&&destroy_dsl&&create_layout&&destroy_layout&&create_shader&&destroy_shader&&create_pipeline&&destroy_pipeline;}
    template<class F>bool create(F&& call){std::lock_guard<std::mutex> lock(mutex);return !quarantined&&call()==VK_SUCCESS;}
    void quarantine()noexcept{try{std::lock_guard<std::mutex> lock(mutex);quarantined=true;}catch(...){}}
};
struct Resource {
    std::shared_ptr<Lifetime> state;
    VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};
    NativeClipKind kind=NativeClipKind::none;
    bool mesh=false;
    std::shared_ptr<Resource> parent;
    explicit Resource(std::shared_ptr<Lifetime> s):state(std::move(s)){}
    ~Resource()noexcept{
        try{
            std::lock_guard<std::mutex> lock(state->mutex);
            if(!state->quarantined){
                if(pipeline)state->destroy_pipeline(state->device,pipeline,nullptr);
                if(shader)state->destroy_shader(state->device,shader,nullptr);
                if(layout)state->destroy_layout(state->device,layout,nullptr);
                if(dsl)state->destroy_dsl(state->device,dsl,nullptr);
            }
        }catch(...){}
    }
};
template<class F>F load(PFN_vkGetDeviceProcAddr get,VkDevice device,const char* name){return reinterpret_cast<F>(get(device,name));}
} // namespace

struct NativeClipPipelines::Impl {
    mutable std::mutex mutex;
    std::shared_ptr<Lifetime> state;
    VkPhysicalDeviceLimits limits{};
    std::shared_ptr<Resource> dsl;
    bool enabled=true;
    bool mesh_enabled=false;
    bool shadow_probe_enabled=false;
    const char* disabled_cause="none";
    bool diagnostic_overflow=false;
    Fact initialization;
    std::map<VkDescriptorSetLayout,Fact> layout_facts;
    std::map<VkPipelineLayout,LayoutFact> pipeline_layout_facts;
    std::map<VkShaderModule,Fact> shader_facts;
    std::map<VkPipeline,PipelineFact> pipeline_facts;
    std::map<VkDescriptorSetLayout,Counts> layouts;
    std::map<VkPipelineLayout,std::shared_ptr<Resource>> pipeline_layouts;
    std::map<VkShaderModule,std::shared_ptr<Resource>> shaders;
    std::map<VkShaderModule,std::shared_ptr<Resource>> shadow_shaders;
    std::map<VkPipeline,std::shared_ptr<const NativeClipPipeline>> pipelines;
    std::map<VkPipeline,std::shared_ptr<const NativeClipPipeline>> shadow_pipelines;
    // Diagnostics are never consulted for admission. All allocation failures
    // are caught locally, including when constructing a new map node.
    template<class K,class V>void note(std::map<K,V>& facts,K key,const V& value,std::size_t cap)noexcept{
        try{auto found=facts.find(key);if(found!=facts.end())found->second=value;
            else if(facts.size()<cap)facts.emplace(key,value);else diagnostic_overflow=true;
        }catch(...){diagnostic_overflow=true;}
    }
    Impl(VkDevice device,PFN_vkGetDeviceProcAddr get,const VkPhysicalDeviceLimits& l,bool mesh,bool shadow_probe):state(std::make_shared<Lifetime>()),limits(l),mesh_enabled(mesh),shadow_probe_enabled(shadow_probe){
        enabled=device&&get&&l.maxBoundDescriptorSets>=6&&l.maxUniformBufferRange>=208&&l.maxStorageBufferRange>=16384;
        if(!enabled){disabled_cause="device-limits-or-dispatch";return;}
        state->device=device;
#define LOAD(field,type,name) state->field=load<type>(get,device,name)
        LOAD(create_dsl,PFN_vkCreateDescriptorSetLayout,"vkCreateDescriptorSetLayout");LOAD(destroy_dsl,PFN_vkDestroyDescriptorSetLayout,"vkDestroyDescriptorSetLayout");
        LOAD(create_layout,PFN_vkCreatePipelineLayout,"vkCreatePipelineLayout");LOAD(destroy_layout,PFN_vkDestroyPipelineLayout,"vkDestroyPipelineLayout");
        LOAD(create_shader,PFN_vkCreateShaderModule,"vkCreateShaderModule");LOAD(destroy_shader,PFN_vkDestroyShaderModule,"vkDestroyShaderModule");
        LOAD(create_pipeline,PFN_vkCreateGraphicsPipelines,"vkCreateGraphicsPipelines");LOAD(destroy_pipeline,PFN_vkDestroyPipeline,"vkDestroyPipeline");
#undef LOAD
        if(!state->ready()){enabled=false;disabled_cause="next-dispatch";return;}
        const VkShaderStageFlags private_stages=VK_SHADER_STAGE_VERTEX_BIT|(mesh_enabled?VK_SHADER_STAGE_MESH_BIT_EXT:0);
        const VkDescriptorSetLayoutBinding bindings[]={{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,private_stages,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,private_stages,nullptr}};
        VkDescriptorSetLayoutCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;info.bindingCount=2;info.pBindings=bindings;
        auto owned=std::make_shared<Resource>(state);
        if(!state->create([&]{initialization.attempted=true;return initialization.result=state->create_dsl(device,&info,nullptr,&owned->dsl);})||!owned->dsl){enabled=false;disabled_cause="private-dsl-create";initialization.reason=Reason::create_failed;return;}
        initialization.reason=Reason::ready;
        dsl=std::move(owned);
    }
    void layout(VkDescriptorSetLayout original,const VkDescriptorSetLayoutCreateInfo& info){
        layouts.erase(original);layout_facts.erase(original);if(!enabled||!original)return;
        if(layouts.size()>=4096){enabled=false;disabled_cause="layout-map-capacity";return;}
        Counts count;Fact fact;const bool accepted=metadata(info,count,fact);note(layout_facts,original,fact,4096);if(accepted)layouts.emplace(original,count);
    }
    void pipeline_layout(VkPipelineLayout original,const VkPipelineLayoutCreateInfo& info){
        pipeline_layouts.erase(original);pipeline_layout_facts.erase(original);if(!enabled||!original||!dsl)return;
        if(pipeline_layouts.size()>=1024){enabled=false;disabled_cause="pipeline-layout-map-capacity";return;}
        LayoutFact fact;fact.own.reason=Reason::layout_shape;fact.own.flags=info.flags;
        if(info.sType!=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO||info.flags||info.pNext||info.setLayoutCount!=5||!info.pSetLayouts||
           info.pushConstantRangeCount>32||(info.pushConstantRangeCount&&!info.pPushConstantRanges)){note(pipeline_layout_facts,original,fact,1024);return;}
        Counts count;std::array<VkDescriptorSetLayout,6> sets{};
        for(unsigned i=0;i<5;++i){auto found=layouts.find(info.pSetLayouts[i]);if(found==layouts.end()){
                fact.own.reason=Reason::missing_prefix;fact.index=i;fact.missing=info.pSetLayouts[i];const auto detail=layout_facts.find(fact.missing);if(detail!=layout_facts.end())fact.prefix=detail->second;
                note(pipeline_layout_facts,original,fact,1024);return;}sets[i]=found->first;for(unsigned k=0;k<count_size;++k)count.v[k]+=found->second.v[k];}
        ++count.v[uniform];++count.v[storage];++count.v[vertex_uniform];++count.v[vertex_storage];count.v[vertex_resources]+=2;
        if(mesh_enabled){++count.v[mesh_uniform];++count.v[mesh_storage];count.v[mesh_resources]+=2;}
        if(!admitted(count,limits,mesh_enabled)){fact.own.reason=Reason::limits;note(pipeline_layout_facts,original,fact,1024);return;}
        for(unsigned i=0;i<info.pushConstantRangeCount;++i){const auto& r=info.pPushConstantRanges[i];
            fact.own.push_range=i;fact.own.push_count=info.pushConstantRangeCount;fact.own.stage_mask=r.stageFlags;fact.own.stage_mask_known=true;
            fact.own.push_offset=r.offset;fact.own.push_size=r.size;fact.own.push_max_size=limits.maxPushConstantsSize;
            if(!r.size||r.offset%4||r.size%4||!r.stageFlags||!accessibility_stages_known(r.stageFlags)||U(r.offset)+r.size>limits.maxPushConstantsSize){fact.own.reason=Reason::push;note(pipeline_layout_facts,original,fact,1024);return;}}
        sets[5]=dsl->dsl;auto copy=info;copy.setLayoutCount=6;copy.pSetLayouts=sets.data();
        auto owned=std::make_shared<Resource>(state);owned->parent=dsl;
        if(state->create([&]{fact.own.attempted=true;return fact.own.result=state->create_layout(state->device,&copy,nullptr,&owned->layout);})&&owned->layout){pipeline_layouts.emplace(original,std::move(owned));fact.own.reason=Reason::ready;}
        else fact.own.reason=fact.own.attempted?Reason::create_failed:Reason::create_blocked;
        note(pipeline_layout_facts,original,fact,1024);
    }
    void shader(VkShaderModule original,const VkShaderModuleCreateInfo& info){
        shaders.erase(original);shadow_shaders.erase(original);shader_facts.erase(original);if(!enabled||!original)return;
        if(info.sType!=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO||info.flags||info.pNext||!info.pCode||info.codeSize<20||info.codeSize>131072||info.codeSize%4)return;
        NativeClipModule patched;std::string reason;
        if(!patch_native_clip_shader(info.pCode,info.codeSize,patched,reason)||patched.kind==NativeClipKind::none)return;
        // Diagnostic resources must not consume ordinary replacement capacity.
        if(patched.kind==NativeClipKind::shadow&&!shadow_probe_enabled)return;
        if(patched.mesh&&!mesh_enabled)return;
        const bool shadow=patched.kind==NativeClipKind::shadow;
        auto& targets=shadow?shadow_shaders:shaders;
        if(shadow&&targets.size()>=128)return;
        if(!shadow&&targets.size()>=4096){enabled=false;disabled_cause="shader-map-capacity";return;}
        Fact fact;
        auto copy=info;copy.codeSize=patched.words.size()*4;copy.pCode=patched.words.data();auto owned=std::make_shared<Resource>(state);owned->kind=patched.kind;owned->mesh=patched.mesh;
        if(state->create([&]{fact.attempted=true;return fact.result=state->create_shader(state->device,&copy,nullptr,&owned->shader);})&&owned->shader){targets.emplace(original,std::move(owned));fact.reason=Reason::ready;}
        else fact.reason=fact.attempted?Reason::create_failed:Reason::create_blocked;
        note(shader_facts,original,fact,4096);
    }
    void graphics(VkPipeline original,const VkGraphicsPipelineCreateInfo& info){
        pipelines.erase(original);shadow_pipelines.erase(original);pipeline_facts.erase(original);if(!enabled||!original)return;
        if(info.stageCount==1&&!shadow_probe_enabled)return;
        const bool shadow=info.stageCount==1;
        auto& targets=shadow?shadow_pipelines:pipelines;
        if(shadow&&targets.size()>=128)return;
        if(!shadow&&targets.size()>=256){enabled=false;disabled_cause="pipeline-map-capacity";return;}
        PipelineFact fact;fact.original_layout=info.layout;fact.own.reason=Reason::graphics_shape;fact.own.flags=info.flags;
        const auto record=[&]()noexcept{note(pipeline_facts,original,fact,512);};
        if(info.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO||info.flags||info.renderPass||info.subpass||!info.pNext||(info.stageCount!=1&&info.stageCount!=2)||!info.pStages){record();return;}
        const auto* rendering=static_cast<const VkPipelineRenderingCreateInfo*>(info.pNext);
        fact.own.reason=Reason::rendering_shape;
        if(rendering->sType!=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO||rendering->pNext||rendering->viewMask||rendering->stencilAttachmentFormat!=VK_FORMAT_UNDEFINED||
           rendering->colorAttachmentCount>32||(rendering->colorAttachmentCount&&!rendering->pColorAttachmentFormats)){record();return;}
        std::array<VkPipelineShaderStageCreateInfo,2> stages{};std::copy_n(info.pStages,info.stageCount,stages.data());int vertex=-1,fragment=-1;
        fact.own.reason=Reason::stage_shape;
        for(unsigned i=0;i<info.stageCount;++i){const auto& s=stages[i];if(s.sType!=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO||s.flags||s.pNext||!s.module||!entry(s.pName)||s.pSpecializationInfo){record();return;}
            if(s.stage==VK_SHADER_STAGE_VERTEX_BIT||(mesh_enabled&&s.stage==VK_SHADER_STAGE_MESH_BIT_EXT))vertex=static_cast<int>(i);else if(s.stage==VK_SHADER_STAGE_FRAGMENT_BIT)fragment=static_cast<int>(i);else{record();return;}}
        if(vertex<0||(info.stageCount==2&&fragment<0)||std::strcmp(stages[vertex].pName,stages[vertex].stage==VK_SHADER_STAGE_MESH_BIT_EXT?"ms_main":"vs_main")){fact.own.reason=Reason::entry_mismatch;record();return;}
        const auto& shader_map=shadow?shadow_shaders:shaders;
        auto shader=shader_map.find(stages[vertex].module);auto layout=pipeline_layouts.find(info.layout);
        fact.original_shader=stages[vertex].module;
        const auto shader_fact=shader_facts.find(fact.original_shader);if(shader_fact!=shader_facts.end()){fact.shader=shader_fact->second;fact.known_shader=true;}
        else if(shader!=shader_map.end()){fact.known_shader=true;fact.shader.reason=Reason::ready;}else fact.shader.reason=Reason::missing_shader;
        const auto layout_fact=pipeline_layout_facts.find(info.layout);if(layout_fact!=pipeline_layout_facts.end())fact.layout=layout_fact->second;
        else fact.layout.own.reason=layout!=pipeline_layouts.end()?Reason::ready:Reason::missing_layout;
        if(shader==shader_map.end()||layout==pipeline_layouts.end()){fact.own.reason=shader==shader_map.end()?Reason::missing_shader:Reason::missing_layout;record();return;}
        if(info.stageCount==1&&(shader->second->kind!=NativeClipKind::shadow||rendering->colorAttachmentCount||rendering->depthAttachmentFormat!=VK_FORMAT_D32_SFLOAT)){fact.own.reason=Reason::stage_shape;record();return;}
        if(shader->second->mesh!=(stages[vertex].stage==VK_SHADER_STAGE_MESH_BIT_EXT)){fact.own.reason=Reason::stage_shape;record();return;}
        stages[vertex].module=shader->second->shader;auto copy=info;copy.layout=layout->second->layout;copy.pStages=stages.data();
        auto owned=std::make_shared<Resource>(state);owned->parent=layout->second;
        if(!state->create([&]{fact.own.attempted=true;return fact.own.result=state->create_pipeline(state->device,VK_NULL_HANDLE,1,&copy,nullptr,&owned->pipeline);})||!owned->pipeline){fact.own.reason=fact.own.attempted?Reason::create_failed:Reason::create_blocked;record();return;}
        auto lease=std::make_shared<NativeClipPipeline>();lease->handle=owned->pipeline;lease->layout=owned->parent->layout;lease->original=original;lease->original_layout=info.layout;lease->kind=shader->second->kind;lease->mesh=shader->second->mesh;lease->lifetime_=std::move(owned);targets.emplace(original,std::move(lease));
        fact.own.reason=Reason::ready;record();
    }
};
NativeClipPipelines::NativeClipPipelines(VkDevice d,PFN_vkGetDeviceProcAddr get,const VkPhysicalDeviceLimits& l,bool mesh,bool shadow_probe)noexcept{try{impl_=std::make_unique<Impl>(d,get,l,mesh,shadow_probe);}catch(...){}}
NativeClipPipelines::~NativeClipPipelines()noexcept=default;
VkDescriptorSetLayout NativeClipPipelines::private_layout()const noexcept{try{if(!impl_)return VK_NULL_HANDLE;std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->dsl?impl_->dsl->dsl:VK_NULL_HANDLE;}catch(...){return VK_NULL_HANDLE;}}
#define OBSERVE(body) try{if(!impl_)return;std::lock_guard<std::mutex> lock(impl_->mutex);body;}catch(...){}
void NativeClipPipelines::layout_created(VkDescriptorSetLayout h,const VkDescriptorSetLayoutCreateInfo& i)noexcept{OBSERVE(impl_->layout(h,i))}
void NativeClipPipelines::layout_destroyed(VkDescriptorSetLayout h)noexcept{OBSERVE(impl_->layouts.erase(h);impl_->layout_facts.erase(h))}
void NativeClipPipelines::pipeline_layout_created(VkPipelineLayout h,const VkPipelineLayoutCreateInfo& i)noexcept{OBSERVE(impl_->pipeline_layout(h,i))}
void NativeClipPipelines::pipeline_layout_destroyed(VkPipelineLayout h)noexcept{OBSERVE(impl_->pipeline_layouts.erase(h);impl_->pipeline_layout_facts.erase(h))}
void NativeClipPipelines::shader_created(VkShaderModule h,const VkShaderModuleCreateInfo& i)noexcept{OBSERVE(impl_->shader(h,i))}
void NativeClipPipelines::shader_destroyed(VkShaderModule h)noexcept{OBSERVE(impl_->shaders.erase(h);impl_->shadow_shaders.erase(h);impl_->shader_facts.erase(h))}
void NativeClipPipelines::graphics_pipelines_created(std::uint32_t n,const VkGraphicsPipelineCreateInfo* info,const VkPipeline* out)noexcept{
    OBSERVE(if(!info||!out||n>256){impl_->enabled=false;impl_->disabled_cause="graphics-batch-shape-or-capacity";impl_->pipelines.clear();return;}for(std::uint32_t i=0;i<n;++i)if(out[i])impl_->graphics(out[i],info[i]))
}
void NativeClipPipelines::pipeline_destroyed(VkPipeline h)noexcept{OBSERVE(impl_->pipelines.erase(h);impl_->shadow_pipelines.erase(h);impl_->pipeline_facts.erase(h))}
#undef OBSERVE
std::shared_ptr<const NativeClipPipeline> NativeClipPipelines::pipeline(VkPipeline h)const noexcept{try{if(!impl_)return {};std::lock_guard<std::mutex> lock(impl_->mutex);auto found=impl_->pipelines.find(h);if(found!=impl_->pipelines.end())return found->second;const auto shadow=impl_->shadow_pipelines.find(h);return shadow==impl_->shadow_pipelines.end()?nullptr:shadow->second;}catch(...){return {};}}
std::string NativeClipPipelines::diagnostic(VkPipeline original,VkPipelineLayout original_layout)const noexcept{
    try{
        if(!impl_)return "manager=unavailable";
        std::lock_guard<std::mutex> lock(impl_->mutex);std::ostringstream out;
        out<<"enabled="<<impl_->enabled<<" disabled-cause="<<impl_->disabled_cause<<" diagnostic-overflow="<<impl_->diagnostic_overflow
            <<" maps="<<impl_->layouts.size()<<","<<impl_->pipeline_layouts.size()<<","<<impl_->shaders.size()<<","<<impl_->pipelines.size()
            <<" shadow-maps="<<impl_->shadow_shaders.size()<<","<<impl_->shadow_pipelines.size()
            <<" facts="<<impl_->layout_facts.size()<<","<<impl_->pipeline_layout_facts.size()<<","<<impl_->shader_facts.size()<<","<<impl_->pipeline_facts.size()
            <<" original="<<original<<" requested-layout="<<original_layout<<" pipeline-present="<<(impl_->pipelines.find(original)!=impl_->pipelines.end());
        const auto describe=[&](const char* phase,const Fact& f){out<<" "<<phase<<"-reason="<<name(f.reason);
            if(f.binding!=UINT32_MAX)out<<" binding="<<f.binding<<" descriptor-type="<<f.type;
            out<<" stage-mask-known="<<f.stage_mask_known;
            if(f.stage_mask_known)out<<" stage-mask=0x"<<std::hex<<f.stage_mask<<std::dec;
            if(f.push_range!=UINT32_MAX)out<<" push-range="<<f.push_range<<" push-count="<<f.push_count
                <<" push-offset="<<f.push_offset<<" push-size="<<f.push_size<<" push-max-size="<<f.push_max_size;
            out<<" flags="<<f.flags<<" result="<<static_cast<int>(f.result)<<" attempted="<<f.attempted;};
        describe("initialize",impl_->initialization);
        const auto observed=impl_->pipeline_facts.find(original);LayoutFact layout;
        if(observed!=impl_->pipeline_facts.end()){
            const auto& fact=observed->second;out<<" pipeline-fact=present observed-layout="<<fact.original_layout<<" shader="<<fact.original_shader<<" known-shader="<<fact.known_shader;
            describe("pipeline",fact.own);describe("shader",fact.shader);layout=fact.layout;
        }else{out<<" pipeline-fact=absent";const auto live=impl_->pipeline_layout_facts.find(original_layout);if(live!=impl_->pipeline_layout_facts.end())layout=live->second;}
        describe("layout",layout.own);
        if(layout.index!=UINT32_MAX){out<<" set="<<layout.index<<" dsl="<<layout.missing;describe("prefix",layout.prefix);}
        auto result=out.str();if(result.size()>2048)result.resize(2048);return result;
    }catch(...){return {};}
}
void NativeClipPipelines::quarantine()noexcept{if(impl_)impl_->state->quarantine();}
}
