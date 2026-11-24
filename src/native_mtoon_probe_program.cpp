#include "native_mtoon_probe_program.hpp"
#include "native_mtoon_probe_source.hpp"
#include <algorithm>
#include <stdexcept>
namespace evrm {namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void checked(VkResult result,const char* why){require(result==VK_SUCCESS,why);}
bool shape(const std::vector<NativeDescriptorDeclaration>& items,bool coefficients)noexcept{
    const unsigned count=coefficients?10u:7u;if(items.size()!=count)return false;std::array<bool,10> seen{};
    for(const auto& d:items){
        const auto type=d.binding==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:coefficients&&d.binding==1?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            coefficients&&d.binding>=3&&d.binding<=5?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if(d.binding>=count||seen[d.binding]||d.type!=type||d.count!=1||d.flags||
           (d.stages!=VK_SHADER_STAGE_COMPUTE_BIT&&(d.binding!=0||d.stages!=mtoon_probe_detail::native_parameter_stages)))return false;
        seen[d.binding]=true;
    }return true;
}
bool same(const std::vector<NativeDescriptorDeclaration>& canonical,const std::vector<NativeDescriptorDeclaration>& other,bool coefficients)noexcept{
    if(!shape(other,coefficients))return false;
    for(const auto& d:other){const auto& a=canonical[d.binding];if(a.binding!=d.binding||a.type!=d.type||a.count!=d.count||a.stages!=d.stages||a.flags!=d.flags)return false;}return true;
}
}
#define PROGRAM_FUNCTIONS(X) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateComputePipelines) X(vkDestroyPipeline)
struct NativeMToonProbeProgram::Impl {
    VkDevice device{};NativeMToonProbeProgramShape shape;const bool* parent=nullptr;
    std::array<VkDescriptorSetLayout,3> sets{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};
#define DECLARE(n) PFN_##n n{};
    PROGRAM_FUNCTIONS(DECLARE)
#undef DECLARE
    ~Impl(){if(parent&&*parent)return;
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);if(shader)vkDestroyShaderModule(device,shader,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);for(auto set:sets)if(set)vkDestroyDescriptorSetLayout(device,set,nullptr);
    }
};
NativeMToonProbeProgram::NativeMToonProbeProgram(VkDevice device,PFN_vkGetDeviceProcAddr get,NativeMToonProbeProgramShape inputs,
    const NativeMToonProbeCopyCapabilities& caps,const std::uint32_t* words,std::size_t count){
    require(device&&get&&words&&count>=5&&count<=1024*1024&&words[0]==0x07230203&&shape(inputs.coefficients,true)&&shape(inputs.classifier,false),"Probe program input/descriptor shape");
    const auto& l=caps.limits;
    require(l.maxBoundDescriptorSets>=3&&l.maxUniformBufferRange>=480&&l.maxPerStageDescriptorUniformBuffers>=5&&l.maxDescriptorSetUniformBuffers>=5&&
        l.maxPerStageDescriptorStorageBuffers>=12&&l.maxDescriptorSetStorageBuffers>=12&&l.maxPerStageDescriptorSampledImages&&l.maxDescriptorSetSampledImages&&
        l.maxPerStageDescriptorStorageImages>=3&&l.maxDescriptorSetStorageImages>=3&&l.maxPerStageResources>=21&&
        l.maxComputeWorkGroupInvocations>=64&&l.maxComputeWorkGroupSize[0]>=64&&(caps.coefficient_format_features&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT),"Probe program limits");
    impl=std::make_unique<Impl>();auto& p=*impl;p.device=device;p.shape=std::move(inputs);
#define LOAD(n) p.n=reinterpret_cast<PFN_##n>(get(device,#n));require(p.n!=nullptr,"Probe program function " #n);
    PROGRAM_FUNCTIONS(LOAD)
#undef LOAD
    const auto sort=[](auto& values){std::sort(values.begin(),values.end(),[](const auto& a,const auto& b){return a.binding<b.binding;});};sort(p.shape.coefficients);sort(p.shape.classifier);
    for(unsigned set=0;set<3;++set){std::vector<VkDescriptorSetLayoutBinding> bindings;
        if(set<2){for(const auto& d:set?p.shape.classifier:p.shape.coefficients)bindings.push_back({d.binding,d.type,d.count,d.stages,nullptr});}
        else bindings={{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;info.bindingCount=static_cast<unsigned>(bindings.size());info.pBindings=bindings.data();
        checked(p.vkCreateDescriptorSetLayout(device,&info,nullptr,&p.sets[set]),"Probe program owned descriptor layout");
    }
    VkPipelineLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=3;layout.pSetLayouts=p.sets.data();
    checked(p.vkCreatePipelineLayout(device,&layout,nullptr,&p.layout),"Probe program layout");
    VkShaderModuleCreateInfo shader{};shader.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;shader.codeSize=count*4;shader.pCode=words;
    checked(p.vkCreateShaderModule(device,&shader,nullptr,&p.shader),"Probe program shader");
    VkComputePipelineCreateInfo pipeline{};pipeline.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline.layout=p.layout;
    pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=p.shader;pipeline.stage.pName="cs_probe_frame";
    checked(p.vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline,nullptr,&p.pipeline),"Probe program pipeline");
}
NativeMToonProbeProgram::~NativeMToonProbeProgram()=default;
std::uint32_t native_mtoon_probe_program_shape_key(const NativeMToonProbeProgramShape& inputs)noexcept{
    if(!shape(inputs.coefficients,true)||!shape(inputs.classifier,false))return UINT32_MAX;
    std::uint32_t key=0;
    for(const auto& d:inputs.coefficients)if(d.binding==0&&d.stages!=VK_SHADER_STAGE_COMPUTE_BIT)key|=1u;
    for(const auto& d:inputs.classifier)if(d.binding==0&&d.stages!=VK_SHADER_STAGE_COMPUTE_BIT)key|=2u;
    return key;
}
bool NativeMToonProbeProgram::matches(const NativeMToonProbeProgramShape& other)const noexcept{return impl&&same(impl->shape.coefficients,other.coefficients,true)&&same(impl->shape.classifier,other.classifier,false);}
VkDevice NativeMToonProbeProgram::device()const noexcept{return impl?impl->device:VkDevice{};}
VkPipeline NativeMToonProbeProgram::pipeline()const noexcept{return impl?impl->pipeline:VkPipeline{};}
VkPipelineLayout NativeMToonProbeProgram::layout()const noexcept{return impl?impl->layout:VkPipelineLayout{};}
VkDescriptorSetLayout NativeMToonProbeProgram::output_layout()const noexcept{return impl?impl->sets[2]:VkDescriptorSetLayout{};}
bool NativeMToonProbeProgram::adopt(const bool* parent)noexcept{
    if(!impl||!parent||*parent||(impl->parent&&impl->parent!=parent))return false;impl->parent=parent;return true;
}
#undef PROGRAM_FUNCTIONS
} // namespace evrm
