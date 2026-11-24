#include "native_mtoon_raster_program.hpp"
#include "native_mtoon_visibility.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
namespace evrm {
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
bool supported(const NativeMToonRasterProgramCapabilities& c)noexcept{
    const auto& l=c.limits;
    return c.api_version>=VK_API_VERSION_1_2&&native_rendering_environment_supported(c.api_version,c.enabled)&&
        native_mtoon_fragment_identity_supported(c.enabled)&&l.maxBoundDescriptorSets>=3&&l.maxUniformBufferRange>=752&&
        l.maxDescriptorSetUniformBuffers>=7&&l.maxPerStageDescriptorUniformBuffers>=7&&
        l.maxDescriptorSetStorageBuffers>=10&&l.maxPerStageDescriptorStorageBuffers>=10&&
        l.maxDescriptorSetSampledImages>=15&&l.maxPerStageDescriptorSampledImages>=15&&
        l.maxDescriptorSetSamplers>=15&&l.maxPerStageDescriptorSamplers>=15&&l.maxPerStageResources>=47&&
        l.maxColorAttachments>=6&&l.maxVertexOutputComponents>=32&&l.maxFragmentInputComponents>=32;
}
bool shader_valid(const std::vector<std::uint32_t>& s)noexcept{return s.size()>=5&&s.size()<=262144&&s[0]==0x07230203;}
struct RasterCore {
    VkDevice device{};const bool* parent=nullptr;std::atomic<bool> uncertain{false};
    std::array<VkDescriptorSetLayout,3> sets{};VkPipelineLayout layout{};
    std::array<std::array<VkShaderModule,3>,4> modules{};
    std::array<std::unique_ptr<NativeMToonMainPipelines>,4> main;
    std::array<VkShaderModule,2> forward_modules{};std::unique_ptr<NativeMToonMainPipelines> forward;
    std::shared_ptr<const NativeMToonWorkProgram> work;
    PFN_vkDestroyDescriptorSetLayout destroy_set{};PFN_vkDestroyPipelineLayout destroy_layout{};PFN_vkDestroyShaderModule destroy_module{};
    void quarantine()noexcept{uncertain=true;for(const auto& p:main)if(p)p->quarantine();if(work)work->quarantine();if(forward)forward->quarantine();}
    ~RasterCore(){if(uncertain||(work&&!work->reusable())){quarantine();return;}work.reset();forward.reset();for(auto& p:main)p.reset();
        if(destroy_module)for(auto m:forward_modules)if(m)destroy_module(device,m,nullptr);
        if(destroy_module)for(const auto& family:modules)for(auto m:family)if(m)destroy_module(device,m,nullptr);
        if(layout&&destroy_layout)destroy_layout(device,layout,nullptr);if(destroy_set)for(auto s:sets)if(s)destroy_set(device,s,nullptr);}
};
template<class T>T entry(VkDevice device,PFN_vkGetDeviceProcAddr get,const char* name){const auto f=reinterpret_cast<T>(get(device,name));require(f!=nullptr,"Missing native MToon raster entry");return f;}
VkShaderModule make_module(VkDevice device,PFN_vkCreateShaderModule create,const std::vector<std::uint32_t>& words,VkShaderModule& out){
    VkShaderModuleCreateInfo i{};i.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;i.codeSize=words.size()*4;i.pCode=words.data();
    require(create(device,&i,nullptr,&out)==VK_SUCCESS&&out,"Native MToon raster module");return out;
}
std::shared_ptr<RasterCore> make_core(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonRasterProgramCapabilities& caps,const NativeMToonRasterProgramShaders& shaders,const bool* parent){
    auto c=std::make_shared<RasterCore>();c->device=device;c->parent=parent;
    c->destroy_set=entry<PFN_vkDestroyDescriptorSetLayout>(device,get,"vkDestroyDescriptorSetLayout");c->destroy_layout=entry<PFN_vkDestroyPipelineLayout>(device,get,"vkDestroyPipelineLayout");c->destroy_module=entry<PFN_vkDestroyShaderModule>(device,get,"vkDestroyShaderModule");
    const auto create_set=entry<PFN_vkCreateDescriptorSetLayout>(device,get,"vkCreateDescriptorSetLayout");const auto create_layout=entry<PFN_vkCreatePipelineLayout>(device,get,"vkCreatePipelineLayout");const auto create_module=entry<PFN_vkCreateShaderModule>(device,get,"vkCreateShaderModule");
    constexpr auto stages=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDescriptorSetLayoutBinding owned[]{
        {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,stages,nullptr},{1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,stages,nullptr},{2,VK_DESCRIPTOR_TYPE_SAMPLER,1,stages,nullptr},
        {3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,stages,nullptr},{4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,stages,nullptr},{5,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,stages,nullptr},
        {11,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,stages,nullptr},{12,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,stages,nullptr},
        {13,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,stages,nullptr},{14,VK_DESCRIPTOR_TYPE_SAMPLER,1,stages,nullptr},{15,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,stages,nullptr},{16,VK_DESCRIPTOR_TYPE_SAMPLER,1,stages,nullptr},{17,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,stages,nullptr},{18,VK_DESCRIPTOR_TYPE_SAMPLER,1,stages,nullptr}};
    const VkDescriptorSetLayoutBinding material[]{{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,stages,nullptr},{1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,11,stages,nullptr},{2,VK_DESCRIPTOR_TYPE_SAMPLER,11,stages,nullptr}};
    std::array<VkDescriptorSetLayoutBinding,10> draw{};for(unsigned n=0;n<10;++n)draw[n]={n,(n==0||n==3||n==7)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,stages,nullptr};
    const VkDescriptorSetLayoutBinding* bindings[]{owned,material,draw.data()};const unsigned counts[]{14,3,10};
    for(unsigned n=0;n<3;++n){VkDescriptorSetLayoutCreateInfo i{};i.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;i.bindingCount=counts[n];i.pBindings=bindings[n];require(create_set(device,&i,nullptr,&c->sets[n])==VK_SUCCESS&&c->sets[n],"Native MToon raster set layout");}
    VkPipelineLayoutCreateInfo i{};i.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;i.setLayoutCount=3;i.pSetLayouts=c->sets.data();require(create_layout(device,&i,nullptr,&c->layout)==VK_SUCCESS&&c->layout,"Native MToon raster pipeline layout");
    for(unsigned family=0;family<4;++family){for(unsigned stage=0;stage<3;++stage)make_module(device,create_module,shaders.main[family][stage],c->modules[family][stage]);const auto& m=c->modules[family];c->main[family]=std::make_unique<NativeMToonMainPipelines>(device,get,c->layout,m[0],m[1],m[2],family==0?7u:1u,caps.enabled);}
    if(!shaders.forward[0].empty()&&!shaders.forward[1].empty()){
        for(unsigned n=0;n<2;++n)make_module(device,create_module,shaders.forward[n],c->forward_modules[n]);
        c->forward=std::make_unique<NativeMToonMainPipelines>(device,get,c->layout,c->forward_modules[0],c->forward_modules[1],c->forward_modules[1],7u,caps.enabled,true);
    }
    NativeMToonWorkCapabilities work_caps;work_caps.limits=caps.limits;std::array<NativeMToonWorkShader,4> work;
    for(unsigned n=0;n<4;++n)work[n]={shaders.work[n].data(),shaders.work[n].size()};
    c->work=std::make_shared<NativeMToonWorkProgram>(device,get,work_caps,work,parent);
    return c;
}
}
namespace {
bool shadow_supported(const NativeMToonRasterProgramCapabilities& c)noexcept{
    const auto& l=c.limits;return supported(c)&&l.maxBoundDescriptorSets>=4&&l.maxDescriptorSetUniformBuffers>=8&&l.maxPerStageDescriptorUniformBuffers>=8&&l.maxPerStageResources>=48;
}
struct RasterOutline {
    std::shared_ptr<RasterCore> core;std::array<VkShaderModule,3> modules{};std::unique_ptr<NativeMToonOutlinePipelines> pipelines;std::atomic<bool> uncertain{false};
    void quarantine()noexcept{uncertain=true;if(pipelines)pipelines->quarantine();}
    ~RasterOutline(){if(uncertain||(core&&core->uncertain)){quarantine();return;}pipelines.reset();if(core)for(auto m:modules)if(m)core->destroy_module(core->device,m,nullptr);}
};
std::shared_ptr<RasterOutline> make_outline(VkDevice device,PFN_vkGetDeviceProcAddr get,const std::shared_ptr<RasterCore>& core,const NativeMToonRasterProgramShaders& code){
    auto out=std::make_shared<RasterOutline>();out->core=core;const auto create=entry<PFN_vkCreateShaderModule>(device,get,"vkCreateShaderModule");
    for(unsigned n=0;n<3;++n)make_module(device,create,code.outline[n],out->modules[n]);const auto& m=out->modules;
    out->pipelines=std::make_unique<NativeMToonOutlinePipelines>(device,get,core->layout,m[0],m[1],m[2],7u);return out;
}
struct RasterShadow {
    std::shared_ptr<RasterCore> core;VkDescriptorSetLayout light{};VkPipelineLayout layout{};std::array<VkShaderModule,2> modules{};
    std::unique_ptr<NativeShadowDrawPipeline> pipeline;PFN_vkCmdBindDescriptorSets descriptors{};std::atomic<bool> uncertain{false};
    void quarantine()noexcept{uncertain=true;}
    ~RasterShadow(){
        // The existing generic caster owner has no quarantine pointer. Retain
        // it with the uncertain device exactly as the existing backend does.
        if(uncertain||(core&&core->uncertain)){pipeline.release();return;}
        pipeline.reset();if(!core)return;for(auto m:modules)if(m)core->destroy_module(core->device,m,nullptr);
        if(layout)core->destroy_layout(core->device,layout,nullptr);if(light)core->destroy_set(core->device,light,nullptr);
    }
};
std::shared_ptr<RasterShadow> make_shadow(VkDevice device,PFN_vkGetDeviceProcAddr get,const std::shared_ptr<RasterCore>& core,const NativeMToonRasterProgramShaders& code){
    auto out=std::make_shared<RasterShadow>();out->core=core;out->descriptors=entry<PFN_vkCmdBindDescriptorSets>(device,get,"vkCmdBindDescriptorSets");
    const auto create_set=entry<PFN_vkCreateDescriptorSetLayout>(device,get,"vkCreateDescriptorSetLayout");const auto create_layout=entry<PFN_vkCreatePipelineLayout>(device,get,"vkCreatePipelineLayout");const auto create_module=entry<PFN_vkCreateShaderModule>(device,get,"vkCreateShaderModule");
    // Identical to the existing native caster light DSL. An already retained
    // generic per-light immutable set is compatible without copying/rebinding
    // a mutable native source descriptor into a second owner.
    const VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr};VkDescriptorSetLayoutCreateInfo s{};s.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;s.bindingCount=1;s.pBindings=&binding;
    require(create_set(device,&s,nullptr,&out->light)==VK_SUCCESS&&out->light,"Native MToon caster light layout");
    const std::array<VkDescriptorSetLayout,4> sets{core->sets[0],out->light,core->sets[2],core->sets[1]};VkPipelineLayoutCreateInfo l{};l.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;l.setLayoutCount=4;l.pSetLayouts=sets.data();
    require(create_layout(device,&l,nullptr,&out->layout)==VK_SUCCESS&&out->layout,"Native MToon caster layout");
    for(unsigned n=0;n<2;++n)make_module(device,create_module,code.shadow[n],out->modules[n]);
    out->pipeline=std::make_unique<NativeShadowDrawPipeline>(device,get,out->layout,out->modules[0],out->modules[1]);return out;
}
}
struct NativeMToonRasterProgram::Impl {
    std::shared_ptr<RasterCore> core;std::shared_ptr<RasterOutline> outlines;std::shared_ptr<RasterShadow> shadows;
    std::atomic<bool> uncertain{false};
    void quarantine()noexcept{uncertain=true;if(core)core->quarantine();if(outlines)outlines->quarantine();if(shadows)shadows->quarantine();}
};
NativeMToonRasterProgram::NativeMToonRasterProgram(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonRasterProgramCapabilities& caps,const NativeMToonRasterProgramShaders& code,bool outline,bool shadow,const bool* parent,const std::shared_ptr<const NativeMToonRasterProgram>& seed):impl(std::make_unique<Impl>()){
    require(device&&get&&parent&&supported(caps)&&(!shadow||shadow_supported(caps)),"Unsupported native MToon raster/caster program");
    for(const auto& family:code.main)for(const auto& words:family)require(shader_valid(words),"Invalid native MToon raster shader");
    for(const auto& words:code.work)require(shader_valid(words),"Invalid native MToon frame Work shader");
    if(outline)for(const auto& words:code.outline)require(shader_valid(words),"Invalid native MToon outline shader");
    if(shadow)for(const auto& words:code.shadow)require(shader_valid(words),"Invalid native MToon caster shader");
    if(seed){require(seed->belongs_to(device,parent)&&seed->reusable(),"Stale native MToon raster core");impl->core=seed->impl->core;impl->outlines=seed->impl->outlines;impl->shadows=seed->impl->shadows;}
    else impl->core=make_core(device,get,caps,code,parent);
    if(outline&&!impl->outlines)impl->outlines=make_outline(device,get,impl->core,code);
    if(shadow&&!impl->shadows)impl->shadows=make_shadow(device,get,impl->core,code);
}
NativeMToonRasterProgram::~NativeMToonRasterProgram()=default;
bool NativeMToonRasterProgram::reusable()const noexcept{return impl&&impl->core&&!impl->uncertain&&!impl->core->uncertain&&impl->core->work&&impl->core->work->reusable()&&(!impl->outlines||!impl->outlines->uncertain)&&(!impl->shadows||!impl->shadows->uncertain);}
bool NativeMToonRasterProgram::has_forward()const noexcept{return reusable()&&bool(impl->core->forward);}
bool NativeMToonRasterProgram::record_forward(VkCommandBuffer command,const NativeMToonMainCall* calls,unsigned count,VkPipeline original,VkPipelineLayout layout,unsigned sets,const VkDescriptorSet* descriptors)const noexcept{
    return !count||(has_forward()&&impl->core->forward->record(command,true,calls,count,original,layout,sets,descriptors));
}
bool NativeMToonRasterProgram::has_outlines()const noexcept{return reusable()&&bool(impl->outlines);}
bool NativeMToonRasterProgram::has_shadows()const noexcept{return reusable()&&bool(impl->shadows);}
bool NativeMToonRasterProgram::belongs_to(VkDevice d,const bool* parent)const noexcept{return impl&&impl->core&&impl->core->device==d&&impl->core->parent==parent;}
void NativeMToonRasterProgram::quarantine()const noexcept{if(impl)impl->quarantine();}
std::array<VkDescriptorSetLayout,3> NativeMToonRasterProgram::descriptor_layouts()const noexcept{return reusable()?impl->core->sets:std::array<VkDescriptorSetLayout,3>{};}
VkPipelineLayout NativeMToonRasterProgram::pipeline_layout()const noexcept{return reusable()?impl->core->layout:VK_NULL_HANDLE;}
std::shared_ptr<const NativeMToonWorkProgram> NativeMToonRasterProgram::work_program()const noexcept{return reusable()?impl->core->work:std::shared_ptr<const NativeMToonWorkProgram>{};}
bool NativeMToonRasterProgram::record(VkCommandBuffer command,bool gbuffer,NativeMToonRasterFamily family,const NativeMToonMainCall* calls,unsigned count,VkPipeline original,VkPipelineLayout layout,unsigned sets,const VkDescriptorSet* descriptors)const noexcept{
    const auto f=unsigned(family);return reusable()&&f<4&&impl->core->main[f]->record(command,gbuffer,calls,count,original,layout,sets,descriptors);
}
bool NativeMToonRasterProgram::record_outline(VkCommandBuffer command,bool gbuffer,const NativeMToonOutlineCall* calls,unsigned count,VkPipeline original,VkPipelineLayout layout,unsigned sets,const VkDescriptorSet* descriptors)const noexcept{
    return reusable()&&(!count||(impl->outlines&&impl->outlines->pipelines->record(command,gbuffer,calls,count,original,layout,sets,descriptors)));
}
bool NativeMToonRasterProgram::record_shadow(VkCommandBuffer command,VkDescriptorSet light,const NativeMToonMainCall* calls,unsigned count,VkPipeline original,VkPipelineLayout layout,unsigned sets,const VkDescriptorSet* descriptors)const noexcept{
    if(!has_shadows()||!command||!light||!count||count>4096||!calls||!original||!layout||!sets||sets>32||!descriptors)return false;
    for(unsigned n=0;n<sets;++n)if(!descriptors[n])return false;
    for(unsigned n=0;n<count;++n){const auto& c=calls[n];if(!c.sets[0]||!c.sets[1]||!c.sets[2]||!c.index_count||c.index_count%3||c.first_index>UINT32_MAX-c.index_count)return false;}
    const auto& shadow=*impl->shadows;
    for(unsigned n=0;n<count;++n){const auto& c=calls[n];const std::array<VkDescriptorSet,2> tail{c.sets[2],c.sets[1]};
        shadow.descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,shadow.layout,2,2,tail.data(),0,nullptr);
        const NativeDrawCall draw{c.sets[0],c.first_index,c.index_count,{},{}};
        if(!shadow.pipeline->record(command,light,&draw,1,original,layout,sets,descriptors))return false;
    }return true;
}
struct NativeMToonRasterProgramCache::Impl {
    VkDevice device{};PFN_vkGetDeviceProcAddr get{};NativeMToonRasterProgramCapabilities caps;const bool* parent=nullptr;
    std::shared_ptr<const NativeMToonRasterProgramShaders> shaders;
    std::shared_ptr<const NativeMToonRasterProgram> program;
    std::future<std::shared_ptr<const NativeMToonRasterProgram>> pending;
    bool pending_outlines=false,pending_shadows=false,failed=false,outline_failed=false,shadow_failed=false,stopped=false,uncertain=false;
    void reject()noexcept{if(program){if(pending_outlines&&!program->has_outlines())outline_failed=true;if(pending_shadows&&!program->has_shadows())shadow_failed=true;}else failed=true;}
    void receive()noexcept{try{if(pending.valid()){auto next=pending.get();if(next&&next->reusable())program=std::move(next);else reject();}}catch(...){reject();}if(uncertain&&program)program->quarantine();}
};
NativeMToonRasterProgramCache::NativeMToonRasterProgramCache(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonRasterProgramCapabilities& caps,NativeMToonRasterProgramShaders code,const bool* parent):impl(std::make_unique<Impl>()){
    require(device&&get&&parent,"Invalid native MToon raster cache");impl->device=device;impl->get=get;impl->caps=caps;impl->parent=parent;impl->shaders=std::make_shared<const NativeMToonRasterProgramShaders>(std::move(code));
}
NativeMToonRasterProgramCache::~NativeMToonRasterProgramCache(){shutdown(impl&&impl->parent&&*impl->parent);}
NativeMToonRasterPreparedProgram NativeMToonRasterProgramCache::request(bool outlines,bool shadows)noexcept{
    if(!impl||impl->stopped||!supported(impl->caps)||(shadows&&!shadow_supported(impl->caps)))return {};auto& p=*impl;
    if(*p.parent){p.uncertain=true;if(p.program)p.program->quarantine();return {};}if(p.uncertain)return {};
    try{
        if(p.pending.valid()&&p.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready)p.receive();
        if(p.program&&!p.program->reusable()){p.failed=true;return {NativeMToonRasterProgramStatus::failed,{}};}
        if(p.program&&(!outlines||p.program->has_outlines())&&(!shadows||p.program->has_shadows()))return {NativeMToonRasterProgramStatus::ready,p.program};
        if(p.failed||(outlines&&p.outline_failed)||(shadows&&p.shadow_failed))return {NativeMToonRasterProgramStatus::failed,{}};
        if(!p.pending.valid()){
            p.pending_outlines=outlines;p.pending_shadows=shadows;const auto device=p.device;const auto get=p.get;const auto caps=p.caps;const auto code=p.shaders;const auto parent=p.parent;const auto seed=p.program;
            p.pending=std::async(std::launch::async,[device,get,caps,code,parent,seed,outlines,shadows]()->std::shared_ptr<const NativeMToonRasterProgram>{try{return std::shared_ptr<const NativeMToonRasterProgram>(new NativeMToonRasterProgram(device,get,caps,*code,outlines,shadows,parent,seed));}catch(...){return {};}});
        }
        return {NativeMToonRasterProgramStatus::pending,{}};
    }catch(...){p.reject();return {NativeMToonRasterProgramStatus::failed,{}};}
}
void NativeMToonRasterProgramCache::shutdown(bool uncertain)noexcept{if(!impl)return;auto& p=*impl;p.stopped=true;p.uncertain=p.uncertain||uncertain;p.receive();if(p.uncertain&&p.program)p.program->quarantine();}
}
