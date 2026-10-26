#include "native_oit_window.hpp"
#include "native_state.hpp"
#include <algorithm>
#include <cstring>
namespace evrm { namespace {
constexpr std::array<VkFormat,5> formats={VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R8_UNORM,
    VK_FORMAT_R8_UNORM,VK_FORMAT_R8_UNORM,VK_FORMAT_R16_UNORM};
bool digest(const std::array<std::uint8_t,32>& value,const char* text)noexcept{
    constexpr char hex[]="0123456789abcdef";
    for(std::size_t i=0;i<value.size();++i)if(hex[value[i]>>4]!=text[i*2]||hex[value[i]&15]!=text[i*2+1])return false;
    return true;
}
template<class T>bool simple(const T& v,VkStructureType type)noexcept{return v.sType==type&&!v.pNext&&!v.flags;}
bool view(const NativeOitView& v,VkFormat format,VkExtent2D extent,bool depth)noexcept{
    return v.current&&v.view&&v.image&&v.image_generation&&v.view_generation&&v.format==format&&
        v.type==VK_IMAGE_VIEW_TYPE_2D&&v.samples==VK_SAMPLE_COUNT_1_BIT&&
        v.extent.width==extent.width&&v.extent.height==extent.height&&v.extent.depth==1&&
        (v.usage&(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))&&
        v.subresource.aspectMask==(depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT)&&
        !v.subresource.baseMipLevel&&v.subresource.levelCount==1&&!v.subresource.baseArrayLayer&&v.subresource.layerCount==1;
}
bool attachment(const VkRenderingAttachmentInfo& a,const NativeOitView& v)noexcept{
    return a.sType==VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO&&!a.pNext&&a.imageView==v.view&&
        a.resolveMode==VK_RESOLVE_MODE_NONE&&!a.resolveImageView&&a.resolveImageLayout==VK_IMAGE_LAYOUT_UNDEFINED;
}
}
NativeVolumeProducer native_volume_producer(std::uint32_t bytes,const std::array<std::uint8_t,32>& value)noexcept{
    if(bytes==53948&&digest(value,"a97f245efad907582799aa928148cd6c49ffbf0ec4a7bc4a9cf513f9fc681a05"))return NativeVolumeProducer::populate;
    if(bytes==10048&&digest(value,"79b06fed814a8ac70f3f71300b6f7e2b39c68078ad11d3cf32d166f2bf11d495"))return NativeVolumeProducer::in_scatter;
    if(bytes==4932&&digest(value,"b4792a0e4856a1e613cc14a2d124ca1dc4fd713843a120edafbc9fa2cde0e879"))return NativeVolumeProducer::integrate;
    return NativeVolumeProducer::unsupported;
}
NativeOitShader native_oit_late_shader(const NativePipelineState& p)noexcept{
    const std::array<std::uint8_t,32> zero{};
    if(p.graphics_stage_count!=2||p.tess_control_bytes||p.tess_evaluation_bytes||
       p.tess_control_digest!=zero||p.tess_evaluation_digest!=zero||p.vertex_bytes!=10716||
       !digest(p.vertex_digest,"e0370541469556a65983b0f4148546d83e71099dc9cbba7e906b44022950911a"))return NativeOitShader::unsupported;
    if(p.fragment_bytes==18840&&digest(p.fragment_digest,"e1996d71e963aa0ee92e7804227dfefb56704808c13f4ad264cf5528c9aa84e3"))return NativeOitShader::unlit;
    if(p.fragment_bytes==23940&&digest(p.fragment_digest,"129c9ebb3aa7638c99b14a790692cd0574196b6057b0d46966642a51f29b5c76"))return NativeOitShader::lit_diffuse;
    if(p.fragment_bytes==28748&&digest(p.fragment_digest,"85cbcf7d7b8d17be106d1c24dd7fec019725ce0c99e70784f23d55d5e30e7710"))return NativeOitShader::lit_shaded;
    return NativeOitShader::unsupported;
}
bool native_oit_pipeline_contract(const VkGraphicsPipelineCreateInfo& p)noexcept{
    // Stop at the rendering shell before inspecting fixed state that can be
    // ignored (and unreadable) for legacy, derivative or discard pipelines.
    if(p.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO||p.flags||p.renderPass||p.subpass||
       p.basePipelineHandle||p.pTessellationState||!p.layout||p.stageCount!=2||!p.pStages||!p.pNext)return false;
    const auto& render=*static_cast<const VkPipelineRenderingCreateInfo*>(p.pNext);
    if(render.sType!=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO||render.pNext||render.viewMask||
       render.depthAttachmentFormat!=VK_FORMAT_D32_SFLOAT||render.stencilAttachmentFormat!=VK_FORMAT_UNDEFINED||
       render.colorAttachmentCount!=5||!render.pColorAttachmentFormats||
       !std::equal(formats.begin(),formats.end(),render.pColorAttachmentFormats))return false;
    // Other dynamic declarations can make fixed-state pointers ignored. Reject
    // them before reading any such pointer, including dynamic vertex input.
    if(!p.pDynamicState||!simple(*p.pDynamicState,VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO)||
       p.pDynamicState->dynamicStateCount!=5||!p.pDynamicState->pDynamicStates)return false;
    constexpr std::array<VkDynamicState,5> expected={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK};
    for(auto state:expected)if(std::count(p.pDynamicState->pDynamicStates,p.pDynamicState->pDynamicStates+5,state)!=1)return false;
    unsigned stages=0;
    for(unsigned i=0;i<2;++i){const auto& s=p.pStages[i];
        if(!simple(s,VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO)||!s.module||!s.pName||s.pSpecializationInfo)return false;
        const unsigned bit=s.stage==VK_SHADER_STAGE_VERTEX_BIT?1u:s.stage==VK_SHADER_STAGE_FRAGMENT_BIT?2u:0u;
        if(!bit||(stages&bit)||std::strcmp(s.pName,bit==1?"vs_main":"fs_main"))return false;
        stages|=bit;
    }
    if(stages!=3||!p.pRasterizationState||!simple(*p.pRasterizationState,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO))return false;
    const auto& r=*p.pRasterizationState;
    if(r.rasterizerDiscardEnable||r.depthClampEnable||r.depthBiasEnable||r.polygonMode!=VK_POLYGON_MODE_FILL||
       (r.cullMode&~VK_CULL_MODE_FRONT_AND_BACK)||r.frontFace>VK_FRONT_FACE_CLOCKWISE||r.lineWidth!=1)return false;
    if(!p.pVertexInputState||!simple(*p.pVertexInputState,VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO)||
       p.pVertexInputState->vertexBindingDescriptionCount||p.pVertexInputState->vertexAttributeDescriptionCount||
       !p.pInputAssemblyState||!simple(*p.pInputAssemblyState,VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO)||
       p.pInputAssemblyState->topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST||p.pInputAssemblyState->primitiveRestartEnable||
       !p.pViewportState||!simple(*p.pViewportState,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO)||
       p.pViewportState->viewportCount!=1||p.pViewportState->scissorCount!=1)return false;
    if(!p.pMultisampleState||!simple(*p.pMultisampleState,VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO))return false;
    const auto& m=*p.pMultisampleState;
    if(m.rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT||m.sampleShadingEnable||m.alphaToCoverageEnable||m.alphaToOneEnable||
       (m.pSampleMask&&!(*m.pSampleMask&1)))return false;
    if(!p.pDepthStencilState||!simple(*p.pDepthStencilState,VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO))return false;
    const auto& d=*p.pDepthStencilState;
    if(!d.depthTestEnable||d.depthWriteEnable||d.depthCompareOp!=VK_COMPARE_OP_GREATER||d.stencilTestEnable||d.depthBoundsTestEnable)return false;
    if(!p.pColorBlendState||!simple(*p.pColorBlendState,VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO))return false;
    const auto& b=*p.pColorBlendState;
    if(b.logicOpEnable||b.attachmentCount!=5||!b.pAttachments)return false;
    for(unsigned i=0;i<5;++i){const auto& a=b.pAttachments[i];const auto source=i==4?VK_BLEND_FACTOR_ZERO:VK_BLEND_FACTOR_ONE;
        const auto destination=i==4?VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:VK_BLEND_FACTOR_ONE;
        if(!a.blendEnable||a.colorWriteMask!=15||a.colorBlendOp!=VK_BLEND_OP_ADD||a.alphaBlendOp!=VK_BLEND_OP_ADD||
           a.srcColorBlendFactor!=source||a.srcAlphaBlendFactor!=source||a.dstColorBlendFactor!=destination||a.dstAlphaBlendFactor!=destination)return false;
    }
    return true;
}
bool native_oit_final_pass(const VkRenderingInfo& i,const std::array<NativeOitView,5>& colors,
                          const NativeOitView& depth,const NativeOitView& expected_depth,VkExtent2D extent)noexcept{
    if(i.sType!=VK_STRUCTURE_TYPE_RENDERING_INFO||i.pNext||i.flags||i.viewMask||i.layerCount!=1||
       i.renderArea.offset.x||i.renderArea.offset.y||!extent.width||!extent.height||
       i.renderArea.extent.width!=extent.width||i.renderArea.extent.height!=extent.height||
       i.colorAttachmentCount!=5||!i.pColorAttachments||!i.pDepthAttachment||i.pStencilAttachment)return false;
    if(!view(depth,VK_FORMAT_D32_SFLOAT,extent,true)||!view(expected_depth,VK_FORMAT_D32_SFLOAT,extent,true)||
       depth.view!=expected_depth.view||depth.image!=expected_depth.image||
       depth.image_generation!=expected_depth.image_generation||depth.view_generation!=expected_depth.view_generation)return false;
    const auto& da=*i.pDepthAttachment;
    if(!attachment(da,depth)||da.imageLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL||
       da.loadOp!=VK_ATTACHMENT_LOAD_OP_LOAD||da.storeOp!=VK_ATTACHMENT_STORE_OP_NONE)return false;
    for(unsigned c=0;c<5;++c){const auto& v=colors[c];const auto& a=i.pColorAttachments[c];
        if(!view(v,formats[c],extent,false)||!attachment(a,v)||a.imageLayout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL||
           a.storeOp!=VK_ATTACHMENT_STORE_OP_STORE||a.loadOp!=(c==0||c==4?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_LOAD)||
           v.view==depth.view||v.image==depth.image)return false;
        for(unsigned previous=0;previous<c;++previous)if(v.view==colors[previous].view||v.image==colors[previous].image)return false;
        if(c==0)for(float component:a.clearValue.color.float32)if(component!=0)return false;
        if(c==4&&a.clearValue.color.float32[0]!=1)return false;
    }
    return true;
}
void NativeOitWindow::begin(bool matching)noexcept{
    if(active){failed=true;active=false;lit=false;}
    if(!matching)return;
    if(candidates<2)++candidates;
    if(candidates!=1||insertions)failed=true;
    active=true;lit=false;
}
void NativeOitWindow::observe_lit(bool compatible)noexcept{if(active&&compatible)lit=true;}
bool NativeOitWindow::can_insert()const noexcept{return !failed&&active&&lit&&candidates==1&&insertions==0;}
void NativeOitWindow::end(bool inserted)noexcept{
    if(!active){if(inserted)failed=true;return;}
    if(inserted&&can_insert())++insertions;else failed=true;
    active=false;lit=false;
}
bool NativeOitWindow::complete()const noexcept{return !failed&&!active&&candidates==1&&insertions==1;}
void NativeOitWindow::reset()noexcept{*this={};}
}
