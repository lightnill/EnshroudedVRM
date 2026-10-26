#include "native_state.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <sstream>
#include <utility>

namespace evrm {
bool NativeRasterState::matches(VkExtent2D extent)const noexcept{
    return viewport_known&&scissor_known&&extent.width&&extent.height&&extent.width<=16384&&extent.height<=16384&&
        std::isfinite(viewport.x)&&std::isfinite(viewport.y)&&std::isfinite(viewport.width)&&std::isfinite(viewport.height)&&
        std::isfinite(viewport.minDepth)&&std::isfinite(viewport.maxDepth)&&viewport.x==0&&viewport.y==0&&
        viewport.width==static_cast<float>(extent.width)&&viewport.height==static_cast<float>(extent.height)&&
        viewport.minDepth==0&&viewport.maxDepth==1&&scissor.offset.x==0&&scissor.offset.y==0&&
        scissor.extent.width==extent.width&&scissor.extent.height==extent.height;
}
bool native_state_command_unobserved(const char* name) noexcept {
    if(!name)return true;
    if(!std::strcmp(name,"vkCmdBindPipeline")||!std::strcmp(name,"vkCmdBindDescriptorSets"))return false;
    for(const auto* prefix:{"vkCmdBindDescriptor","vkCmdPushDescriptor","vkCmdSetDescriptor","vkCmdBindShaders","vkCmdBindPipeline","vkCmdSetDeviceMask","vkCmdExecuteGeneratedCommands","vkCmdPreprocessGeneratedCommands"})
        if(!std::strncmp(name,prefix,std::strlen(prefix)))return true;
    return false;
}
namespace {
constexpr std::size_t cap=4096,opaque_cap=1024*1024,command_cap=512,binding_cap=256,template_cap=16*1024*1024;
// Native 3D lighting views can outlive a stream of unrelated world volumes.
// Preserve their lifetime records up to the image-backing census bound; cache
// eviction is not a resource destruction event. Exhaustion still fails closed.
constexpr std::size_t volume_lifetime_cap=65536;
constexpr std::array<VkFormat,2> vis_formats{VkFormat(98),VkFormat(101)};
constexpr std::array<VkFormat,6> gbuf_formats{VkFormat(43),VkFormat(64),VkFormat(41),VkFormat(98),VkFormat(78),VkFormat(9)};
bool buffer_type(VkDescriptorType t){return t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;}
bool descriptor_type(VkDescriptorType t){return (t>=VK_DESCRIPTOR_TYPE_SAMPLER && t<=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) ||
    t==VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR || t==VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;}
std::size_t descriptor_size(VkDescriptorType t){
    if(buffer_type(t) || t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || t==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)return sizeof(VkDescriptorBufferInfo);
    if(t==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || t==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)return sizeof(VkBufferView);
    if(t==VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)return sizeof(VkAccelerationStructureKHR);
    if(t==VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV)return sizeof(VkAccelerationStructureNV);
    return sizeof(VkDescriptorImageInfo);
}
bool point(VkPipelineBindPoint p){return p==VK_PIPELINE_BIND_POINT_GRAPHICS || p==VK_PIPELINE_BIND_POINT_COMPUTE;}
bool sampled_layout(VkImageLayout l){return l==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||l==VK_IMAGE_LAYOUT_GENERAL;}
bool sampled_color_format(VkFormat f){return f!=VK_FORMAT_UNDEFINED&&f!=VK_FORMAT_D16_UNORM&&f!=VK_FORMAT_X8_D24_UNORM_PACK32&&
    f!=VK_FORMAT_D32_SFLOAT&&f!=VK_FORMAT_S8_UINT&&f!=VK_FORMAT_D16_UNORM_S8_UINT&&f!=VK_FORMAT_D24_UNORM_S8_UINT&&f!=VK_FORMAT_D32_SFLOAT_S8_UINT;}
bool same_view(const NativeSampledImageView& a,const NativeSampledImageView& b){
    return a.view==b.view&&a.image==b.image&&a.image_type==b.image_type&&a.samples==b.samples&&a.view_type==b.view_type&&a.format==b.format&&
        a.extent.width==b.extent.width&&a.extent.height==b.extent.height&&a.extent.depth==b.extent.depth&&a.usage==b.usage&&
        a.image_generation==b.image_generation&&a.view_generation==b.view_generation&&a.subresource.aspectMask==b.subresource.aspectMask&&
        a.subresource.baseMipLevel==b.subresource.baseMipLevel&&a.subresource.levelCount==b.subresource.levelCount&&
        a.subresource.baseArrayLayer==b.subresource.baseArrayLayer&&a.subresource.layerCount==b.subresource.layerCount&&
        a.image_flags==b.image_flags&&a.image_usage==b.image_usage&&a.image_format_count==b.image_format_count&&a.image_formats==b.image_formats&&
        a.image_format==b.image_format&&a.mip_levels==b.mip_levels&&a.array_layers==b.array_layers;
}
std::size_t pi(VkPipelineBindPoint p){return p==VK_PIPELINE_BIND_POINT_COMPUTE?1:0;}
template<class T>bool simple(const T& v,VkStructureType type){return v.sType==type && !v.pNext && !v.flags;}
std::array<bool,2> inherited_raster_declaration(const VkGraphicsPipelineCreateInfo& p){
    // Binding lifetime depends on the dynamic declaration, not on whether the
    // observer recognizes a shader/layout. Do not inspect ignored fixed state.
    if(p.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO||p.flags||!p.stageCount||p.stageCount>32)return {};
    if(p.pNext){const auto& rendering=*static_cast<const VkPipelineRenderingCreateInfo*>(p.pNext);
        if(rendering.sType!=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO||rendering.pNext)return {};}
    if(!p.pDynamicState||!simple(*p.pDynamicState,VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO)||
       p.pDynamicState->dynamicStateCount>5||(p.pDynamicState->dynamicStateCount&&!p.pDynamicState->pDynamicStates))return {};
    std::array<bool,5> seen{};
    for(std::uint32_t i=0;i<p.pDynamicState->dynamicStateCount;++i){unsigned index=5;
        switch(p.pDynamicState->pDynamicStates[i]){
            case VK_DYNAMIC_STATE_VIEWPORT:index=0;break;case VK_DYNAMIC_STATE_SCISSOR:index=1;break;
            case VK_DYNAMIC_STATE_STENCIL_REFERENCE:index=2;break;case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:index=3;break;
            case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:index=4;break;default:return {};}
        if(seen[index])return {};
        seen[index]=true;
    }
    return {seen[0],seen[1]};
}
std::array<bool,2> raster_dynamic(const VkGraphicsPipelineCreateInfo& p){
    // Preserve scalar evidence only across an ordinary, fully observed dynamic
    // declaration. Unknown states can make the viewport structure ignored.
    if(p.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO||p.flags||(p.stageCount!=1&&p.stageCount!=2)||p.pTessellationState)return {};
    if(p.pNext){const auto& rendering=*static_cast<const VkPipelineRenderingCreateInfo*>(p.pNext);
        if(rendering.sType!=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO||rendering.pNext)return {};
    }
    if(!p.pDynamicState||!simple(*p.pDynamicState,VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO)||
       p.pDynamicState->dynamicStateCount>5||
       (p.pDynamicState->dynamicStateCount&&!p.pDynamicState->pDynamicStates))return {};
    std::array<bool,5> seen{};
    for(std::uint32_t i=0;i<p.pDynamicState->dynamicStateCount;++i){unsigned index=5;
        switch(p.pDynamicState->pDynamicStates[i]){
            case VK_DYNAMIC_STATE_VIEWPORT:index=0;break;
            case VK_DYNAMIC_STATE_SCISSOR:index=1;break;
            case VK_DYNAMIC_STATE_STENCIL_REFERENCE:index=2;break;
            case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:index=3;break;
            case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:index=4;break;
            default:return {};
        }
        if(seen[index])return {};
        seen[index]=true;
    }
    if(!p.pRasterizationState||!simple(*p.pRasterizationState,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO)||
       p.pRasterizationState->rasterizerDiscardEnable||!p.pViewportState||
       !simple(*p.pViewportState,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO))return {};
    return {seen[0]&&p.pViewportState->viewportCount==1,seen[1]&&p.pViewportState->scissorCount==1};
}
bool digest(const detail::Sha256Digest& value,const char* expected){
    constexpr char hex[]="0123456789abcdef";
    for(std::size_t i=0;i<value.size();++i)if(hex[value[i]>>4]!=expected[i*2] || hex[value[i]&15]!=expected[i*2+1])return false;
    return true;
}
NativePipelineKind classify(std::size_t size,const detail::Sha256Digest& value){
    if(size==26928 && digest(value,"b03b19480b45cdde956cf36d1fe591d68465bda7dbe97b4a454477b5c2b2c86d"))return NativePipelineKind::visibility;
    if(size==48504 && digest(value,"e68043b76b025782c798105809be0f3e30635a1fb73bcabdbccb6828dfed9f64"))return NativePipelineKind::gbuffer;
    if((size==74292 && digest(value,"50177f15315cb95c82320a0c8fce5a66daec9652786f12601cac48e1355ff537")) ||
       (size==86252 && digest(value,"05dd03261e589f636486ffd0a71c9bb3510a20d0326120152972798a6bd01c20")))return NativePipelineKind::lighting;
    return NativePipelineKind::other;
}
bool stage(const VkPipelineShaderStageCreateInfo& s,VkShaderStageFlagBits type,const char* entry=nullptr){
    return simple(s,VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO) && s.stage==type && s.module &&
           s.pName && s.pName[0] && (!entry || std::strcmp(s.pName,entry)==0) && !s.pSpecializationInfo;
}
bool graphics_contract(const VkGraphicsPipelineCreateInfo& p,NativePipelineKind kind){
    if((kind!=NativePipelineKind::visibility && kind!=NativePipelineKind::gbuffer) ||
       p.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO || p.flags || p.renderPass || p.subpass ||
       p.basePipelineHandle || p.pTessellationState || !p.pNext || p.stageCount!=2 || !p.pStages)return false;
    const auto& render=*static_cast<const VkPipelineRenderingCreateInfo*>(p.pNext);
    if(render.sType!=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO || render.pNext)return false;
    if(!p.pDynamicState || !simple(*p.pDynamicState,VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO) ||
       p.pDynamicState->dynamicStateCount!=5 || !p.pDynamicState->pDynamicStates)return false;
    const std::array<VkDynamicState,5> expected{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK};
    for(const auto state:expected)if(std::count(p.pDynamicState->pDynamicStates,p.pDynamicState->pDynamicStates+5,state)!=1)return false;
    if(!p.pRasterizationState || !simple(*p.pRasterizationState,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO))return false;
    const auto& r=*p.pRasterizationState;
    if(r.depthClampEnable || r.rasterizerDiscardEnable || r.depthBiasEnable || r.polygonMode!=VK_POLYGON_MODE_FILL ||
       r.lineWidth!=1 || (r.cullMode&~VK_CULL_MODE_FRONT_AND_BACK) || r.frontFace>VK_FRONT_FACE_CLOCKWISE)return false;
    if(render.viewMask ||
       render.stencilAttachmentFormat!=VK_FORMAT_UNDEFINED || render.depthAttachmentFormat!=VK_FORMAT_D32_SFLOAT || !render.pColorAttachmentFormats)return false;
    if(kind==NativePipelineKind::visibility){
        if(render.colorAttachmentCount!=vis_formats.size() || !std::equal(vis_formats.begin(),vis_formats.end(),render.pColorAttachmentFormats))return false;
    }else if(render.colorAttachmentCount!=gbuf_formats.size() || !std::equal(gbuf_formats.begin(),gbuf_formats.end(),render.pColorAttachmentFormats))return false;
    if(!p.pVertexInputState || !simple(*p.pVertexInputState,VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO) ||
       p.pVertexInputState->vertexBindingDescriptionCount || p.pVertexInputState->vertexAttributeDescriptionCount ||
       !p.pInputAssemblyState || !simple(*p.pInputAssemblyState,VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO) ||
       p.pInputAssemblyState->topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST || p.pInputAssemblyState->primitiveRestartEnable ||
       !p.pViewportState || !simple(*p.pViewportState,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO) ||
       p.pViewportState->viewportCount!=1 || p.pViewportState->scissorCount!=1)return false;
    if(!p.pMultisampleState || !simple(*p.pMultisampleState,VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO))return false;
    const auto& m=*p.pMultisampleState;
    if(m.rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT || m.sampleShadingEnable || m.alphaToCoverageEnable || m.alphaToOneEnable ||
       (m.pSampleMask && !(*m.pSampleMask&1)))return false;
    if(!p.pDepthStencilState || !simple(*p.pDepthStencilState,VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO))return false;
    const auto& d=*p.pDepthStencilState;
    if(!d.depthTestEnable || d.depthBoundsTestEnable || d.stencilTestEnable ||
       d.depthCompareOp!=(kind==NativePipelineKind::visibility?VK_COMPARE_OP_GREATER:VK_COMPARE_OP_EQUAL) ||
       d.depthWriteEnable!=(kind==NativePipelineKind::visibility?VK_TRUE:VK_FALSE))return false;
    if(!p.pColorBlendState || !simple(*p.pColorBlendState,VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO) ||
       p.pColorBlendState->logicOpEnable || p.pColorBlendState->attachmentCount!=render.colorAttachmentCount || !p.pColorBlendState->pAttachments)return false;
    for(std::uint32_t i=0;i<p.pColorBlendState->attachmentCount;++i)if(p.pColorBlendState->pAttachments[i].blendEnable)return false;
    return true;
}
bool shadow_contract(const VkGraphicsPipelineCreateInfo& p){
    if(p.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO||p.flags||p.renderPass||p.subpass||
       p.basePipelineHandle||p.pTessellationState||p.stageCount!=1||!p.pNext)return false;
    const auto& render=*static_cast<const VkPipelineRenderingCreateInfo*>(p.pNext);
    if(render.sType!=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO||render.pNext||render.viewMask||
       render.colorAttachmentCount||render.stencilAttachmentFormat!=VK_FORMAT_UNDEFINED||render.depthAttachmentFormat!=VK_FORMAT_D32_SFLOAT)return false;
    const auto dynamic=raster_dynamic(p);if(!dynamic[0]||!dynamic[1])return false;
    const auto& raster=*p.pRasterizationState;
    if(raster.depthClampEnable||raster.depthBiasEnable||raster.polygonMode!=VK_POLYGON_MODE_FILL||raster.lineWidth!=1||
       (raster.cullMode&~VK_CULL_MODE_FRONT_AND_BACK)||raster.frontFace>VK_FRONT_FACE_CLOCKWISE)return false;
    if(!p.pVertexInputState||!simple(*p.pVertexInputState,VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO)||
       p.pVertexInputState->vertexBindingDescriptionCount||p.pVertexInputState->vertexAttributeDescriptionCount||
       !p.pInputAssemblyState||!simple(*p.pInputAssemblyState,VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO)||
       p.pInputAssemblyState->topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST||p.pInputAssemblyState->primitiveRestartEnable)return false;
    if(!p.pMultisampleState||!simple(*p.pMultisampleState,VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO))return false;
    const auto& ms=*p.pMultisampleState;
    if(ms.rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT||ms.sampleShadingEnable||ms.alphaToCoverageEnable||ms.alphaToOneEnable||
       (ms.pSampleMask&&!(*ms.pSampleMask&1u)))return false;
    if(!p.pDepthStencilState||!simple(*p.pDepthStencilState,VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO))return false;
    const auto& depth=*p.pDepthStencilState;
    // Blend state is ignored for zero color attachments and is never read.
    return depth.depthTestEnable&&depth.depthWriteEnable&&depth.depthCompareOp==VK_COMPARE_OP_GREATER&&
        !depth.depthBoundsTestEnable&&!depth.stencilTestEnable;
}
std::string pipeline_observation(VkPipeline handle,const VkGraphicsPipelineCreateInfo& p,NativePipelineKind kind,
                                 bool layout_known,const VkPipelineShaderStageCreateInfo* vertex,const VkPipelineShaderStageCreateInfo* fragment){
    const auto name=[](const char* value){std::string out;if(!value)return std::string("null");for(unsigned i=0;i<32&&value[i];++i){const char c=value[i];out+=c>=' '&&c<='~'&&c!='|'?c:'?';}return out;};
    std::ostringstream s;s<<"pipeline="<<handle<<" kind="<<static_cast<unsigned>(kind)<<" layout="<<p.layout<<" layoutKnown="<<layout_known
        <<" stages="<<p.stageCount<<":"<<(vertex?name(vertex->pName):"absent")<<","<<(fragment?name(fragment->pName):"absent")
        <<" stageValid="<<(vertex&&stage(*vertex,VK_SHADER_STAGE_VERTEX_BIT,"vs_main"))<<","<<(fragment&&stage(*fragment,VK_SHADER_STAGE_FRAGMENT_BIT,"fs_main"))
        <<" contract="<<graphics_contract(p,kind)<<" create="<<p.sType<<","<<p.flags<<","<<p.renderPass<<","<<p.subpass<<","<<bool(p.basePipelineHandle)<<","<<p.basePipelineIndex<<","<<bool(p.pTessellationState);
    if(vertex)s<<" VS="<<vertex->sType<<","<<vertex->flags<<","<<bool(vertex->pNext)<<","<<bool(vertex->pSpecializationInfo);
    if(fragment)s<<" FS="<<fragment->sType<<","<<fragment->flags<<","<<bool(fragment->pNext)<<","<<bool(fragment->pSpecializationInfo);
    // Dependent fixed state may legally be ignored, with unreadable pointers,
    // outside this complete observed rendering shell. Log presence only there.
    const auto* rendering=static_cast<const VkPipelineRenderingCreateInfo*>(p.pNext);
    bool inspect=p.sType==VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO && !p.flags && !p.renderPass && !p.subpass &&
        !p.basePipelineHandle && !p.pTessellationState && p.stageCount==2 && vertex && fragment &&
        rendering && rendering->sType==VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO && !rendering->pNext &&
        p.pDynamicState && simple(*p.pDynamicState,VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO) &&
        p.pDynamicState->dynamicStateCount==5 && p.pDynamicState->pDynamicStates;
    const std::array<VkDynamicState,5> expected{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK};
    if(inspect)for(const auto state:expected)inspect=inspect&&std::count(p.pDynamicState->pDynamicStates,p.pDynamicState->pDynamicStates+5,state)==1;
    inspect=inspect && p.pRasterizationState && simple(*p.pRasterizationState,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO) && !p.pRasterizationState->rasterizerDiscardEnable;
    if(inspect){const auto* render=static_cast<const VkPipelineRenderingCreateInfo*>(p.pNext);
        inspect=render && render->sType==VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO && !render->pNext && !render->viewMask &&
            render->depthAttachmentFormat==VK_FORMAT_D32_SFLOAT && render->stencilAttachmentFormat==VK_FORMAT_UNDEFINED && render->pColorAttachmentFormats;
        if(inspect)inspect=kind==NativePipelineKind::visibility?
            render->colorAttachmentCount==vis_formats.size()&&std::equal(vis_formats.begin(),vis_formats.end(),render->pColorAttachmentFormats):
            render->colorAttachmentCount==gbuf_formats.size()&&std::equal(gbuf_formats.begin(),gbuf_formats.end(),render->pColorAttachmentFormats);}
    if(!inspect){s<<" scalarSnapshot=unsupported-or-ignored statePointers="<<bool(p.pVertexInputState)<<","<<bool(p.pInputAssemblyState)<<","<<bool(p.pViewportState)<<","<<bool(p.pRasterizationState)<<","<<bool(p.pMultisampleState)<<","<<bool(p.pDepthStencilState)<<","<<bool(p.pColorBlendState)<<","<<bool(p.pDynamicState);return s.str().substr(0,1650);}
    const auto group=[&](const char* label,const auto* v){s<<" "<<label<<"=";if(v)s<<v->sType<<","<<v->flags<<","<<bool(v->pNext)<<":";else s<<"null";};
    const auto* vi=p.pVertexInputState;group("VI",vi);if(vi)s<<vi->vertexBindingDescriptionCount<<","<<vi->vertexAttributeDescriptionCount;
    const auto* ia=p.pInputAssemblyState;group("IA",ia);if(ia)s<<ia->topology<<","<<ia->primitiveRestartEnable;
    const auto* vp=p.pViewportState;group("VP",vp);if(vp)s<<vp->viewportCount<<","<<vp->scissorCount;
    const auto* r=p.pRasterizationState;group("R",r);if(r)s<<r->depthClampEnable<<","<<r->rasterizerDiscardEnable<<","<<r->polygonMode<<","<<r->cullMode<<","<<r->frontFace<<","<<r->depthBiasEnable<<","<<r->lineWidth<<","<<r->depthBiasConstantFactor<<","<<r->depthBiasClamp<<","<<r->depthBiasSlopeFactor;
    const auto* m=p.pMultisampleState;group("MS",m);if(m)s<<m->rasterizationSamples<<","<<m->sampleShadingEnable<<","<<m->minSampleShading<<","<<m->alphaToCoverageEnable<<","<<m->alphaToOneEnable<<","<<bool(m->pSampleMask)<<","<<(m->pSampleMask?*m->pSampleMask:0);
    const auto* d=p.pDepthStencilState;group("D",d);if(d)s<<d->depthTestEnable<<","<<d->depthWriteEnable<<","<<d->depthCompareOp<<","<<d->depthBoundsTestEnable<<","<<d->stencilTestEnable<<","<<d->minDepthBounds<<","<<d->maxDepthBounds;
    const auto* b=p.pColorBlendState;group("B",b);if(b){s<<b->logicOpEnable<<","<<b->logicOp<<","<<b->attachmentCount<<","<<bool(b->pAttachments);if(b->pAttachments)for(unsigned i=0;i<std::min(b->attachmentCount,6u);++i){const auto& a=b->pAttachments[i];s<<"["<<a.blendEnable<<","<<a.colorWriteMask<<","<<a.srcColorBlendFactor<<","<<a.dstColorBlendFactor<<","<<a.colorBlendOp<<","<<a.srcAlphaBlendFactor<<","<<a.dstAlphaBlendFactor<<","<<a.alphaBlendOp<<"]";}}
    const auto* dy=p.pDynamicState;group("DY",dy);if(dy){s<<dy->dynamicStateCount<<","<<bool(dy->pDynamicStates);if(dy->pDynamicStates)for(unsigned i=0;i<std::min(dy->dynamicStateCount,8u);++i)s<<","<<dy->pDynamicStates[i];}
    s<<" render="<<bool(p.pNext);if(p.pNext){const auto* render=static_cast<const VkPipelineRenderingCreateInfo*>(p.pNext);s<<","<<render->sType;if(render->sType==VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO){s<<","<<bool(render->pNext)<<","<<render->viewMask<<","<<render->depthAttachmentFormat<<","<<render->stencilAttachmentFormat<<","<<render->colorAttachmentCount<<","<<bool(render->pColorAttachmentFormats);if(render->pColorAttachmentFormats)for(unsigned i=0;i<std::min(render->colorAttachmentCount,6u);++i)s<<","<<render->pColorAttachmentFormats[i];}}
    return s.str().substr(0,1650);
}
}
NativeBufferBinding::operator bool()const noexcept{
    return info.buffer && generation && size && info.range && info.offset<size && info.range!=VK_WHOLE_SIZE &&
           info.range<=size-info.offset && (!set || (set_generation && set_version));
}
struct NativeState::Impl{
    struct Buffer{VkDeviceSize size=0;VkBufferUsageFlags usage=0;std::uint64_t generation=0;};
    struct Layout{std::map<std::uint32_t,VkDescriptorSetLayoutBinding> bindings;std::map<std::uint32_t,VkDescriptorBindingFlags> flags;std::uint64_t generation=0;};
    struct LayoutRef{VkDescriptorSetLayout handle{};std::uint64_t generation=0;};
    struct PipelineLayout{std::vector<LayoutRef> layouts;std::uint64_t generation=0;};
    struct Cell{VkDescriptorBufferInfo info{};std::uint64_t generation=0;};
    struct Content{std::vector<Cell> cells;bool known=false,poisoned=false;};
    // raw_known means an exact scalar payload was observed, not usable image provenance.
    struct SampledCell{VkDescriptorImageInfo info{};NativeSampledImageView view;bool known=false,poisoned=false,raw_known=false,storage_known=false,storage_poisoned=false;};
    struct Set{VkDescriptorPool pool{};LayoutRef layout;std::uint64_t generation=0,version=1;bool poisoned=false;std::map<std::uint32_t,Content> contents;std::map<std::uint32_t,SampledCell> sampled;std::array<std::uint32_t,6> rejected_update{};};
    struct Template{LayoutRef layout;std::vector<VkDescriptorUpdateTemplateEntry> entries;};
    struct Shader{NativePipelineKind kind=NativePipelineKind::other;std::uint32_t bytes=0;detail::Sha256Digest digest{};};
    struct Pipeline{NativePipelineState state;VkPipelineBindPoint point{};std::uint64_t layout_generation=0,generation=0;std::array<bool,2> raster_dynamic{};};
    struct Image{VkImageCreateInfo info{};std::uint64_t generation=0;std::vector<VkFormat> view_formats;};
    struct View{VkImageViewCreateInfo info{};std::uint64_t image_generation=0;VkImageUsageFlags usage=0;std::uint64_t generation=0;};
    struct SampledImage{VkImageType type{};VkFormat format{};VkExtent3D extent{};std::uint32_t mips=0,layers=0;VkImageUsageFlags usage=0;std::uint64_t generation=0;VkImageCreateFlags flags=0;std::uint32_t format_count=0;std::array<VkFormat,16> formats{};};
    struct Bind{VkPipeline pipeline{};std::uint64_t pipeline_generation=0;VkPipelineLayout layout{};std::uint64_t layout_generation=0;std::vector<std::pair<VkDescriptorSet,std::uint64_t>> sets;bool complete=false;};
    struct Command{std::array<Bind,2> points;std::array<VkPipeline,2> observed_pipeline{};std::array<VkPipelineLayout,2> observed_layout{};std::array<std::array<std::uint32_t,3>,2> observed_sets{};NativeRasterState raster;};
    mutable std::mutex mutex;
    std::atomic<bool> available{true};
    std::uint64_t next_generation=1;
    std::uint64_t buffers_epoch=1;
    std::map<VkBuffer,Buffer> buffers;
    std::map<VkDescriptorSetLayout,Layout> layouts;
    std::map<VkPipelineLayout,PipelineLayout> pipeline_layouts;
    std::map<VkDescriptorSet,Set> sets;
    std::map<VkDescriptorUpdateTemplate,Template> templates;
    std::map<VkShaderModule,Shader> shaders;
    std::map<VkPipeline,Pipeline> pipelines;
    std::map<VkPipeline,std::array<bool,2>> raster_declarations;
    std::map<VkImage,Image> images;
    std::map<VkImageView,View> views;
    std::map<VkImageView,View> attachment_views;
    // Separate bounded candidate caches: ordinary2D textures never evict3D
    // volume facts or exhaust the old attachment registry. Eviction only
    // makes sampled certificates unavailable; generations are never recycled.
    std::array<std::map<VkImage,SampledImage>,2> sampled_images;
    std::array<std::map<VkImageView,NativeSampledImageView>,2> sampled_views;
    std::map<VkCommandBuffer,Command> commands;
    std::array<std::string,2> native_pipeline_observations;
    std::uint64_t generation(){if(next_generation==std::numeric_limits<std::uint64_t>::max())throw std::bad_alloc();return next_generation++;}
    void advance_buffer_epoch(){if(buffers_epoch==std::numeric_limits<std::uint64_t>::max())throw std::bad_alloc();++buffers_epoch;}
    template<class Map,class Key,class Value>void store(Map& map,Key key,Value&& value,std::size_t limit=cap){
        if(map.find(key)==map.end() && map.size()>=limit)throw std::bad_alloc();
        map.insert_or_assign(key,std::forward<Value>(value));
    }
    template<class F>void write(F&& f)noexcept{
        try{std::lock_guard<std::mutex> lock(mutex);if(available)f();}catch(...){available=false;}
    }
    template<class R,class F>R read(F&& f,bool diagnostic=false)const noexcept{
        try{std::lock_guard<std::mutex> lock(mutex);if(available||diagnostic)return f();}catch(...){return {};}
        return {};
    }
    const Layout* layout(LayoutRef ref)const{
        const auto it=layouts.find(ref.handle);return it!=layouts.end() && it->second.generation==ref.generation?&it->second:nullptr;
    }
    const PipelineLayout* pipeline_layout(VkPipelineLayout handle)const{
        const auto it=pipeline_layouts.find(handle);if(it==pipeline_layouts.end())return nullptr;
        for(const auto& ref:it->second.layouts)if(!layout(ref))return nullptr;
        return &it->second;
    }
    bool set_valid(const Set& s)const{return layout(s.layout)!=nullptr;}
    void bump(Set& s){if(s.version==std::numeric_limits<std::uint64_t>::max())throw std::bad_alloc();++s.version;}
    void poison(Set& s){bump(s);s.poisoned=true;for(auto& pair:s.contents){pair.second.known=false;pair.second.poisoned=true;}for(auto& pair:s.sampled){pair.second.known=false;pair.second.poisoned=true;pair.second.raw_known=false;pair.second.storage_known=false;pair.second.storage_poisoned=true;}}
    template<class M,class K,class V>void sampled_store(M& map,K key,V&& value,std::size_t limit){
        if(map.size()>=limit&&map.find(key)==map.end())map.erase(map.begin());
        map.insert_or_assign(key,std::forward<V>(value));
    }
    void sampled_image(VkImage h,const VkImageCreateInfo& i){
        for(auto& m:sampled_images)m.erase(h);
        if(!h||i.sType!=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO||!(i.usage&VK_IMAGE_USAGE_SAMPLED_BIT)||
           (i.imageType!=VK_IMAGE_TYPE_2D&&i.imageType!=VK_IMAGE_TYPE_3D)||!sampled_color_format(i.format)||
           !i.extent.width||!i.extent.height||!i.extent.depth||i.extent.width>32768||i.extent.height>32768||i.extent.depth>32768||
           !i.mipLevels||i.mipLevels>16||!i.arrayLayers||i.arrayLayers>2048||i.samples!=VK_SAMPLE_COUNT_1_BIT||
           i.tiling!=VK_IMAGE_TILING_OPTIMAL||(i.imageType==VK_IMAGE_TYPE_2D&&i.extent.depth!=1)||(i.imageType==VK_IMAGE_TYPE_3D&&i.arrayLayers!=1))return;
        const bool plain=!i.flags&&!i.pNext;
        const bool radiance=i.imageType==VK_IMAGE_TYPE_3D&&i.format==VK_FORMAT_R32_UINT&&i.flags==VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT&&i.pNext;
        if(!plain&&!radiance)return; // Reject unsupported shells before reading extension data.
        SampledImage out{i.imageType,i.format,i.extent,i.mipLevels,i.arrayLayers,i.usage,0};out.flags=i.flags;
        if(radiance){const auto& list=*static_cast<const VkImageFormatListCreateInfo*>(i.pNext);
            if(list.sType!=VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO||list.pNext||!list.viewFormatCount||list.viewFormatCount>out.formats.size()||!list.pViewFormats)return;
            for(std::uint32_t n=0;n<list.viewFormatCount;++n){if(list.pViewFormats[n]==VK_FORMAT_UNDEFINED)return;out.formats[n]=list.pViewFormats[n];}
            out.format_count=list.viewFormatCount;
            if(std::find(out.formats.begin(),out.formats.begin()+out.format_count,i.format)==out.formats.begin()+out.format_count)return;
        }
        const auto index=i.imageType==VK_IMAGE_TYPE_3D?1u:0u;out.generation=generation();
        if(index)store(sampled_images[index],h,std::move(out),volume_lifetime_cap);
        else sampled_store(sampled_images[index],h,std::move(out),1024);
    }
    NativeSampledImageView sampled_view(VkImageView h)const{
        for(unsigned n=0;n<2;++n){const auto v=sampled_views[n].find(h);if(v==sampled_views[n].end())continue;
            const auto i=sampled_images[n].find(v->second.image);if(i!=sampled_images[n].end()&&i->second.generation==v->second.image_generation)return v->second;}
        return {};
    }
    void sampled_view(VkImageView h,const VkImageViewCreateInfo& i){
        for(auto& m:sampled_views)m.erase(h);
        if(!h||i.sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO||i.flags||i.components.r!=VK_COMPONENT_SWIZZLE_IDENTITY||
           i.components.g!=VK_COMPONENT_SWIZZLE_IDENTITY||i.components.b!=VK_COMPONENT_SWIZZLE_IDENTITY||i.components.a!=VK_COMPONENT_SWIZZLE_IDENTITY)return;
        const auto index=i.viewType==VK_IMAGE_VIEW_TYPE_3D?1u:0u;
        const auto found=sampled_images[index].find(i.image);if(found==sampled_images[index].end())return;const auto& image=found->second;
        const bool radiance=image.type==VK_IMAGE_TYPE_3D&&image.format==VK_FORMAT_R32_UINT&&image.flags==VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT&&image.format_count;
        const bool format=radiance?((i.format==VK_FORMAT_R32_UINT||i.format==VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)&&
            std::find(image.formats.begin(),image.formats.begin()+image.format_count,i.format)!=image.formats.begin()+image.format_count):i.format==image.format;
        if((i.viewType!=VK_IMAGE_VIEW_TYPE_2D&&i.viewType!=VK_IMAGE_VIEW_TYPE_3D)||!format||
           i.subresourceRange.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||i.subresourceRange.levelCount!=1||i.subresourceRange.layerCount!=1||
           i.subresourceRange.baseMipLevel>=image.mips||i.subresourceRange.baseArrayLayer>=image.layers||
           (index&&i.subresourceRange.baseArrayLayer))return;
        auto usage=image.usage;
        if(i.pNext){const auto& u=*static_cast<const VkImageViewUsageCreateInfo*>(i.pNext);
            if(u.sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO||u.pNext||!u.usage||(u.usage&~usage))return;
            usage=u.usage;}
        if(!(usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&!radiance)return;
        NativeSampledImageView out;out.view=h;out.image=i.image;out.image_type=image.type;out.samples=VK_SAMPLE_COUNT_1_BIT;
        out.view_type=i.viewType;out.format=i.format;out.extent={std::max(1u,image.extent.width>>i.subresourceRange.baseMipLevel),
            std::max(1u,image.extent.height>>i.subresourceRange.baseMipLevel),std::max(1u,image.extent.depth>>i.subresourceRange.baseMipLevel)};
        out.subresource=i.subresourceRange;out.usage=usage;out.image_generation=image.generation;out.view_generation=generation();
        out.image_flags=image.flags;out.image_usage=image.usage;out.image_format_count=image.format_count;out.image_formats=image.formats;
        out.image_format=image.format;out.mip_levels=image.mips;out.array_layers=image.layers;
        if(index)store(sampled_views[index],h,std::move(out),volume_lifetime_cap);
        else sampled_store(sampled_views[index],h,std::move(out),1024);
    }
    bool sampled_current(const SampledCell& cell)const{return cell.known&&(cell.view.usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&cell.info.imageView&&sampled_layout(cell.info.imageLayout)&&same_view(cell.view,sampled_view(cell.info.imageView));}
    bool storage_current(const SampledCell& cell)const{
        return cell.storage_known&&!cell.storage_poisoned&&cell.raw_known&&!cell.poisoned&&cell.info.imageView&&cell.info.imageLayout==VK_IMAGE_LAYOUT_GENERAL&&
            cell.view.view&&(cell.view.usage&VK_IMAGE_USAGE_STORAGE_BIT)&&same_view(cell.view,sampled_view(cell.info.imageView));
    }
    void sampled_direct(VkDescriptorSet h,std::uint32_t binding,std::uint32_t first,std::uint32_t count,const VkDescriptorImageInfo* value,bool valid,std::uint32_t op=1,VkDescriptorType type=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE){
        const auto it=sets.find(h);if(it==sets.end())return;auto& s=it->second;
        if(!valid||!span(s,binding,first,count,type)){reject(s,op,binding,first,count,type,!valid?1:2);return;}
        if(layout(s.layout)->bindings.at(binding).descriptorCount!=1){s.sampled.erase(binding);bump(s);return;}
        if(!value){reject(s,op,binding,first,count,type,3);return;}
        auto& cell=s.sampled[binding];const bool storage_poisoned=cell.storage_poisoned;cell={};cell.info={VK_NULL_HANDLE,value->imageView,value->imageLayout};cell.view=sampled_view(value->imageView);
        cell.raw_known=true;cell.known=type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE&&cell.view.view&&(cell.view.usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&sampled_layout(cell.info.imageLayout);
        cell.poisoned=type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE&&!cell.known;
        if(type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){const bool current=cell.view.view&&(cell.view.usage&VK_IMAGE_USAGE_STORAGE_BIT)&&cell.info.imageLayout==VK_IMAGE_LAYOUT_GENERAL;
            cell.storage_known=current&&(op==1||(!storage_poisoned&&!s.poisoned));cell.storage_poisoned=!cell.storage_known;}
        bump(s);
    }
    // Diagnostic POD only: operation,binding,first,count,type,reason. Never retain caller data pointers.
    void reject(Set& s,std::uint32_t op,std::uint32_t binding,std::uint32_t first,std::uint32_t count,std::uint32_t type,std::uint32_t reason){s.rejected_update={op,binding,first,count,type,reason};poison(s);}
    void poison_all(){for(auto& pair:sets)reject(pair.second,4,0,0,0,UINT32_MAX,9);}
    bool cell(VkDescriptorType type,const VkDescriptorBufferInfo& input,Cell& output)const{
        const auto it=buffers.find(input.buffer);if(it==buffers.end() || !buffer_type(type))return false;
        const auto& b=it->second;const auto usage=type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if(!(b.usage&usage) || input.offset>=b.size)return false;
        const auto range=input.range==VK_WHOLE_SIZE?b.size-input.offset:input.range;
        if(!range || range>b.size-input.offset)return false;
        output={{input.buffer,input.offset,range},b.generation};return true;
    }
    bool span(const Set& set,std::uint32_t binding,std::uint32_t first,std::uint32_t count,VkDescriptorType type)const{
        const auto* l=layout(set.layout);if(!l || !count || count>(buffer_type(type)?cap:opaque_cap))return false;
        const auto it=l->bindings.find(binding);return it!=l->bindings.end() && it->second.descriptorType==type && descriptor_type(type) &&
            first<=it->second.descriptorCount && count<=it->second.descriptorCount-first;
    }
    void direct(VkDescriptorSet handle,std::uint32_t binding,std::uint32_t first,std::uint32_t count,VkDescriptorType type,const VkDescriptorBufferInfo* values,bool valid,std::uint32_t operation=1){
        const auto it=sets.find(handle);if(it==sets.end())return;auto& s=it->second;
        if(!valid || !span(s,binding,first,count,type)){reject(s,operation,binding,first,count,type,!valid?1:2);return;}
        // Opaque descriptors are restored through their original native sets.
        // Exact, nonspilling writes cannot modify any tracked buffer binding.
        if(!buffer_type(type)){bump(s);return;}
        if(!values){reject(s,operation,binding,first,count,type,3);return;}
        std::vector<Cell> incoming;incoming.reserve(count);
        for(std::uint32_t i=0;i<count;++i){Cell c;if(!cell(type,values[i],c)){
            // The exact declared span cannot alter neighboring bindings. The
            // target array remains unknown until a full known direct repair.
            s.rejected_update={operation,binding,first,count,static_cast<std::uint32_t>(type),buffers.find(values[i].buffer)==buffers.end()?4u:5u};
            auto& rejected=s.contents[binding];rejected.known=false;rejected.poisoned=true;bump(s);return;
        }incoming.push_back(c);}
        const auto total=layout(s.layout)->bindings.at(binding).descriptorCount;auto& content=s.contents[binding];
        if(first==0 && count==total){content.cells=std::move(incoming);content.known=true;content.poisoned=false;}
        else if(content.known && content.cells.size()==total)std::copy(incoming.begin(),incoming.end(),content.cells.begin()+first);
        else{content.known=false;content.poisoned=true;s.rejected_update={operation,binding,first,count,static_cast<std::uint32_t>(type),6};}
        bump(s);
    }
    bool pipeline_valid(const Pipeline& p,bool require_supported=true)const{
        const auto* l=pipeline_layout(p.state.layout);return (!require_supported||p.state.supported) && l && l->generation==p.layout_generation;
    }
    NativeBoundState bound(const Bind& b,VkPipelineBindPoint bind_point,bool require_supported=true)const{
        const auto p=pipelines.find(b.pipeline);
        if(p==pipelines.end() || p->second.generation!=b.pipeline_generation || p->second.point!=bind_point || !pipeline_valid(p->second,require_supported) ||
           !b.complete || b.layout!=p->second.state.layout)return {};
        const auto* pl=pipeline_layout(b.layout);
        if(!pl || pl->generation!=b.layout_generation || b.sets.size()!=pl->layouts.size())return {};
        NativeBoundState result;result.pipeline=p->second.state;
        for(std::size_t i=0;i<b.sets.size();++i){
            const auto s=sets.find(b.sets[i].first);
            if(s==sets.end() || s->second.generation!=b.sets[i].second || !set_valid(s->second) || s->second.layout.handle!=pl->layouts[i].handle ||
               s->second.layout.generation!=pl->layouts[i].generation)return {};
            result.sets.push_back(s->first);
            result.set_generations.push_back(s->second.generation);result.set_versions.push_back(s->second.version);
        }
        result.valid=true;return result;
    }
    const Set* current_set(const NativeBoundState& state,std::uint32_t index,bool require_supported)const{
        if(!state.valid || (require_supported&&!state.pipeline.supported) || index>=state.sets.size())return nullptr;
        const auto p=pipelines.find(state.pipeline.pipeline);
        if(p==pipelines.end() || !pipeline_valid(p->second,require_supported) || p->second.state.layout!=state.pipeline.layout ||
           p->second.state.kind!=state.pipeline.kind || p->second.state.set_count!=state.pipeline.set_count ||
           p->second.state.generation!=state.pipeline.generation || p->second.state.supported!=state.pipeline.supported ||
           p->second.state.vertex_bytes!=state.pipeline.vertex_bytes || p->second.state.vertex_digest!=state.pipeline.vertex_digest ||
           p->second.state.mesh_bytes!=state.pipeline.mesh_bytes || p->second.state.mesh_digest!=state.pipeline.mesh_digest ||
           p->second.state.fragment_bytes!=state.pipeline.fragment_bytes || p->second.state.fragment_digest!=state.pipeline.fragment_digest ||
           p->second.state.graphics_stage_count!=state.pipeline.graphics_stage_count ||
           p->second.state.tess_control_bytes!=state.pipeline.tess_control_bytes || p->second.state.tess_control_digest!=state.pipeline.tess_control_digest ||
           p->second.state.tess_evaluation_bytes!=state.pipeline.tess_evaluation_bytes || p->second.state.tess_evaluation_digest!=state.pipeline.tess_evaluation_digest ||
           p->second.state.oit_compatible!=state.pipeline.oit_compatible || p->second.state.shadow_compatible!=state.pipeline.shadow_compatible || p->second.state.compute_bytes!=state.pipeline.compute_bytes ||
           p->second.state.compute_digest!=state.pipeline.compute_digest)return {};
        const auto* pl=pipeline_layout(state.pipeline.layout);if(!pl || state.sets.size()!=pl->layouts.size() ||
            state.set_generations.size()!=state.sets.size() || state.set_versions.size()!=state.sets.size())return {};
        const auto set=sets.find(state.sets[index]);if(set==sets.end() || !set_valid(set->second) ||
            set->second.layout.handle!=pl->layouts[index].handle || set->second.layout.generation!=pl->layouts[index].generation ||
            set->second.generation!=state.set_generations[index] || set->second.version!=state.set_versions[index])return {};
        return &set->second;
    }
    NativeBufferBinding source(const NativeBoundState& state,std::uint32_t index,std::uint32_t binding,bool require_supported=true)const{
        const auto* current=current_set(state,index,require_supported);if(!current)return {};const auto& s=*current;
        const auto set=sets.find(state.sets[index]);const auto content=s.contents.find(binding);
        if(content==s.contents.end() || !content->second.known || content->second.cells.empty())return {};
        const auto* l=layout(s.layout);const auto decl=l->bindings.find(binding);
        if(decl==l->bindings.end() || !buffer_type(decl->second.descriptorType) || content->second.cells.size()!=decl->second.descriptorCount)return {};
        for(const auto& c:content->second.cells){const auto b=buffers.find(c.info.buffer);if(b==buffers.end() || b->second.generation!=c.generation)return {};}
        const auto& c=content->second.cells.front();const auto& b=buffers.at(c.info.buffer);
        return {c.info,b.size,b.usage,b.generation,s.generation,s.version,set->first,binding,l->flags.at(binding)};
    }
    NativeStorageImageBinding storage_source(const NativeBoundState& state,std::uint32_t index,std::uint32_t binding)const{
        const auto* s=current_set(state,index,false);if(!s||s->poisoned)return {};
        // A query requires the complete observed prefix to remain current;
        // the returned token itself certifies only its selected set/resource.
        for(std::uint32_t n=0;n<state.sets.size();++n)if(!current_set(state,n,false))return {};
        const auto* l=layout(s->layout);const auto d=l->bindings.find(binding);
        if(d==l->bindings.end()||d->second.descriptorType!=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE||d->second.descriptorCount!=1)return {};
        const auto c=s->sampled.find(binding);if(c==s->sampled.end()||!storage_current(c->second))return {};
        return {c->second.info,c->second.view,state.sets[index],binding,s->generation,s->version,l->flags.at(binding)};
    }
    NativeSampledImageBinding sampled_source(const NativeBoundState& state,std::uint32_t index,std::uint32_t binding)const{
        const auto* s=current_set(state,index,false);if(!s)return {};const auto* l=layout(s->layout);const auto d=l->bindings.find(binding);
        if(d==l->bindings.end()||d->second.descriptorType!=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||d->second.descriptorCount!=1)return {};
        const auto c=s->sampled.find(binding);if(c==s->sampled.end()||!sampled_current(c->second))return {};
        return {c->second.info,c->second.view,state.sets[index],binding,s->generation,s->version,l->flags.at(binding)};
    }
};

NativeState::NativeState():impl_(new(std::nothrow) Impl){}
NativeState::~NativeState()=default;
NativeSampledImageBinding::operator bool()const noexcept{
    return set&&set_generation&&set_version&&!info.sampler&&info.imageView&&info.imageView==view.view&&sampled_layout(info.imageLayout)&&
        view.image&&view.image_generation&&view.view_generation&&(view.usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&view.samples==VK_SAMPLE_COUNT_1_BIT&&
        ((view.image_type==VK_IMAGE_TYPE_2D&&view.view_type==VK_IMAGE_VIEW_TYPE_2D&&view.extent.depth==1)||
         (view.image_type==VK_IMAGE_TYPE_3D&&view.view_type==VK_IMAGE_VIEW_TYPE_3D))&&view.extent.width&&view.extent.height&&view.extent.depth&&
        view.subresource.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&view.subresource.levelCount==1&&view.subresource.layerCount==1;
}
NativeStorageImageBinding::operator bool()const noexcept{
    return set&&set_generation&&set_version&&!info.sampler&&info.imageView&&info.imageView==view.view&&info.imageLayout==VK_IMAGE_LAYOUT_GENERAL&&
        view.image&&view.image_generation&&view.view_generation&&(view.usage&VK_IMAGE_USAGE_STORAGE_BIT)&&view.samples==VK_SAMPLE_COUNT_1_BIT&&
        ((view.image_type==VK_IMAGE_TYPE_2D&&view.view_type==VK_IMAGE_VIEW_TYPE_2D&&view.extent.depth==1)||
         (view.image_type==VK_IMAGE_TYPE_3D&&view.view_type==VK_IMAGE_VIEW_TYPE_3D))&&view.extent.width&&view.extent.height&&view.extent.depth&&
        view.subresource.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&view.subresource.levelCount==1&&view.subresource.layerCount==1;
}
NativeStorageImageBinding NativeState::observed_storage_image_binding(const NativeBoundState& state,std::uint32_t index,std::uint32_t binding)const noexcept{
    return impl_?impl_->read<NativeStorageImageBinding>([&]{return impl_->storage_source(state,index,binding);}):NativeStorageImageBinding{};
}
bool NativeState::live(const NativeStorageImageBinding& ref)const noexcept{
    return impl_&&impl_->read<bool>([&]{if(!ref)return false;const auto s=impl_->sets.find(ref.set);
        if(s==impl_->sets.end()||!impl_->set_valid(s->second)||s->second.poisoned||s->second.generation!=ref.set_generation||s->second.version!=ref.set_version)return false;
        const auto* l=impl_->layout(s->second.layout);const auto d=l->bindings.find(ref.binding);const auto f=l->flags.find(ref.binding);
        if(d==l->bindings.end()||d->second.descriptorType!=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE||d->second.descriptorCount!=1||f==l->flags.end()||f->second!=ref.declaration_flags)return false;
        const auto c=s->second.sampled.find(ref.binding);return c!=s->second.sampled.end()&&impl_->storage_current(c->second)&&
            c->second.info.imageView==ref.info.imageView&&c->second.info.imageLayout==ref.info.imageLayout&&same_view(c->second.view,ref.view);});
}
NativeOitView NativeState::attachment_view(VkImageView h)const noexcept{
    if(!impl_)return {};
    return impl_->read<NativeOitView>([&]{const auto v=impl_->attachment_views.find(h);if(v==impl_->attachment_views.end())return NativeOitView{};
        const auto image=impl_->images.find(v->second.info.image);if(image==impl_->images.end()||image->second.generation!=v->second.image_generation)return NativeOitView{};
        const auto& value=v->second;NativeOitView out;out.view=h;out.image=value.info.image;out.format=value.info.format;out.type=value.info.viewType;
        out.extent=image->second.info.extent;out.samples=image->second.info.samples;out.usage=value.usage;out.subresource=value.info.subresourceRange;
        out.image_generation=image->second.generation;out.view_generation=value.generation;out.current=true;return out;});
}
std::string NativeState::sampled_binding_diagnostic(const NativeBoundState& state,std::uint32_t index,std::uint32_t binding)const noexcept{
    if(!impl_)return {};
    return impl_->read<std::string>([&]{std::ostringstream out;
        out<<"available="<<impl_->available.load()<<" usable="<<(impl_->available&&bool(impl_->sampled_source(state,index,binding)))
           <<" boundValid="<<state.valid<<" currentBound="<<(impl_->available&&bool(impl_->current_set(state,index,false)))<<" setIndex="<<index<<" binding="<<binding;
        const auto handle=index<state.sets.size()?state.sets[index]:VK_NULL_HANDLE;const auto set=impl_->sets.find(handle);
        out<<" set="<<handle<<" setExists="<<(set!=impl_->sets.end());if(set==impl_->sets.end())return out.str();
        const auto& value=set->second;const auto* layout=impl_->layout(value.layout);
        out<<" layoutKnown="<<bool(layout)<<" setPoisoned="<<value.poisoned<<" setGeneration="<<value.generation<<" setVersion="<<value.version;
        if(layout){const auto d=layout->bindings.find(binding);out<<" declared="<<(d!=layout->bindings.end());
            if(d!=layout->bindings.end())out<<" type="<<d->second.descriptorType<<" declaredCount="<<d->second.descriptorCount<<" declarationFlags="<<layout->flags.at(binding);}
        const auto cell=value.sampled.find(binding);out<<" cellExists="<<(cell!=value.sampled.end());
        if(cell!=value.sampled.end()){const auto& c=cell->second;const auto current=impl_->sampled_view(c.info.imageView);
            out<<" rawObserved="<<c.raw_known<<" contentsKnown="<<c.known<<" currentCellKnown="<<impl_->sampled_current(c)<<" contentsPoisoned="<<c.poisoned
               <<" retainedImageView="<<c.info.imageView<<" retainedLayout="<<c.info.imageLayout
               <<" currentViewKnown="<<bool(current.view)
               <<" retainedImage="<<c.view.image<<" retainedImageGeneration="<<c.view.image_generation<<" retainedViewGeneration="<<c.view.view_generation;
            if(current.view){out<<" currentViewFormat="<<current.format<<" currentViewUsage="<<current.usage<<" currentImageFlags="<<current.image_flags
                <<" currentImageUsage="<<current.image_usage<<" currentImageFormatCount="<<current.image_format_count<<" currentImageFormats=";
                for(std::uint32_t n=0;n<current.image_format_count;++n){if(n)out<<",";out<<current.image_formats[n];}}}
        return out.str().substr(0,2048);
    },true);
}
NativeSampledImageView NativeState::sampled_view(VkImageView h)const noexcept{return impl_?impl_->read<NativeSampledImageView>([&]{return impl_->sampled_view(h);}):NativeSampledImageView{};}
NativeSampledImageBinding NativeState::observed_sampled_image_binding(const NativeBoundState& state,std::uint32_t index,std::uint32_t binding)const noexcept{
    return impl_?impl_->read<NativeSampledImageBinding>([&]{return impl_->sampled_source(state,index,binding);}):NativeSampledImageBinding{};
}
bool NativeState::live(const NativeSampledImageBinding& ref)const noexcept{
    return impl_&&impl_->read<bool>([&]{if(!ref)return false;const auto s=impl_->sets.find(ref.set);
        if(s==impl_->sets.end()||!impl_->set_valid(s->second)||s->second.generation!=ref.set_generation||s->second.version!=ref.set_version)return false;
        const auto* l=impl_->layout(s->second.layout);const auto d=l->bindings.find(ref.binding);const auto f=l->flags.find(ref.binding);
        if(d==l->bindings.end()||d->second.descriptorType!=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||d->second.descriptorCount!=1||f==l->flags.end()||f->second!=ref.declaration_flags)return false;
        const auto c=s->second.sampled.find(ref.binding);return c!=s->second.sampled.end()&&impl_->sampled_current(c->second)&&
            c->second.info.imageView==ref.info.imageView&&c->second.info.imageLayout==ref.info.imageLayout&&same_view(c->second.view,ref.view);});
}
void NativeState::buffer_created(VkBuffer h,const VkBufferCreateInfo& i)noexcept{
    if(impl_)impl_->write([&]{impl_->advance_buffer_epoch();impl_->buffers.erase(h);if(h && i.sType==VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && !i.pNext && !i.flags && i.size && i.usage)
        impl_->store(impl_->buffers,h,Impl::Buffer{i.size,i.usage,impl_->generation()});});
}
void NativeState::buffer_destroyed(VkBuffer h)noexcept{if(impl_)impl_->write([&]{impl_->advance_buffer_epoch();impl_->buffers.erase(h);});}
NativeBufferBinding NativeState::buffer(VkBuffer h)const noexcept{
    if(!impl_)return {};
    return impl_->read<NativeBufferBinding>([&]{const auto it=impl_->buffers.find(h);if(it==impl_->buffers.end())return NativeBufferBinding{};
        const auto& b=it->second;return NativeBufferBinding{{h,0,b.size},b.size,b.usage,b.generation,0,0,VK_NULL_HANDLE,0};});
}
std::uint64_t NativeState::buffer_epoch()const noexcept {if(!impl_)return 0;return impl_->read<std::uint64_t>([&]{return impl_->buffers_epoch;});}
void NativeState::layout_created(VkDescriptorSetLayout h,const VkDescriptorSetLayoutCreateInfo& i)noexcept{
    if(impl_)impl_->write([&]{impl_->layouts.erase(h);
        if(!h || i.sType!=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO || i.flags || i.bindingCount>binding_cap || (i.bindingCount && !i.pBindings))return;
        const VkDescriptorBindingFlags* flags=nullptr;
        if(i.pNext){const auto& f=*static_cast<const VkDescriptorSetLayoutBindingFlagsCreateInfo*>(i.pNext);
            if(f.sType!=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO || f.pNext || f.bindingCount!=i.bindingCount || (f.bindingCount && !f.pBindingFlags))return;
            for(std::uint32_t j=0;j<f.bindingCount;++j)if(f.pBindingFlags[j]&~VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT)return;
            flags=f.pBindingFlags;}
        Impl::Layout l;l.generation=impl_->generation();std::size_t total=0,buffers=0;
        for(std::uint32_t j=0;j<i.bindingCount;++j){auto b=i.pBindings[j];
            if(!descriptor_type(b.descriptorType) || !b.descriptorCount || b.descriptorCount>opaque_cap-total ||
               (buffer_type(b.descriptorType) && b.descriptorCount>cap-buffers) || !b.stageFlags || !l.bindings.emplace(b.binding,b).second)return;
            total+=b.descriptorCount;if(buffer_type(b.descriptorType))buffers+=b.descriptorCount;
            l.bindings.at(b.binding).pImmutableSamplers=nullptr;l.flags.emplace(b.binding,flags?flags[j]:0);}
        impl_->store(impl_->layouts,h,std::move(l));});
}
void NativeState::layout_destroyed(VkDescriptorSetLayout h)noexcept{if(impl_)impl_->write([&]{impl_->layouts.erase(h);});}
void NativeState::pipeline_layout_created(VkPipelineLayout h,const VkPipelineLayoutCreateInfo& i)noexcept{
    if(impl_)impl_->write([&]{impl_->pipeline_layouts.erase(h);
        if(!h || !simple(i,VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO) || i.setLayoutCount>32 || (i.setLayoutCount && !i.pSetLayouts) ||
           i.pushConstantRangeCount>32 || (i.pushConstantRangeCount && !i.pPushConstantRanges))return;
        for(std::uint32_t j=0;j<i.pushConstantRangeCount;++j){const auto& r=i.pPushConstantRanges[j];
            if(!r.stageFlags || !r.size || r.offset%4 || r.size%4 || r.offset>4096 || r.size>4096-r.offset)return;}
        Impl::PipelineLayout l;l.generation=impl_->generation();
        for(std::uint32_t j=0;j<i.setLayoutCount;++j){const auto it=impl_->layouts.find(i.pSetLayouts[j]);if(it==impl_->layouts.end())return;l.layouts.push_back({it->first,it->second.generation});}
        impl_->store(impl_->pipeline_layouts,h,std::move(l));});
}
void NativeState::pipeline_layout_destroyed(VkPipelineLayout h)noexcept{if(impl_)impl_->write([&]{impl_->pipeline_layouts.erase(h);});}
void NativeState::sets_allocated(const VkDescriptorSetAllocateInfo& i,const VkDescriptorSet* handles)noexcept{
    if(impl_)impl_->write([&]{if(i.descriptorSetCount>cap || (i.descriptorSetCount && !handles)){impl_->available=false;return;}
        for(std::uint32_t j=0;j<i.descriptorSetCount;++j){impl_->sets.erase(handles[j]);
            if(!handles[j] || i.sType!=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO || i.pNext || !i.descriptorPool || !i.pSetLayouts)continue;
            const auto l=impl_->layouts.find(i.pSetLayouts[j]);if(l==impl_->layouts.end())continue;
            Impl::Set s;s.pool=i.descriptorPool;s.layout={l->first,l->second.generation};s.generation=impl_->generation();impl_->store(impl_->sets,handles[j],std::move(s));}});
}
void NativeState::sets_freed(std::uint32_t count,const VkDescriptorSet* handles)noexcept{
    if(impl_)impl_->write([&]{if(count>cap || (count && !handles)){impl_->available=false;return;}for(std::uint32_t i=0;i<count;++i)impl_->sets.erase(handles[i]);});
}
void NativeState::pool_reset(VkDescriptorPool h)noexcept{
    if(impl_)impl_->write([&]{for(auto it=impl_->sets.begin();it!=impl_->sets.end();)if(it->second.pool==h)it=impl_->sets.erase(it);else ++it;});
}
void NativeState::descriptors_written(std::uint32_t count,const VkWriteDescriptorSet* writes)noexcept{
    if(impl_)impl_->write([&]{if(count>cap || (count && !writes)){impl_->poison_all();return;}
        for(std::uint32_t i=0;i<count;++i){const auto& w=writes[i];bool valid=w.sType==VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET && !w.pNext;
            if(buffer_type(w.descriptorType))valid=valid && !w.pImageInfo && !w.pTexelBufferView;
            else if(w.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || w.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)valid=valid && w.pTexelBufferView;
            else if(w.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || w.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)valid=valid && w.pBufferInfo;
            else valid=valid && w.pImageInfo;
            if(w.descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||w.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)impl_->sampled_direct(w.dstSet,w.dstBinding,w.dstArrayElement,w.descriptorCount,w.pImageInfo,valid,1,w.descriptorType);
            else impl_->direct(w.dstSet,w.dstBinding,w.dstArrayElement,w.descriptorCount,w.descriptorType,w.pBufferInfo,valid);}});
}
void NativeState::descriptors_copied(std::uint32_t count,const VkCopyDescriptorSet* copies)noexcept{
    if(impl_)impl_->write([&]{if(count>cap || (count && !copies)){impl_->poison_all();return;}
        for(std::uint32_t i=0;i<count;++i){const auto& c=copies[i];const auto dst=impl_->sets.find(c.dstSet);if(dst==impl_->sets.end())continue;
            const auto src=impl_->sets.find(c.srcSet);auto& target=dst->second;
            if(c.sType!=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET || c.pNext || src==impl_->sets.end() || !impl_->set_valid(src->second) || !impl_->set_valid(target)){impl_->reject(target,2,c.dstBinding,c.dstArrayElement,c.descriptorCount,UINT32_MAX,7);continue;}
            const auto* l=impl_->layout(src->second.layout);const auto decl=l->bindings.find(c.srcBinding);
            if(decl==l->bindings.end() || !impl_->span(src->second,c.srcBinding,c.srcArrayElement,c.descriptorCount,decl->second.descriptorType) ||
               !impl_->span(target,c.dstBinding,c.dstArrayElement,c.descriptorCount,decl->second.descriptorType)){impl_->reject(target,2,c.dstBinding,c.dstArrayElement,c.descriptorCount,UINT32_MAX,7);continue;}
            if(c.srcSet==c.dstSet && c.srcBinding==c.dstBinding &&
               c.srcArrayElement<c.dstArrayElement+c.descriptorCount && c.dstArrayElement<c.srcArrayElement+c.descriptorCount){impl_->reject(target,2,c.dstBinding,c.dstArrayElement,c.descriptorCount,UINT32_MAX,7);continue;}
            if(decl->second.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){
                const auto cell=src->second.sampled.find(c.srcBinding);const auto previous=target.sampled.find(c.dstBinding);
                const bool poisoned=previous!=target.sampled.end()&&previous->second.storage_poisoned;
                if(decl->second.descriptorCount!=1)target.sampled.erase(c.dstBinding);
                else if(cell!=src->second.sampled.end()&&cell->second.raw_known&&!cell->second.poisoned){auto raw=cell->second;raw.known=false;
                    raw.storage_known=!target.poisoned&&!src->second.poisoned&&!poisoned&&impl_->storage_current(cell->second);raw.storage_poisoned=!raw.storage_known;target.sampled[c.dstBinding]=raw;}
                else{auto& raw=target.sampled[c.dstBinding];raw.raw_known=false;raw.known=false;raw.poisoned=true;raw.storage_known=false;raw.storage_poisoned=true;}
                impl_->bump(target);continue;
            }
            if(decl->second.descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE){
                const auto cell=src->second.sampled.find(c.srcBinding);const auto old=target.sampled.find(c.dstBinding);
                if(decl->second.descriptorCount!=1){target.sampled.erase(c.dstBinding);impl_->bump(target);continue;}
                if(cell==src->second.sampled.end()||!impl_->sampled_current(cell->second)||(old==target.sampled.end()&&target.poisoned)||
                   (old!=target.sampled.end()&&old->second.poisoned)){impl_->reject(target,2,c.dstBinding,c.dstArrayElement,c.descriptorCount,UINT32_MAX,7);continue;}
                target.sampled[c.dstBinding]=cell->second;impl_->bump(target);continue;
            }
            if(!buffer_type(decl->second.descriptorType)){impl_->bump(target);continue;}
            const auto source=src->second.contents.find(c.srcBinding);
            if(source==src->second.contents.end() || !source->second.known || source->second.cells.size()!=decl->second.descriptorCount){impl_->reject(target,2,c.dstBinding,c.dstArrayElement,c.descriptorCount,UINT32_MAX,7);continue;}
            std::vector<Impl::Cell> cells(source->second.cells.begin()+c.srcArrayElement,source->second.cells.begin()+c.srcArrayElement+c.descriptorCount);
            bool valid=true;for(const auto& cell:cells){const auto b=impl_->buffers.find(cell.info.buffer);if(b==impl_->buffers.end() || b->second.generation!=cell.generation)valid=false;}
            if(!valid){impl_->reject(target,2,c.dstBinding,c.dstArrayElement,c.descriptorCount,UINT32_MAX,7);continue;}
            const auto total=impl_->layout(target.layout)->bindings.at(c.dstBinding).descriptorCount;const auto old=target.contents.find(c.dstBinding);
            if((old==target.contents.end() && target.poisoned) || (old!=target.contents.end() && old->second.poisoned)){impl_->reject(target,2,c.dstBinding,c.dstArrayElement,c.descriptorCount,UINT32_MAX,7);continue;}
            auto& out=target.contents[c.dstBinding];
            if(c.dstArrayElement==0 && c.descriptorCount==total){out.cells=std::move(cells);out.known=true;}
            else if(out.known && out.cells.size()==total)std::copy(cells.begin(),cells.end(),out.cells.begin()+c.dstArrayElement);
            else{out.known=false;out.poisoned=true;target.rejected_update={2,c.dstBinding,c.dstArrayElement,c.descriptorCount,static_cast<std::uint32_t>(decl->second.descriptorType),6};}
            impl_->bump(target);}});
}
void NativeState::template_created(VkDescriptorUpdateTemplate h,const VkDescriptorUpdateTemplateCreateInfo& i)noexcept{
    if(impl_)impl_->write([&]{impl_->templates.erase(h);
        if(!h || !simple(i,VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO) || i.templateType!=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET ||
           i.descriptorUpdateEntryCount>binding_cap || (i.descriptorUpdateEntryCount && !i.pDescriptorUpdateEntries))return;
        const auto l=impl_->layouts.find(i.descriptorSetLayout);if(l==impl_->layouts.end())return;
        Impl::Template t;t.layout={l->first,l->second.generation};std::size_t total=0,buffers=0;
        for(std::uint32_t j=0;j<i.descriptorUpdateEntryCount;++j){const auto e=i.pDescriptorUpdateEntries[j];const auto b=l->second.bindings.find(e.dstBinding);
            const auto bytes=descriptor_size(e.descriptorType);
            if(!descriptor_type(e.descriptorType) || b==l->second.bindings.end() || b->second.descriptorType!=e.descriptorType || !e.descriptorCount ||
               e.descriptorCount>opaque_cap-total || (buffer_type(e.descriptorType) && e.descriptorCount>cap-buffers) ||
               e.dstArrayElement>b->second.descriptorCount || e.descriptorCount>b->second.descriptorCount-e.dstArrayElement ||
               e.offset>template_cap-bytes || (e.descriptorCount>1 && e.stride>(template_cap-bytes-e.offset)/(e.descriptorCount-1)))return;
            total+=e.descriptorCount;if(buffer_type(e.descriptorType))buffers+=e.descriptorCount;t.entries.push_back(e);}
        impl_->store(impl_->templates,h,std::move(t));});
}
void NativeState::template_destroyed(VkDescriptorUpdateTemplate h)noexcept{if(impl_)impl_->write([&]{impl_->templates.erase(h);});}
void NativeState::template_written(VkDescriptorSet h,VkDescriptorUpdateTemplate update,const void* data)noexcept{
    if(impl_)impl_->write([&]{const auto s=impl_->sets.find(h);if(s==impl_->sets.end())return;const auto t=impl_->templates.find(update);
        if(t==impl_->templates.end() || !data || !impl_->layout(t->second.layout) || s->second.layout.handle!=t->second.layout.handle ||
           s->second.layout.generation!=t->second.layout.generation){impl_->reject(s->second,3,0,0,0,UINT32_MAX,8);return;}
        // Templates and copies cannot repair unknown provenance. Only a full
        // known direct write of the selected binding clears its poison state.
        for(const auto& e:t->second.entries){if(e.descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE){const auto old=s->second.sampled.find(e.dstBinding);
                if((old==s->second.sampled.end()&&s->second.poisoned)||(old!=s->second.sampled.end()&&old->second.poisoned)){impl_->reject(s->second,3,0,0,0,UINT32_MAX,8);return;}}
            if(!buffer_type(e.descriptorType))continue;
            const auto old=s->second.contents.find(e.dstBinding);
            if((old==s->second.contents.end() && s->second.poisoned) || (old!=s->second.contents.end() && old->second.poisoned)){impl_->reject(s->second,3,0,0,0,UINT32_MAX,8);return;}}
        for(const auto& e:t->second.entries){if(e.descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||e.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){
                if(impl_->layout(s->second.layout)->bindings.at(e.dstBinding).descriptorCount!=1){impl_->bump(s->second);continue;}
                VkDescriptorImageInfo value{};std::memcpy(&value,static_cast<const unsigned char*>(data)+e.offset,sizeof(value));
                impl_->sampled_direct(h,e.dstBinding,e.dstArrayElement,e.descriptorCount,&value,true,3,e.descriptorType);continue;}
            if(!buffer_type(e.descriptorType)){impl_->bump(s->second);continue;}std::vector<VkDescriptorBufferInfo> values(e.descriptorCount);
            for(std::uint32_t j=0;j<e.descriptorCount;++j)std::memcpy(&values[j],static_cast<const unsigned char*>(data)+e.offset+j*e.stride,sizeof(VkDescriptorBufferInfo));
            impl_->direct(h,e.dstBinding,e.dstArrayElement,e.descriptorCount,e.descriptorType,values.data(),true,3);}});
}
void NativeState::shader_created(VkShaderModule h,const VkShaderModuleCreateInfo& i)noexcept{
    if(impl_)impl_->write([&]{impl_->shaders.erase(h);if(!h || !simple(i,VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO) || !i.pCode || i.codeSize<20 ||
        i.codeSize%4 || i.codeSize>16*1024*1024 || i.pCode[0]!=0x07230203)return;
        const auto fingerprint=detail::sha256(i.pCode,i.codeSize);
        const auto kind=classify(i.codeSize,fingerprint);
        impl_->store(impl_->shaders,h,Impl::Shader{kind,static_cast<std::uint32_t>(i.codeSize),fingerprint});});
}
void NativeState::shader_destroyed(VkShaderModule h)noexcept{if(impl_)impl_->write([&]{impl_->shaders.erase(h);});}
void NativeState::graphics_pipelines_created(std::uint32_t count,const VkGraphicsPipelineCreateInfo* infos,const VkPipeline* handles)noexcept{
    if(impl_)impl_->write([&]{if(count>cap || (count && (!infos || !handles))){impl_->available=false;return;}
        for(std::uint32_t i=0;i<count;++i){const auto h=handles[i];impl_->pipelines.erase(h);impl_->raster_declarations.erase(h);if(!h)continue;const auto& p=infos[i];const auto* l=impl_->pipeline_layout(p.layout);
            impl_->store(impl_->raster_declarations,h,inherited_raster_declaration(p));
            const VkPipelineShaderStageCreateInfo* vertex=nullptr;const VkPipelineShaderStageCreateInfo* fragment=nullptr;
            const VkPipelineShaderStageCreateInfo* control=nullptr;const VkPipelineShaderStageCreateInfo* evaluation=nullptr;const VkPipelineShaderStageCreateInfo* mesh=nullptr;
            bool unique=true;
            if(p.pStages&&p.stageCount<=32)for(std::uint32_t j=0;j<p.stageCount;++j){
                const auto& s=p.pStages[j];const VkPipelineShaderStageCreateInfo** slot=nullptr;
                switch(s.stage){
                    case VK_SHADER_STAGE_VERTEX_BIT:slot=&vertex;break;
                    case VK_SHADER_STAGE_FRAGMENT_BIT:slot=&fragment;break;
                    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:slot=&control;break;
                    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:slot=&evaluation;break;
                    case VK_SHADER_STAGE_MESH_BIT_EXT:slot=&mesh;break;
                    default:unique=false;break;
                }
                if(slot){if(*slot)unique=false;*slot=&s;}
            }
            if(vertex){const auto known=impl_->shaders.find(vertex->module);if(known!=impl_->shaders.end()&&(known->second.kind==NativePipelineKind::visibility||known->second.kind==NativePipelineKind::gbuffer)){
                try{impl_->native_pipeline_observations[known->second.kind==NativePipelineKind::visibility?0:1]=pipeline_observation(h,p,known->second.kind,l!=nullptr,vertex,fragment);}catch(...){}
            }}
            bool shadow_only=false;
            if(p.stageCount==1&&vertex&&!fragment&&!mesh){const auto known=impl_->shaders.find(vertex->module);
                shadow_only=known!=impl_->shaders.end()&&known->second.bytes==26060&&
                    digest(known->second.digest,"a109d726a6c928a89eb9eaf15dc91c2ca2f84a1e663296fd375db09386537002");}
            if(!l || p.sType!=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO || (p.stageCount!=2&&p.stageCount!=4&&!shadow_only) || !p.pStages || !unique)continue;
            const auto* front=vertex?vertex:mesh;
            if(!front || (vertex&&mesh) || (!fragment&&!shadow_only) || !stage(*front,mesh?VK_SHADER_STAGE_MESH_BIT_EXT:VK_SHADER_STAGE_VERTEX_BIT,mesh?"ms_main":"vs_main") ||
               (fragment&&(!stage(*fragment,VK_SHADER_STAGE_FRAGMENT_BIT,"fs_main")||impl_->shaders.find(fragment->module)==impl_->shaders.end())) ||
               impl_->shaders.find(front->module)==impl_->shaders.end())continue;
            if(mesh&&(p.stageCount!=2||control||evaluation))continue;
            if(p.stageCount==4 && (!control || !evaluation || !stage(*control,VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,"tcs_main") ||
               !stage(*evaluation,VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,"tes_main") ||
               impl_->shaders.find(control->module)==impl_->shaders.end() || impl_->shaders.find(evaluation->module)==impl_->shaders.end()))continue;
            const auto& vertex_info=impl_->shaders.at(front->module);
            // Complete four-stage identities are observational only, even with a pinned body VS.
            const auto kind=p.stageCount==2&&!mesh?vertex_info.kind:NativePipelineKind::other;
            Impl::Pipeline out;out.state={h,p.layout,kind,graphics_contract(p,kind),static_cast<std::uint32_t>(l->layouts.size())};
            out.point=VK_PIPELINE_BIND_POINT_GRAPHICS;out.layout_generation=l->generation;out.generation=impl_->generation();
            out.state.generation=out.generation;
            if(mesh){out.state.mesh_bytes=vertex_info.bytes;out.state.mesh_digest=vertex_info.digest;}
            else{out.state.vertex_bytes=vertex_info.bytes;out.state.vertex_digest=vertex_info.digest;}
            if(fragment){const auto& fragment_info=impl_->shaders.at(fragment->module);
                out.state.fragment_bytes=fragment_info.bytes;out.state.fragment_digest=fragment_info.digest;}
            out.state.graphics_stage_count=p.stageCount;out.state.oit_compatible=native_oit_pipeline_contract(p);
            out.state.shadow_compatible=shadow_only&&shadow_contract(p);
            out.raster_dynamic=raster_dynamic(p);
            if(control){const auto& s=impl_->shaders.at(control->module);out.state.tess_control_bytes=s.bytes;out.state.tess_control_digest=s.digest;}
            if(evaluation){const auto& s=impl_->shaders.at(evaluation->module);out.state.tess_evaluation_bytes=s.bytes;out.state.tess_evaluation_digest=s.digest;}
            impl_->store(impl_->pipelines,h,out);}});
}
void NativeState::compute_pipelines_created(std::uint32_t count,const VkComputePipelineCreateInfo* infos,const VkPipeline* handles)noexcept{
    if(impl_)impl_->write([&]{if(count>cap || (count && (!infos || !handles))){impl_->available=false;return;}
        for(std::uint32_t i=0;i<count;++i){const auto h=handles[i];impl_->pipelines.erase(h);impl_->raster_declarations.erase(h);if(!h)continue;const auto& p=infos[i];const auto* l=impl_->pipeline_layout(p.layout);
            if(!l || !simple(p,VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO) || p.basePipelineHandle || !stage(p.stage,VK_SHADER_STAGE_COMPUTE_BIT))continue;
            const auto s=impl_->shaders.find(p.stage.module);if(s==impl_->shaders.end())continue;
            const auto kind=s->second.kind==NativePipelineKind::lighting?NativePipelineKind::lighting:NativePipelineKind::other;
            if(kind==NativePipelineKind::lighting && !stage(p.stage,VK_SHADER_STAGE_COMPUTE_BIT,"cs_main"))continue;
            Impl::Pipeline out;out.state={h,p.layout,kind,true,static_cast<std::uint32_t>(l->layouts.size())};
            out.point=VK_PIPELINE_BIND_POINT_COMPUTE;out.layout_generation=l->generation;out.generation=impl_->generation();
            out.state.generation=out.generation;out.state.compute_bytes=s->second.bytes;out.state.compute_digest=s->second.digest;impl_->store(impl_->pipelines,h,out);}});
}
void NativeState::pipeline_destroyed(VkPipeline h)noexcept{if(impl_)impl_->write([&]{impl_->pipelines.erase(h);impl_->raster_declarations.erase(h);});}
void NativeState::image_created(VkImage h,const VkImageCreateInfo& i)noexcept{
    if(impl_)impl_->write([&]{impl_->sampled_image(h,i);impl_->images.erase(h);if(!(i.usage&(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))return;
        if(!h || i.sType!=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO || (i.flags&~VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) || i.imageType!=VK_IMAGE_TYPE_2D ||
        !i.extent.width || !i.extent.height || i.extent.depth!=1 || i.extent.width>32768 || i.extent.height>32768 || !i.mipLevels || !i.arrayLayers ||
        i.samples!=VK_SAMPLE_COUNT_1_BIT || !i.usage)return;
        Impl::Image image;image.info=i;image.generation=impl_->generation();image.info.pNext=nullptr;image.info.pQueueFamilyIndices=nullptr;image.info.queueFamilyIndexCount=0;
        if(i.pNext){const auto& list=*static_cast<const VkImageFormatListCreateInfo*>(i.pNext);
            if(list.sType!=VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO || list.pNext || list.viewFormatCount>16 || (list.viewFormatCount && !list.pViewFormats))return;
            if(list.viewFormatCount)image.view_formats.assign(list.pViewFormats,list.pViewFormats+list.viewFormatCount);}
        impl_->store(impl_->images,h,std::move(image));});
}
void NativeState::image_destroyed(VkImage h)noexcept{if(impl_)impl_->write([&]{impl_->images.erase(h);for(auto& m:impl_->sampled_images)m.erase(h);});}
void NativeState::view_created(VkImageView h,const VkImageViewCreateInfo& i)noexcept{
    if(impl_)impl_->write([&]{impl_->sampled_view(h,i);impl_->views.erase(h);impl_->attachment_views.erase(h);const auto image=impl_->images.find(i.image);
        if(!h || i.sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO || i.flags || image==impl_->images.end() || i.viewType!=VK_IMAGE_VIEW_TYPE_2D ||
           i.subresourceRange.baseMipLevel || i.subresourceRange.levelCount!=1 ||
           i.subresourceRange.baseArrayLayer>=image->second.info.arrayLayers || i.subresourceRange.layerCount!=1 || i.components.r!=VK_COMPONENT_SWIZZLE_IDENTITY ||
           i.components.g!=VK_COMPONENT_SWIZZLE_IDENTITY || i.components.b!=VK_COMPONENT_SWIZZLE_IDENTITY || i.components.a!=VK_COMPONENT_SWIZZLE_IDENTITY)return;
        const auto base=image->second.info.format;
        const bool srgb_pair=(base==VK_FORMAT_R8G8B8A8_UNORM && i.format==VK_FORMAT_R8G8B8A8_SRGB) || (base==VK_FORMAT_R8G8B8A8_SRGB && i.format==VK_FORMAT_R8G8B8A8_UNORM);
        if(i.format!=base && (!(image->second.info.flags&VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) || !srgb_pair))return;
        const auto& formats=image->second.view_formats;if(!formats.empty() && std::find(formats.begin(),formats.end(),i.format)==formats.end())return;
        auto usage=image->second.info.usage;
        if(i.pNext){const auto& narrowed=*static_cast<const VkImageViewUsageCreateInfo*>(i.pNext);
            if(narrowed.sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO || narrowed.pNext || !narrowed.usage || (narrowed.usage&~usage))return;
            usage=narrowed.usage;}
        const auto aspect=i.format==VK_FORMAT_D32_SFLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
        if(i.subresourceRange.aspectMask!=aspect)return;
        auto copy=i;copy.pNext=nullptr;const Impl::View observed{copy,image->second.generation,usage,impl_->generation()};
        impl_->store(impl_->attachment_views,h,observed);
        // Additional array layers are observational shadow attachments only.
        // Keep the strict visible-pass registry's layer-zero contract intact.
        if(i.subresourceRange.baseArrayLayer)return;
        impl_->store(impl_->views,h,observed);});
}
void NativeState::view_destroyed(VkImageView h)noexcept{if(impl_)impl_->write([&]{impl_->views.erase(h);impl_->attachment_views.erase(h);for(auto& m:impl_->sampled_views)m.erase(h);});}
void NativeState::command_begin(VkCommandBuffer h)noexcept{if(impl_)impl_->write([&]{if(h)impl_->store(impl_->commands,h,Impl::Command{},command_cap);});}
void NativeState::command_freed(std::uint32_t count,const VkCommandBuffer* handles)noexcept{
    if(impl_)impl_->write([&]{if(count>command_cap || (count && !handles)){impl_->available=false;return;}for(std::uint32_t i=0;i<count;++i)impl_->commands.erase(handles[i]);});
}
void NativeState::viewports_set(VkCommandBuffer command,std::uint32_t first,std::uint32_t count,const VkViewport* values)noexcept{
    if(impl_)impl_->write([&]{const auto found=impl_->commands.find(command);if(found==impl_->commands.end())return;
        auto& raster=found->second.raster;raster.viewport={};raster.viewport_known=false;
        if(first||count!=1||!values)return;
        raster.viewport=*values;raster.viewport_known=true;});
}
void NativeState::scissors_set(VkCommandBuffer command,std::uint32_t first,std::uint32_t count,const VkRect2D* values)noexcept{
    if(impl_)impl_->write([&]{const auto found=impl_->commands.find(command);if(found==impl_->commands.end())return;
        auto& raster=found->second.raster;raster.scissor={};raster.scissor_known=false;
        if(first||count!=1||!values)return;
        raster.scissor=*values;raster.scissor_known=true;});
}
void NativeState::raster_unobserved(VkCommandBuffer command,const char* name)noexcept{
    if(impl_)impl_->write([&]{const auto found=impl_->commands.find(command);if(found==impl_->commands.end())return;
        auto& raster=found->second.raster;
        if(!name||!std::strncmp(name,"vkCmdSetViewport",16)){raster.viewport={};raster.viewport_known=false;}
        if(!name||!std::strncmp(name,"vkCmdSetScissor",15)){raster.scissor={};raster.scissor_known=false;}});
}
NativeRasterState NativeState::raster(VkCommandBuffer command)const noexcept{
    return impl_?impl_->read<NativeRasterState>([&]{const auto found=impl_->commands.find(command);return found==impl_->commands.end()?NativeRasterState{}:found->second.raster;}):NativeRasterState{};
}
void NativeState::pipeline_bound(VkCommandBuffer command,VkPipelineBindPoint bind_point,VkPipeline pipeline)noexcept{
    if(impl_)impl_->write([&]{const auto cb=impl_->commands.find(command);if(cb==impl_->commands.end() || !point(bind_point))return;
        cb->second.observed_pipeline[pi(bind_point)]=pipeline;
        auto& b=cb->second.points[pi(bind_point)];const auto p=impl_->pipelines.find(pipeline);
        const bool valid=p!=impl_->pipelines.end()&&p->second.point==bind_point&&impl_->pipeline_valid(p->second,false);
        if(bind_point==VK_PIPELINE_BIND_POINT_GRAPHICS){auto& raster=cb->second.raster;const auto declaration=impl_->raster_declarations.find(pipeline);
            if(declaration==impl_->raster_declarations.end()||!declaration->second[0]){raster.viewport={};raster.viewport_known=false;}
            if(declaration==impl_->raster_declarations.end()||!declaration->second[1]){raster.scissor={};raster.scissor_known=false;}
        }
        if(!valid){b={};return;}
        if(b.complete && (b.layout!=p->second.state.layout || b.layout_generation!=p->second.layout_generation)){b.complete=false;b.sets.clear();}
        b.pipeline=pipeline;b.pipeline_generation=p->second.generation;});
}
void NativeState::sets_bound(VkCommandBuffer command,VkPipelineBindPoint bind_point,VkPipelineLayout layout,std::uint32_t first,std::uint32_t count,
    const VkDescriptorSet* handles,std::uint32_t dynamic_count,const std::uint32_t*)noexcept{
    if(impl_)impl_->write([&]{const auto cb=impl_->commands.find(command);if(cb==impl_->commands.end() || !point(bind_point))return;
        cb->second.observed_layout[pi(bind_point)]=layout;cb->second.observed_sets[pi(bind_point)]={first,count,dynamic_count};
        auto& b=cb->second.points[pi(bind_point)];b.complete=false;b.sets.clear();b.layout=VK_NULL_HANDLE;b.layout_generation=0;
        const auto* l=impl_->pipeline_layout(layout);if(!l || first || count!=l->layouts.size() || count>32 || dynamic_count || (count && !handles))return;
        // A descriptor bind is independent of the pipeline currently bound.
        // bound() checks the pair when used, including a later pipeline bind.
        for(std::uint32_t i=0;i<count;++i){const auto s=impl_->sets.find(handles[i]);
            if(s==impl_->sets.end() || !impl_->set_valid(s->second) || s->second.layout.handle!=l->layouts[i].handle || s->second.layout.generation!=l->layouts[i].generation){b.sets.clear();return;}
            b.sets.emplace_back(s->first,s->second.generation);}
        b.layout=layout;b.layout_generation=l->generation;b.complete=true;});
}
NativeBoundState NativeState::bound(VkCommandBuffer command,VkPipelineBindPoint bind_point)const noexcept{
    if(!impl_)return {};
    return impl_->read<NativeBoundState>([&]{const auto c=impl_->commands.find(command);return c!=impl_->commands.end() && point(bind_point)?impl_->bound(c->second.points[pi(bind_point)],bind_point):NativeBoundState{};});
}
NativeBoundState NativeState::observed_bound(VkCommandBuffer command,VkPipelineBindPoint bind_point)const noexcept{
    if(!impl_)return {};
    return impl_->read<NativeBoundState>([&]{const auto c=impl_->commands.find(command);return c!=impl_->commands.end() && point(bind_point)?impl_->bound(c->second.points[pi(bind_point)],bind_point,false):NativeBoundState{};});
}
std::string NativeState::binding_diagnostic(VkCommandBuffer command,VkPipelineBindPoint bind_point)const noexcept{
    if(!impl_)return {};
    return impl_->read<std::string>([&]{
        std::ostringstream s;s<<"available="<<impl_->available.load()<<" maps(buffer,layout,pipelineLayout,set,shader,pipeline,image,view,command)="
            <<impl_->buffers.size()<<","<<impl_->layouts.size()<<","<<impl_->pipeline_layouts.size()<<","<<impl_->sets.size()<<","<<impl_->shaders.size()<<","<<impl_->pipelines.size()<<","<<impl_->images.size()<<","<<impl_->views.size()<<","<<impl_->commands.size();
        const auto c=impl_->commands.find(command);s<<" command="<<(c!=impl_->commands.end())<<" point="<<bind_point;
        if(c!=impl_->commands.end()&&point(bind_point)){
            const auto index=pi(bind_point);const auto& b=c->second.points[index];const auto observed=c->second.observed_pipeline[index];const auto p=impl_->pipelines.find(observed);
            s<<" currentPipeline="<<observed<<" tracked="<<(p!=impl_->pipelines.end());
            if(p!=impl_->pipelines.end())s<<" supported="<<p->second.state.supported<<" kind="<<static_cast<unsigned>(p->second.state.kind)<<" pipelineValid="<<impl_->pipeline_valid(p->second)<<" pipelineLayout="<<p->second.state.layout<<" layoutKnown="<<bool(impl_->pipeline_layout(p->second.state.layout))<<" expectedSets="<<p->second.state.set_count;
            s<<" trackedBindingPipeline="<<b.pipeline<<" generationMatch="<<(p!=impl_->pipelines.end()&&p->second.generation==b.pipeline_generation)
                <<" requestedLayout="<<c->second.observed_layout[index]<<" requestedFirstCountDynamic="<<c->second.observed_sets[index][0]<<","<<c->second.observed_sets[index][1]<<","<<c->second.observed_sets[index][2]
                <<" bindingLayout="<<b.layout<<" bindingLayoutKnown="<<bool(impl_->pipeline_layout(b.layout))<<" completeSets="<<b.complete<<" retainedSets="<<b.sets.size()<<" valid="<<impl_->bound(b,bind_point).valid;
            const auto* pl=impl_->pipeline_layout(b.layout);
            for(std::size_t i=0;i<b.sets.size();++i){const auto set=impl_->sets.find(b.sets[i].first);if(set==impl_->sets.end()||set->second.generation!=b.sets[i].second||!impl_->set_valid(set->second)||!pl||i>=pl->layouts.size()||set->second.layout.handle!=pl->layouts[i].handle||set->second.layout.generation!=pl->layouts[i].generation){s<<" firstInvalidSet="<<i<<" exists="<<(set!=impl_->sets.end());break;}}
        }
        for(unsigned i=0;i<impl_->native_pipeline_observations.size();++i)if(!impl_->native_pipeline_observations[i].empty())s<<" | "<<impl_->native_pipeline_observations[i];
        return s.str().substr(0,4096);
    },true);
}
NativeBufferBinding NativeState::buffer_binding(const NativeBoundState& state,std::uint32_t set,std::uint32_t binding)const noexcept{
    if(!impl_)return {};
    return impl_->read<NativeBufferBinding>([&]{return impl_->source(state,set,binding);});
}
NativeBufferBinding NativeState::observed_buffer_binding(const NativeBoundState& state,std::uint32_t set,std::uint32_t binding)const noexcept{
    if(!impl_)return {};
    return impl_->read<NativeBufferBinding>([&]{return impl_->source(state,set,binding,false);});
}
std::string NativeState::buffer_binding_diagnostic(const NativeBoundState& state,std::uint32_t index,std::uint32_t binding)const noexcept{
    if(!impl_)return {};
    return impl_->read<std::string>([&]{
        std::ostringstream s;s<<"available="<<impl_->available.load()<<" source="<<(impl_->available&&bool(impl_->source(state,index,binding)))<<" boundValid="<<state.valid<<" setIndex="<<index<<" binding="<<binding;
        const auto handle=index<state.sets.size()?state.sets[index]:VK_NULL_HANDLE;const auto set=impl_->sets.find(handle);s<<" set="<<handle<<" setExists="<<(set!=impl_->sets.end());
        if(set==impl_->sets.end())return s.str();
        const auto& value=set->second;const auto* layout=impl_->layout(value.layout);s<<" layoutKnown="<<bool(layout)<<" setPoisoned="<<value.poisoned<<" setGeneration="<<value.generation<<" setVersion="<<value.version;
        if(layout){const auto decl=layout->bindings.find(binding);s<<" declared="<<(decl!=layout->bindings.end());if(decl!=layout->bindings.end())s<<" type="<<decl->second.descriptorType<<" declaredCount="<<decl->second.descriptorCount;}
        const auto content=value.contents.find(binding);s<<" contentsExists="<<(content!=value.contents.end());
        if(content!=value.contents.end()){
            s<<" contentsKnown="<<content->second.known<<" contentsPoisoned="<<content->second.poisoned<<" cells="<<content->second.cells.size();
            if(!content->second.cells.empty()){const auto& cell=content->second.cells.front();const auto buffer=impl_->buffers.find(cell.info.buffer);s<<" firstBuffer="<<cell.info.buffer<<" bufferExists="<<(buffer!=impl_->buffers.end())<<" storedGeneration="<<cell.generation<<" offset="<<cell.info.offset<<" range="<<cell.info.range;if(buffer!=impl_->buffers.end())s<<" currentGeneration="<<buffer->second.generation<<" size="<<buffer->second.size<<" usage="<<buffer->second.usage;}
        }
        constexpr std::array<const char*,10> reasons{"none","invalidHeader","invalidSpan","missingBufferInfo","missingBuffer","invalidCell","partialUnknownContents","copyRejected","templateUnknownOrMalformed","bulkInvalidUpdate"};
        const auto& r=value.rejected_update;s<<" lastRejected(op,binding,first,count,type)="<<r[0]<<","<<r[1]<<","<<r[2]<<","<<r[3]<<","<<r[4]<<" reason="<<(r[5]<reasons.size()?reasons[r[5]]:"unknown");return s.str().substr(0,2048);
    },true);
}
bool NativeState::live(const NativeBufferBinding& ref)const noexcept{
    if(!impl_)return false;
    return impl_->read<bool>([&]{if(!ref)return false;const auto b=impl_->buffers.find(ref.info.buffer);
        if(b==impl_->buffers.end() || b->second.generation!=ref.generation || b->second.size!=ref.size || b->second.usage!=ref.usage)return false;
        if(!ref.set)return !ref.set_generation && !ref.set_version && !ref.declaration_flags && ref.info.offset==0 && ref.info.range==ref.size;
        const auto s=impl_->sets.find(ref.set);if(s==impl_->sets.end() || !impl_->set_valid(s->second) || s->second.generation!=ref.set_generation || s->second.version!=ref.set_version)return false;
        const auto* declaration=impl_->layout(s->second.layout);const auto flags=declaration->flags.find(ref.binding);
        if(flags==declaration->flags.end()||flags->second!=ref.declaration_flags)return false;
        const auto content=s->second.contents.find(ref.binding);if(content==s->second.contents.end() || !content->second.known || content->second.cells.empty())return false;
        for(const auto& element:content->second.cells){const auto it=impl_->buffers.find(element.info.buffer);
            if(it==impl_->buffers.end() || it->second.generation!=element.generation)return false;}
        const auto& c=content->second.cells.front();return c.generation==ref.generation && c.info.buffer==ref.info.buffer && c.info.offset==ref.info.offset && c.info.range==ref.info.range;});
}
NativePassState NativeState::rendering(const VkRenderingInfo& i)const noexcept{
    if(!impl_)return {};
    return impl_->read<NativePassState>([&]{
        if(i.sType!=VK_STRUCTURE_TYPE_RENDERING_INFO || i.pNext || i.flags || i.viewMask || i.layerCount!=1 || i.renderArea.offset.x || i.renderArea.offset.y ||
           !i.renderArea.extent.width || !i.renderArea.extent.height || i.pStencilAttachment || !i.pDepthAttachment || !i.pColorAttachments)return NativePassState{};
        const bool vis=i.colorAttachmentCount==vis_formats.size(),gbuf=i.colorAttachmentCount==gbuf_formats.size();if(!vis && !gbuf)return NativePassState{};
        const auto attachment=[&](const VkRenderingAttachmentInfo& a,VkFormat format,bool depth){
            if(a.sType!=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO || a.pNext || a.resolveMode!=VK_RESOLVE_MODE_NONE || a.resolveImageView || a.resolveImageLayout!=VK_IMAGE_LAYOUT_UNDEFINED || !a.imageView)return false;
            const auto v=impl_->views.find(a.imageView);if(v==impl_->views.end() || v->second.info.format!=format)return false;
            const auto image=impl_->images.find(v->second.info.image);if(image==impl_->images.end() || image->second.generation!=v->second.image_generation ||
               image->second.info.extent.width!=i.renderArea.extent.width || image->second.info.extent.height!=i.renderArea.extent.height || image->second.info.samples!=VK_SAMPLE_COUNT_1_BIT)return false;
            if(depth){if(!(v->second.usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))return false;
                return vis?((a.imageLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || a.imageLayout==VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) && (a.loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR || a.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD) && a.storeOp==VK_ATTACHMENT_STORE_OP_STORE):
                    (a.imageLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && a.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD && a.storeOp==VK_ATTACHMENT_STORE_OP_NONE);}
            return (v->second.usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) && a.imageLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && a.storeOp==VK_ATTACHMENT_STORE_OP_STORE &&
                (vis?a.loadOp==i.pDepthAttachment->loadOp:(a.loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR || a.loadOp==VK_ATTACHMENT_LOAD_OP_DONT_CARE));};
        if(!attachment(*i.pDepthAttachment,VK_FORMAT_D32_SFLOAT,true))return NativePassState{};
        for(std::uint32_t c=0;c<i.colorAttachmentCount;++c)if(!attachment(i.pColorAttachments[c],vis?vis_formats[c]:gbuf_formats[c],false))return NativePassState{};
        return NativePassState{vis?NativePipelineKind::visibility:NativePipelineKind::gbuffer,i.renderArea.extent,i.pDepthAttachment->imageView,true};});
}
} // namespace evrm
