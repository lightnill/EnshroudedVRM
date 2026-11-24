#define VK_NO_PROTOTYPES
#include "native_draw.hpp"
#include "native_mtoon_visibility.hpp"
#include "native_mtoon_outline.hpp"
#include "native_mtoon_main.hpp"
#include <atomic>
#include <array>
#include <limits>
#include <stdexcept>
namespace evrm {
struct NativeDrawPipelines::Impl {
    VkDevice device{};
    VkPipelineLayout layout{};
    VkPipeline visibility{},gbuffer{};
    VkPipelineLayout surface_layout{};
    VkPipeline surface_pipeline{};
    std::array<VkPipeline,2> identity_pipeline{};
    std::array<VkPipelineLayout,2> identity_layout{};
    PFN_vkCreateGraphicsPipelines create{};
    PFN_vkDestroyPipeline destroy{};
    PFN_vkCmdBindPipeline bind{};
    PFN_vkCmdBindDescriptorSets descriptors{};
    PFN_vkCmdDraw draw{};
    ~Impl() {
        if(destroy) {
            if(visibility)destroy(device,visibility,nullptr);
            if(gbuffer)destroy(device,gbuffer,nullptr);
            if(surface_pipeline)destroy(device,surface_pipeline,nullptr);
            for(auto pipeline:identity_pipeline)if(pipeline)destroy(device,pipeline,nullptr);
        }
    }
};
namespace {
VkPipeline make_draw_pipeline(VkDevice device,PFN_vkCreateGraphicsPipelines create,PFN_vkDestroyPipeline destroy,
    VkPipelineLayout layout,VkShaderModule vertex_shader,VkShaderModule fragment_shader,bool gbuffer,std::uint32_t visibility_colors,bool outline=false,VkCullModeFlags cull=VK_CULL_MODE_NONE,bool forward_capture=false) {
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    for(auto& stage:stages)stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex_shader;stages[0].pName=outline?"vs_outline":"vs_native";
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
    VkPipelineVertexInputStateCreateInfo vertex{};vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    // The reflected avatar basis and native camera projection, followed by
    // clip-Y conversion, map physical front surfaces to counterclockwise winding.
    // Kept in agreement with OIT and the independent actor-orientation GPU proof.
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=cull;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
    // MToon's outline polygon offset separates coplanar hair cards from
    // their base surface. Native depth is reversed, so away from the camera
    // is negative. Keep identical bias in visibility and the EQUAL GBuffer
    // pass; ordinary geometry and transparent capture retain zero bias.
    if(outline){raster.depthBiasEnable=VK_TRUE;raster.depthBiasConstantFactor=-1.f;raster.depthBiasSlopeFactor=-1.f;}
    VkPipelineMultisampleStateCreateInfo multisample{};multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;depth.depthTestEnable=VK_TRUE;
    std::array<VkPipelineColorBlendAttachmentState,6> attachments{};
    for(auto& attachment:attachments)attachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.pAttachments=attachments.data();
    const std::array<VkDynamicState,5> states={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK};
    VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=static_cast<std::uint32_t>(states.size());dynamic.pDynamicStates=states.data();
    const std::array<VkFormat,6> gformats={VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R8G8B8A8_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R16G16_SNORM,VK_FORMAT_R8_UNORM};
    const std::array<VkFormat,2> vformats={VK_FORMAT_R32_UINT,VK_FORMAT_R32G32_UINT};
    VkPipelineRenderingCreateInfo rendering{};rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;rendering.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.pNext=&rendering;
    info.stageCount=static_cast<std::uint32_t>(stages.size());info.pStages=stages.data();info.pVertexInputState=&vertex;info.pInputAssemblyState=&assembly;
    info.pViewportState=&viewport;info.pRasterizationState=&raster;info.pMultisampleState=&multisample;info.pDepthStencilState=&depth;
    info.pColorBlendState=&blend;info.pDynamicState=&dynamic;info.layout=layout;
    stages[1].module=fragment_shader;stages[1].pName=forward_capture?"ps_forward_capture":gbuffer?(outline?"ps_outline_gbuffer":"ps_gbuffer"):(outline?"ps_outline_visibility":"ps_visibility");
    depth.depthWriteEnable=gbuffer?VK_FALSE:VK_TRUE;depth.depthCompareOp=gbuffer&&!forward_capture?VK_COMPARE_OP_EQUAL:VK_COMPARE_OP_GREATER;
    blend.attachmentCount=gbuffer?6:visibility_colors;
    // Native visibility does not write target1. Preserve every component.
    attachments[1].colorWriteMask=gbuffer?15u:0u;
    if(forward_capture)for(auto& attachment:attachments)attachment.colorWriteMask=0;
    rendering.colorAttachmentCount=blend.attachmentCount;rendering.pColorAttachmentFormats=gbuffer?gformats.data():vformats.data();
    VkPipeline target{};
    if(create(device,VK_NULL_HANDLE,1,&info,nullptr,&target)!=VK_SUCCESS||!target) {
        if(target)destroy(device,target,nullptr);
        throw std::runtime_error("Native draw pipeline creation failed");
    }
    return target;
}
}
NativeDrawPipelines::NativeDrawPipelines(VkDevice device,PFN_vkGetDeviceProcAddr get,VkPipelineLayout layout,
    VkShaderModule vertex_shader,VkShaderModule visibility_shader,VkShaderModule gbuffer_shader,std::uint32_t visibility_colors)
    :impl(std::make_unique<Impl>()) {
    if(!device||!get||!layout||!vertex_shader||!visibility_shader||!gbuffer_shader||
       (visibility_colors!=1&&visibility_colors!=2))throw std::runtime_error("Invalid native draw pipeline inputs");
    impl->device=device;impl->layout=layout;
    impl->destroy=reinterpret_cast<PFN_vkDestroyPipeline>(get(device,"vkDestroyPipeline"));
    impl->bind=reinterpret_cast<PFN_vkCmdBindPipeline>(get(device,"vkCmdBindPipeline"));
    impl->descriptors=reinterpret_cast<PFN_vkCmdBindDescriptorSets>(get(device,"vkCmdBindDescriptorSets"));
    impl->draw=reinterpret_cast<PFN_vkCmdDraw>(get(device,"vkCmdDraw"));
    impl->create=reinterpret_cast<PFN_vkCreateGraphicsPipelines>(get(device,"vkCreateGraphicsPipelines"));
    if(!impl->create||!impl->destroy||!impl->bind||!impl->descriptors||!impl->draw)throw std::runtime_error("Missing native draw entries");
    impl->visibility=make_draw_pipeline(device,impl->create,impl->destroy,layout,vertex_shader,visibility_shader,false,visibility_colors);
    impl->gbuffer=make_draw_pipeline(device,impl->create,impl->destroy,layout,vertex_shader,gbuffer_shader,true,visibility_colors);
}
NativeDrawPipelines::~NativeDrawPipelines()=default;
bool NativeDrawPipelines::install_surface_variant(VkPipelineLayout layout,VkShaderModule vertex,VkShaderModule fragment) noexcept {
    if(!impl||impl->surface_pipeline||!layout||!vertex||!fragment)return false;
    try {
        const auto pipeline=make_draw_pipeline(impl->device,impl->create,impl->destroy,layout,vertex,fragment,true,2);
        impl->surface_pipeline=pipeline;impl->surface_layout=layout;
        return true;
    }catch(...){return false;}
}
bool NativeDrawPipelines::surface_ready()const noexcept {return impl&&impl->surface_pipeline&&impl->surface_layout;}
bool NativeDrawPipelines::install_identity_variant(bool surface,VkPipelineLayout layout,VkShaderModule vertex,VkShaderModule fragment,
    const DeviceFeaturesObservation& features) noexcept {
    const unsigned i=surface?1:0;
    if(!impl||impl->identity_pipeline[i]||!layout||!vertex||!fragment||!native_mtoon_fragment_identity_supported(features))return false;
    try{const auto pipeline=make_draw_pipeline(impl->device,impl->create,impl->destroy,layout,vertex,fragment,true,2);
        impl->identity_pipeline[i]=pipeline;impl->identity_layout[i]=layout;return true;
    }catch(...){return false;}
}
bool NativeDrawPipelines::identity_ready(bool surface)const noexcept{
    const unsigned i=surface?1:0;return impl&&impl->identity_pipeline[i]&&impl->identity_layout[i];
}
bool NativeDrawPipelines::record(VkCommandBuffer command,bool gb,const NativeDrawCall* calls,std::uint32_t count,
    VkPipeline original,VkPipelineLayout compatible,std::uint32_t set_count,const VkDescriptorSet* sets)const noexcept {
    if(!command||!impl||!calls||!count||count>4096||!original||!compatible||!sets||!set_count||set_count>32)return false;
    for(std::uint32_t i=0;i<set_count;++i)if(!sets[i])return false;
    const bool identity=gb&&bool(calls[0].identity_set);
    for(std::uint32_t i=0;i<count;++i)if(!calls[i].set||!calls[i].index_count||calls[i].index_count%3||
        calls[i].first_index>std::numeric_limits<std::uint32_t>::max()-calls[i].index_count||
        (gb&&bool(calls[i].identity_set)!=identity)||
        (identity&&!identity_ready(bool(calls[i].surface_set)))||
        (gb&&!identity&&calls[i].surface_set&&!surface_ready()))return false;
    VkPipeline bound{};
    for(std::uint32_t i=0;i<count;++i) {
        const bool enhanced=gb&&calls[i].surface_set;
        const auto pipeline=identity?impl->identity_pipeline[enhanced?1:0]:enhanced?impl->surface_pipeline:(gb?impl->gbuffer:impl->visibility);
        if(pipeline!=bound){impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);bound=pipeline;}
        const auto layout=identity?impl->identity_layout[enhanced?1:0]:enhanced?impl->surface_layout:impl->layout;
        const auto set=enhanced?calls[i].surface_set:calls[i].set;
        impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);
        if(identity)impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,2,1,&calls[i].identity_set,0,nullptr);
        impl->draw(command,calls[i].index_count,1,calls[i].first_index,0);
    }
    impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);
    impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,compatible,0,set_count,sets,0,nullptr);
    return true;
}
struct NativeMToonMainPipelines::Impl {
    std::atomic<bool> uncertain{false};VkDevice device{};VkPipelineLayout layout{};std::array<std::array<VkPipeline,2>,3> pipelines{};
    PFN_vkDestroyPipeline destroy{};PFN_vkCmdBindPipeline bind{};PFN_vkCmdBindDescriptorSets descriptors{};PFN_vkCmdDraw draw{};
    ~Impl(){if(uncertain)return;if(destroy)for(const auto& pair:pipelines)for(auto p:pair)if(p)destroy(device,p,nullptr);}
};
NativeMToonMainPipelines::NativeMToonMainPipelines(VkDevice device,PFN_vkGetDeviceProcAddr get,VkPipelineLayout layout,VkShaderModule vertex,VkShaderModule visibility,VkShaderModule gbuffer,std::uint32_t modes,const DeviceFeaturesObservation& features,bool forward_capture):impl(std::make_unique<Impl>()){
    if(modes&~7u)throw std::runtime_error("Invalid native MToon main culling");if(!modes)return;
    if(!native_mtoon_fragment_identity_supported(features)||!device||!get||!layout||!vertex||!visibility||!gbuffer)throw std::runtime_error("Invalid native MToon main inputs");
    auto& p=*impl;p.device=device;p.layout=layout;
    p.destroy=reinterpret_cast<PFN_vkDestroyPipeline>(get(device,"vkDestroyPipeline"));p.bind=reinterpret_cast<PFN_vkCmdBindPipeline>(get(device,"vkCmdBindPipeline"));p.descriptors=reinterpret_cast<PFN_vkCmdBindDescriptorSets>(get(device,"vkCmdBindDescriptorSets"));p.draw=reinterpret_cast<PFN_vkCmdDraw>(get(device,"vkCmdDraw"));const auto create=reinterpret_cast<PFN_vkCreateGraphicsPipelines>(get(device,"vkCreateGraphicsPipelines"));
    if(!p.destroy||!p.bind||!p.descriptors||!p.draw||!create)throw std::runtime_error("Missing native MToon main entries");
    for(unsigned cull=0;cull<3;++cull)if(modes&(1u<<cull)){
        if(!forward_capture)p.pipelines[cull][0]=make_draw_pipeline(device,create,p.destroy,layout,vertex,visibility,false,2,false,VkCullModeFlags(cull));
        p.pipelines[cull][1]=make_draw_pipeline(device,create,p.destroy,layout,vertex,gbuffer,true,2,false,VkCullModeFlags(cull),forward_capture);}
}
NativeMToonMainPipelines::~NativeMToonMainPipelines()=default;
bool NativeMToonMainPipelines::record(VkCommandBuffer command,bool gbuffer,const NativeMToonMainCall* calls,std::uint32_t count,VkPipeline original,VkPipelineLayout compatible,std::uint32_t set_count,const VkDescriptorSet* sets)const noexcept{
    if(!count)return true;
    if(!impl||impl->uncertain||!command||!calls||count>4096||!original||!compatible||!set_count||set_count>32||!sets)return false;
    for(unsigned i=0;i<set_count;++i)if(!sets[i])return false;
    for(unsigned i=0;i<count;++i){const auto& c=calls[i];if(c.cull>2||!impl->pipelines[c.cull][gbuffer?1:0]||!c.index_count||c.index_count%3||c.first_index>UINT32_MAX-c.index_count)return false;for(auto s:c.sets)if(!s)return false;}
    VkPipeline bound{};for(unsigned i=0;i<count;++i){const auto& c=calls[i];const auto pipeline=impl->pipelines[c.cull][gbuffer?1:0];if(bound!=pipeline){impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);bound=pipeline;}impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,impl->layout,0,3,c.sets.data(),0,nullptr);impl->draw(command,c.index_count,1,c.first_index,0);}
    impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,compatible,0,set_count,sets,0,nullptr);return true;
}
void NativeMToonMainPipelines::quarantine()const noexcept{if(impl)impl->uncertain=true;}
struct NativeMToonOutlinePipelines::Impl {
    std::atomic<bool> uncertain{false};VkDevice device{};VkPipelineLayout layout{};std::array<std::array<VkPipeline,2>,3> pipelines{};
    PFN_vkDestroyPipeline destroy{};PFN_vkCmdBindPipeline bind{};PFN_vkCmdBindDescriptorSets descriptors{};PFN_vkCmdDraw draw{};
    ~Impl(){if(!uncertain&&destroy)for(const auto& pair:pipelines)for(auto p:pair)if(p)destroy(device,p,nullptr);}
};
NativeMToonOutlinePipelines::NativeMToonOutlinePipelines(VkDevice device,PFN_vkGetDeviceProcAddr get,VkPipelineLayout layout,VkShaderModule vertex,VkShaderModule visibility,VkShaderModule gbuffer,std::uint32_t modes):impl(std::make_unique<Impl>()){
    if(modes&~7u)throw std::runtime_error("Invalid native outline culling");if(!modes)return;
    if(!device||!get||!layout||!vertex||!visibility||!gbuffer)throw std::runtime_error("Invalid native outline inputs");
    auto& p=*impl;p.device=device;p.layout=layout;
    p.destroy=reinterpret_cast<PFN_vkDestroyPipeline>(get(device,"vkDestroyPipeline"));p.bind=reinterpret_cast<PFN_vkCmdBindPipeline>(get(device,"vkCmdBindPipeline"));p.descriptors=reinterpret_cast<PFN_vkCmdBindDescriptorSets>(get(device,"vkCmdBindDescriptorSets"));p.draw=reinterpret_cast<PFN_vkCmdDraw>(get(device,"vkCmdDraw"));const auto create=reinterpret_cast<PFN_vkCreateGraphicsPipelines>(get(device,"vkCreateGraphicsPipelines"));
    if(!p.destroy||!p.bind||!p.descriptors||!p.draw||!create)throw std::runtime_error("Missing native outline entries");
    for(unsigned cull=0;cull<3;++cull)if(modes&(1u<<cull)){
        p.pipelines[cull][0]=make_draw_pipeline(device,create,p.destroy,layout,vertex,visibility,false,2,true,VkCullModeFlags(cull));
        p.pipelines[cull][1]=make_draw_pipeline(device,create,p.destroy,layout,vertex,gbuffer,true,2,true,VkCullModeFlags(cull));}
}
NativeMToonOutlinePipelines::~NativeMToonOutlinePipelines()=default;
bool NativeMToonOutlinePipelines::record(VkCommandBuffer command,bool gbuffer,const NativeMToonOutlineCall* calls,std::uint32_t count,VkPipeline original,VkPipelineLayout compatible,std::uint32_t set_count,const VkDescriptorSet* sets)const noexcept{
    if(!count)return true;
    if(!impl||impl->uncertain||!command||!calls||count>4096||!original||!compatible||!set_count||set_count>32||!sets)return false;
    for(unsigned i=0;i<set_count;++i)if(!sets[i])return false;
    for(unsigned i=0;i<count;++i){const auto& c=calls[i];if(c.cull>2||!impl->pipelines[c.cull][gbuffer?1:0]||!c.index_count||c.index_count%3||c.first_index>UINT32_MAX-c.index_count)return false;for(auto s:c.sets)if(!s)return false;}
    VkPipeline bound{};for(unsigned i=0;i<count;++i){const auto& c=calls[i];const auto pipeline=impl->pipelines[c.cull][gbuffer?1:0];if(bound!=pipeline){impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);bound=pipeline;}impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,impl->layout,0,3,c.sets.data(),0,nullptr);impl->draw(command,c.index_count,1,c.first_index,0);}
    impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,compatible,0,set_count,sets,0,nullptr);return true;
}
void NativeMToonOutlinePipelines::quarantine()const noexcept{if(impl)impl->uncertain=true;}
struct NativeShadowDrawPipeline::Impl {
    VkDevice device{};VkPipelineLayout layout{};VkPipeline pipeline{};
    PFN_vkDestroyPipeline destroy{};PFN_vkCmdBindPipeline bind{};
    PFN_vkCmdBindDescriptorSets descriptors{};PFN_vkCmdDraw draw{};
    ~Impl(){if(pipeline&&destroy)destroy(device,pipeline,nullptr);}
};
NativeShadowDrawPipeline::NativeShadowDrawPipeline(VkDevice device,PFN_vkGetDeviceProcAddr get,VkPipelineLayout layout,VkShaderModule vertex,VkShaderModule fragment):impl(std::make_unique<Impl>()){
    if(!device||!get||!layout||!vertex||!fragment)throw std::runtime_error("Invalid native shadow draw inputs");
    impl->device=device;impl->layout=layout;
    impl->destroy=reinterpret_cast<PFN_vkDestroyPipeline>(get(device,"vkDestroyPipeline"));
    impl->bind=reinterpret_cast<PFN_vkCmdBindPipeline>(get(device,"vkCmdBindPipeline"));
    impl->descriptors=reinterpret_cast<PFN_vkCmdBindDescriptorSets>(get(device,"vkCmdBindDescriptorSets"));
    impl->draw=reinterpret_cast<PFN_vkCmdDraw>(get(device,"vkCmdDraw"));
    const auto create=reinterpret_cast<PFN_vkCreateGraphicsPipelines>(get(device,"vkCreateGraphicsPipelines"));
    if(!create||!impl->destroy||!impl->bind||!impl->descriptors||!impl->draw)throw std::runtime_error("Missing native shadow draw entries");
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};for(auto& s:stages)s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="vs_shadow";
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;stages[1].pName="ps_shadow";
    VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;depth.depthTestEnable=depth.depthWriteEnable=VK_TRUE;depth.depthCompareOp=VK_COMPARE_OP_GREATER;
    VkPipelineColorBlendStateCreateInfo blend{};blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    const std::array<VkDynamicState,5> states{{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK}};
    VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=static_cast<std::uint32_t>(states.size());dynamic.pDynamicStates=states.data();
    VkPipelineRenderingCreateInfo rendering{};rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;rendering.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.pNext=&rendering;info.stageCount=static_cast<std::uint32_t>(stages.size());info.pStages=stages.data();info.layout=layout;
    info.pVertexInputState=&vi;info.pInputAssemblyState=&ia;info.pViewportState=&vp;info.pRasterizationState=&raster;info.pMultisampleState=&ms;info.pDepthStencilState=&depth;info.pColorBlendState=&blend;info.pDynamicState=&dynamic;
    if(create(device,VK_NULL_HANDLE,1,&info,nullptr,&impl->pipeline)!=VK_SUCCESS||!impl->pipeline)throw std::runtime_error("Native shadow draw pipeline creation failed");
}
NativeShadowDrawPipeline::~NativeShadowDrawPipeline()=default;
bool NativeShadowDrawPipeline::record(VkCommandBuffer command,VkDescriptorSet view,const NativeDrawCall* calls,std::uint32_t count,VkPipeline original,VkPipelineLayout compatible,std::uint32_t set_count,const VkDescriptorSet* sets)const noexcept{
    if(!command||!impl||!impl->pipeline||!view||!calls||!count||count>4096||!original||!compatible||!sets||!set_count||set_count>32)return false;
    for(std::uint32_t i=0;i<set_count;++i)if(!sets[i])return false;
    for(std::uint32_t i=0;i<count;++i)if(!calls[i].set||!calls[i].index_count||calls[i].index_count%3||calls[i].first_index>std::numeric_limits<std::uint32_t>::max()-calls[i].index_count)return false;
    impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,impl->pipeline);
    impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,impl->layout,1,1,&view,0,nullptr);
    for(std::uint32_t i=0;i<count;++i){impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,impl->layout,0,1,&calls[i].set,0,nullptr);impl->draw(command,calls[i].index_count,1,calls[i].first_index,0);}
    impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);
    impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,compatible,0,set_count,sets,0,nullptr);
    return true;
}
}
