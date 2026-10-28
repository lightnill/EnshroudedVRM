#include "native_material_plan.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
namespace evrm {
bool make_native_material_plan(const vrm::Avatar& avatar,const NativeAvatarPose& pose,NativeMaterialPlan& out)noexcept {
    out={};
    const auto reject=[&](NativeMaterialPlanFailure why){out={};out.failure=why;return false;};
    try{
        if(!pose||pose.vertices.empty()||pose.vertices.size()>2000000||pose.indices.empty()||pose.indices.size()>6000000||pose.draws.empty()||pose.draws.size()>4096)
            return reject(NativeMaterialPlanFailure::geometry);
        NativeMaterialPlan plan;bool first=true;std::uint64_t work=0;
        for(std::size_t draw_index=0;draw_index<pose.draws.size();++draw_index){
            const auto& draw=pose.draws[draw_index];
            if(draw.index_count<3||draw.index_count%3||draw.first_index>pose.indices.size()||draw.index_count>pose.indices.size()-draw.first_index)
                return reject(NativeMaterialPlanFailure::draw_range);
            work+=draw.index_count;if(work>6000000)return reject(NativeMaterialPlanFailure::geometry);
            if(draw.material< -1||(draw.material>=0&&static_cast<std::size_t>(draw.material)>=avatar.materials.size()))return reject(NativeMaterialPlanFailure::material);
            const vrm::Material fallback{};const auto& material=draw.material<0?fallback:avatar.materials[draw.material];
            if(material.alpha!=vrm::AlphaMode::Opaque&&material.alpha!=vrm::AlphaMode::Mask&&material.alpha!=vrm::AlphaMode::Blend)return reject(NativeMaterialPlanFailure::alpha_mode);
            for(const auto value:material.base_color)if(!std::isfinite(value))return reject(NativeMaterialPlanFailure::nonfinite);
            const bool blend=material.alpha==vrm::AlphaMode::Blend;
            (blend?plan.blend_draws:plan.opaque_draws).push_back(static_cast<std::uint32_t>(draw_index));
            if(blend)for(unsigned axis=0;axis<3;++axis)plan.base_max[axis]=std::max(plan.base_max[axis],std::abs(material.base_color[axis]));
            for(std::uint32_t at=0;at<draw.index_count;++at){
                const auto index=pose.indices[draw.first_index+at];if(index>=pose.vertices.size())return reject(NativeMaterialPlanFailure::draw_range);
                if(!blend)continue;
                const auto& vertex=pose.vertices[index];
                for(const auto position:vertex.position)if(!std::isfinite(position))return reject(NativeMaterialPlanFailure::nonfinite);
                for(const auto uv:vertex.uv){if(!std::isfinite(uv))return reject(NativeMaterialPlanFailure::nonfinite);if(std::abs(uv)>16384.f)return reject(NativeMaterialPlanFailure::uv_bounds);}
                for(unsigned axis=0;axis<3;++axis){
                    plan.blend_min[axis]=first?vertex.position[axis]:std::min(plan.blend_min[axis],vertex.position[axis]);
                    plan.blend_max[axis]=first?vertex.position[axis]:std::max(plan.blend_max[axis],vertex.position[axis]);
                }
                first=false;
            }
        }
        plan.mixed=!plan.blend_draws.empty();
        if(plan.mixed&&plan.opaque_draws.empty())return reject(NativeMaterialPlanFailure::all_blend);
        if(plan.mixed)plan.base_max[3]=1; // PS saturates alpha; all material alpha factors were checked finite.
        out=std::move(plan);return true;
    }catch(...){return reject(NativeMaterialPlanFailure::allocation);}
}
}
