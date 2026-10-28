#define VK_NO_PROTOTYPES
#include "native_draw.hpp"
#include <array>
#include <limits>
#include <stdexcept>
namespace evrm {
struct NativeDrawPipelines::Impl {
    VkDevice device{};
    VkPipelineLayout layout{};
    VkPipeline visibility{},gbuffer{};
    PFN_vkDestroyPipeline destroy{};
    PFN_vkCmdBindPipeline bind{};
    PFN_vkCmdBindDescriptorSets descriptors{};
    PFN_vkCmdDraw draw{};
    ~Impl() {
        if(destroy) {
            if(visibility)destroy(device,visibility,nullptr);
            if(gbuffer)destroy(device,gbuffer,nullptr);
        }
    }
};
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
    const auto create=reinterpret_cast<PFN_vkCreateGraphicsPipelines>(get(device,"vkCreateGraphicsPipelines"));
    if(!create||!impl->destroy||!impl->bind||!impl->descriptors||!impl->draw)throw std::runtime_error("Missing native draw entries");
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    for(auto& stage:stages)stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex_shader;stages[0].pName="vs_native";
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
    VkPipelineVertexInputStateCreateInfo vertex{};vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    // Positive-forward native projection plus the shader's clip-Y flip maps
    // physically front-facing glTF triangles to clockwise raster winding.
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo multisample{};multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;depth.depthTestEnable=VK_TRUE;
    std::array<VkPipelineColorBlendAttachmentState,6> attachments{};
    for(auto& attachment:attachments)attachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.pAttachments=attachments.data();
    const std::array<VkDynamicState,5> states={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK};
    VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=states.size();dynamic.pDynamicStates=states.data();
    const std::array<VkFormat,6> gformats={VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R8G8B8A8_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R16G16_SNORM,VK_FORMAT_R8_UNORM};
    const std::array<VkFormat,2> vformats={VK_FORMAT_R32_UINT,VK_FORMAT_R32G32_UINT};
    VkPipelineRenderingCreateInfo rendering{};rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;rendering.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.pNext=&rendering;
    info.stageCount=stages.size();info.pStages=stages.data();info.pVertexInputState=&vertex;info.pInputAssemblyState=&assembly;
    info.pViewportState=&viewport;info.pRasterizationState=&raster;info.pMultisampleState=&multisample;info.pDepthStencilState=&depth;
    info.pColorBlendState=&blend;info.pDynamicState=&dynamic;info.layout=layout;
    for(unsigned pass=0;pass<2;++pass) {
        stages[1].module=pass?gbuffer_shader:visibility_shader;stages[1].pName=pass?"ps_gbuffer":"ps_visibility";
        depth.depthWriteEnable=pass?VK_FALSE:VK_TRUE;depth.depthCompareOp=pass?VK_COMPARE_OP_EQUAL:VK_COMPARE_OP_GREATER;
        blend.attachmentCount=pass?6:visibility_colors;
        // Native visibility does not write target1. Preserve every component.
        attachments[1].colorWriteMask=pass?15u:0u;
        rendering.colorAttachmentCount=blend.attachmentCount;rendering.pColorAttachmentFormats=pass?gformats.data():vformats.data();
        auto& target=pass?impl->gbuffer:impl->visibility;
        if(create(device,VK_NULL_HANDLE,1,&info,nullptr,&target)!=VK_SUCCESS)throw std::runtime_error("Native draw pipeline creation failed");
    }
}
NativeDrawPipelines::~NativeDrawPipelines()=default;
bool NativeDrawPipelines::record(VkCommandBuffer command,bool gb,const NativeDrawCall* calls,std::uint32_t count,
    VkPipeline original,VkPipelineLayout compatible,std::uint32_t set_count,const VkDescriptorSet* sets)const noexcept {
    if(!command||!impl||!calls||!count||count>4096||!original||!compatible||!sets||!set_count||set_count>32)return false;
    for(std::uint32_t i=0;i<set_count;++i)if(!sets[i])return false;
    for(std::uint32_t i=0;i<count;++i)if(!calls[i].set||!calls[i].index_count||calls[i].index_count%3||
        calls[i].first_index>std::numeric_limits<std::uint32_t>::max()-calls[i].index_count)return false;
    impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,gb?impl->gbuffer:impl->visibility);
    for(std::uint32_t i=0;i<count;++i) {
        impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,impl->layout,0,1,&calls[i].set,0,nullptr);
        impl->draw(command,calls[i].index_count,1,calls[i].first_index,0);
    }
    impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);
    impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,compatible,0,set_count,sets,0,nullptr);
    return true;
}
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
    VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;depth.depthTestEnable=depth.depthWriteEnable=VK_TRUE;depth.depthCompareOp=VK_COMPARE_OP_GREATER;
    VkPipelineColorBlendStateCreateInfo blend{};blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    const std::array<VkDynamicState,5> states{{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK}};
    VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=states.size();dynamic.pDynamicStates=states.data();
    VkPipelineRenderingCreateInfo rendering{};rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;rendering.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.pNext=&rendering;info.stageCount=stages.size();info.pStages=stages.data();info.layout=layout;
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
