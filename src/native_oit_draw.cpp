#define VK_NO_PROTOTYPES
#include "native_oit_draw.hpp"
#include <array>
#include <limits>
#include <stdexcept>
namespace evrm {
struct NativeOitDrawPipelines::Impl {
    VkDevice device{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    PFN_vkDestroyPipeline destroy{};
    PFN_vkCmdBindPipeline bind{};
    PFN_vkCmdBindDescriptorSets descriptors{};
    PFN_vkCmdDraw draw{};
    ~Impl() {
        if(destroy) {
            if(pipeline)destroy(device,pipeline,nullptr);
        }
    }
};
NativeOitDrawPipelines::NativeOitDrawPipelines(VkDevice device,PFN_vkGetDeviceProcAddr get,VkPipelineLayout layout,
    VkShaderModule vertex_shader,VkShaderModule fragment_shader,bool independent_blend_enabled)
    :impl(std::make_unique<Impl>()) {
    if(!device||!get||!layout||!vertex_shader||!fragment_shader||!independent_blend_enabled)throw std::runtime_error("Invalid native OIT inputs or independentBlend not enabled");
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
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment_shader;stages[1].pName="ps_oit";
    VkPipelineVertexInputStateCreateInfo vertex{};vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo multisample{};multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;depth.depthTestEnable=VK_TRUE;
    depth.depthWriteEnable=VK_FALSE;depth.depthCompareOp=VK_COMPARE_OP_GREATER;
    std::array<VkPipelineColorBlendAttachmentState,5> attachments{};
    for(unsigned i:{0u,4u}){auto& a=attachments[i];a.colorWriteMask=i?VkColorComponentFlags(VK_COLOR_COMPONENT_R_BIT):15u;a.blendEnable=VK_TRUE;
        a.srcColorBlendFactor=a.srcAlphaBlendFactor=i?VK_BLEND_FACTOR_ZERO:VK_BLEND_FACTOR_ONE;
        a.dstColorBlendFactor=a.dstAlphaBlendFactor=i?VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:VK_BLEND_FACTOR_ONE;}
    VkPipelineColorBlendStateCreateInfo blend{};blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=attachments.size();blend.pAttachments=attachments.data();
    const std::array<VkDynamicState,5> states={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK};
    VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=states.size();dynamic.pDynamicStates=states.data();
    const std::array<VkFormat,5> formats={VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R8_UNORM,VK_FORMAT_R8_UNORM,VK_FORMAT_R8_UNORM,VK_FORMAT_R16_UNORM};
    VkPipelineRenderingCreateInfo rendering{};rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;rendering.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;
    rendering.colorAttachmentCount=formats.size();rendering.pColorAttachmentFormats=formats.data();
    VkGraphicsPipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.pNext=&rendering;
    info.stageCount=stages.size();info.pStages=stages.data();info.pVertexInputState=&vertex;info.pInputAssemblyState=&assembly;
    info.pViewportState=&viewport;info.pRasterizationState=&raster;info.pMultisampleState=&multisample;info.pDepthStencilState=&depth;
    info.pColorBlendState=&blend;info.pDynamicState=&dynamic;info.layout=layout;
    if(create(device,VK_NULL_HANDLE,1,&info,nullptr,&impl->pipeline)!=VK_SUCCESS)throw std::runtime_error("Native OIT pipeline creation failed");
}
NativeOitDrawPipelines::~NativeOitDrawPipelines()=default;
bool NativeOitDrawPipelines::record(VkCommandBuffer command,const NativeDrawCall* calls,std::uint32_t count,
    VkPipeline original,VkPipelineLayout compatible,std::uint32_t set_count,const VkDescriptorSet* sets)const noexcept {
    if(!command||!impl||!calls||!count||count>4096||!original||!compatible||!sets||!set_count||set_count>32)return false;
    for(std::uint32_t i=0;i<set_count;++i)if(!sets[i])return false;
    for(std::uint32_t i=0;i<count;++i)if(!calls[i].set||!calls[i].index_count||calls[i].index_count%3||
        calls[i].first_index>std::numeric_limits<std::uint32_t>::max()-calls[i].index_count)return false;
    impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,impl->pipeline);
    for(std::uint32_t i=0;i<count;++i) {
        impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,impl->layout,0,1,&calls[i].set,0,nullptr);
        impl->draw(command,calls[i].index_count,1,calls[i].first_index,0);
    }
    impl->bind(command,VK_PIPELINE_BIND_POINT_GRAPHICS,original);
    impl->descriptors(command,VK_PIPELINE_BIND_POINT_GRAPHICS,compatible,0,set_count,sets,0,nullptr);
    return true;
}
}
