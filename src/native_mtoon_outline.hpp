#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "native_mtoon_raster_plan.hpp"
#include "mtoon_outline_policy.hpp"
namespace evrm {
enum class NativeMToonOutlineStatus {not_requested,prepared,unsupported,invalid};
struct NativeMToonOutlineDraw {std::uint32_t draw_index=0,material=0;VkCullModeFlags cull=VK_CULL_MODE_FRONT_BIT;std::uint32_t reserved=0;NativeMToonRasterUniform uniform;};
struct NativeMToonOutlinePlan {
    NativeMToonOutlineStatus status=NativeMToonOutlineStatus::not_requested;
    std::shared_ptr<const NativeMToonRequest> request;std::uint32_t triangles=0,cull_modes=0;
    std::vector<NativeMToonOutlineDraw> draws;
};
// CPU selection/rank preparation only. The parent still joins the exact posed
// geometry/history, uploaded leaves and admitted native visibility/GBuffer pair.
inline NativeMToonOutlinePlan make_native_mtoon_outline_plan(const std::shared_ptr<const NativeMToonRequest>& request,
    const NativeMToonRasterPlan& raster,std::uint32_t first)noexcept {
    if(!request)return {};
    try{
        const auto require=[](bool b){if(!b)throw std::invalid_argument("Invalid native outline preparation");};
        require(raster.status!=NativeMToonRasterStatus::invalid&&bool(request->plan())&&raster.draws.size()<=4096);
        if(raster.status!=NativeMToonRasterStatus::prepared)return {};
        std::uint32_t end=0;for(const auto& d:raster.draws){const auto& u=d.uniform;
            require(u.domain[0]&&u.domain[1]&&u.geometry[3]&&u.geometry[3]%3==0&&u.domain[3]==u.geometry[3]/3&&
                u.domain[2]>=end&&u.domain[2]<=0x7fffffffu&&u.domain[3]<=0x7fffffffu-u.domain[2]&&u.uv[11]<=1&&std::isfinite(u.time[0]));end=u.domain[2]+u.domain[3];}
        NativeMToonOutlinePlan out;
        for(const auto& d:raster.draws){if(!d.uniform.uv[11])continue;
            const auto& p=request->plan();const auto found=std::find_if(p.draws.begin(),p.draws.end(),[&](const auto& entry){return entry.draw_index==d.draw_index;});
            require(found!=p.draws.end()&&found->material>=0&&std::size_t(found->material)<p.materials.size());
            const auto& prepared=p.materials[found->material];if(!prepared.outline)continue;
            require(prepared.status==MToonResourceStatus::prepared_cpu&&std::size_t(found->material)<request->avatar()->materials.size());
            const auto& material=request->avatar()->materials[found->material].mtoon;
            require(material&&material->outline_mode!=vrm::MToonOutlineMode::None&&std::isfinite(material->outline_width)&&material->outline_width>0);
            if(material->render_mode!=vrm::MToonRenderMode::Opaque&&material->render_mode!=vrm::MToonRenderMode::Cutout)return {NativeMToonOutlineStatus::unsupported,{},{},{},{}};
            const auto cull=unsigned(mtoon_outline_cull(*material));require(cull<=2&&first<=0x7fffffffu&&first>=end&&out.triangles<=0x7fffffffu-first&&d.uniform.domain[3]<=0x7fffffffu-first-out.triangles);
            NativeMToonOutlineDraw draw;draw.draw_index=d.draw_index;draw.material=unsigned(found->material);draw.cull=VkCullModeFlags(cull);draw.uniform=d.uniform;draw.uniform.domain[2]=first+out.triangles;
            out.triangles+=draw.uniform.domain[3];out.cull_modes|=1u<<cull;out.draws.push_back(draw);
        }
        if(!out.draws.empty()){out.status=NativeMToonOutlineStatus::prepared;out.request=request;}return out;
    }catch(...){return {NativeMToonOutlineStatus::invalid,{},{},{},{}};}
}
struct NativeMToonOutlineCall {
    // Private geometry/frame, authored material, and outline draw/history sets.
    std::array<VkDescriptorSet,3> sets{};std::uint32_t first_index=0,index_count=0;VkCullModeFlags cull=VK_CULL_MODE_FRONT_BIT;
};
// Native-format opaque/masked outline pair, prepared only when requested.
// Caller retains shaders/layout, exact expanded/deformed/history/UV resources
// and all descriptor tuples through original retirement. It supplies matching
// native dynamic viewport/scissor/stencil state. No caster/transparent pass,
// source/scene authority, push changes, waits or submissions are introduced.
class NativeMToonOutlinePipelines {
public:
    NativeMToonOutlinePipelines(VkDevice,PFN_vkGetDeviceProcAddr,VkPipelineLayout,
        VkShaderModule vertex,VkShaderModule visibility,VkShaderModule gbuffer,std::uint32_t cull_modes);
    ~NativeMToonOutlinePipelines();
    NativeMToonOutlinePipelines(const NativeMToonOutlinePipelines&)=delete;
    NativeMToonOutlinePipelines& operator=(const NativeMToonOutlinePipelines&)=delete;
    bool record(VkCommandBuffer,bool gbuffer,const NativeMToonOutlineCall*,std::uint32_t,
        VkPipeline original,VkPipelineLayout compatible,std::uint32_t native_set_count,const VkDescriptorSet*)const noexcept;
    void quarantine()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
