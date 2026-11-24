#pragma once
#include "mtoon_gpu_material.hpp"
#include "mtoon_policy.hpp"
#include <limits>
#include <map>
#include <tuple>
#include <vector>

namespace evrm {
// CPU preparation only: none of these states attest an upload or native scene.
enum class MToonResourceStatus {not_requested,missing_metadata,prepared_cpu};
struct NativeMToonPlanMaterial {
    MToonResourceStatus status=MToonResourceStatus::not_requested;
    std::uint32_t textures=0;
    bool outline=false;
    MToonGpuMaterial uniform{};
};
struct NativeMToonImageRequest {int image=-1;bool srgb=false;};
struct NativeMToonDrawUv {
    std::uint32_t vertex_base=0,vertex_count=0,draw_index=0;
    std::uint32_t node=0,mesh=0,primitive=0;
    int material=-1;
    std::array<std::uint32_t,vrm::mtoon_texture_count> uv_offsets{};
};
struct NativeMToonResourcePlan {
    std::vector<NativeMToonPlanMaterial> materials;
    std::vector<NativeMToonImageRequest> images;
    std::vector<NativeMToonDrawUv> draws;
    std::vector<std::array<float,2>> uv;
    std::string error;
    explicit operator bool()const noexcept{return error.empty();}
};
namespace mtoon_plan_detail {
inline bool requested(AvatarShaderPreset p)noexcept{
    return avatar_preset_is_toon(p);
}
inline bool nonzero(const std::array<float,3>& v)noexcept{return v[0]!=0||v[1]!=0||v[2]!=0;}
inline std::uint32_t texture_mask(const vrm::MToonMaterial& m,bool outline)noexcept{
    std::uint32_t mask=0;
    const auto add=[&](unsigned slot,bool active){if(active&&m.textures[slot].texture>=0)mask|=1u<<slot;};
    const bool legacy=m.version==vrm::MToonVersion::Vrm0;
    add(0,true);add(1,nonzero(m.shade_color));add(2,m.normal_scale!=0);
    add(3,legacy&&m.receive_shadow!=0);add(4,legacy&&m.shading_grade!=0);
    add(5,nonzero(m.emission_color));add(7,legacy||nonzero(m.matcap_color));
    add(6,nonzero(m.rim_color)||(!legacy&&(mask&(1u<<7))));
    add(8,outline);add(10,!legacy&&m.shade_shift_texture_scale!=0);
    const auto animated=mask&~((1u<<7)|(legacy?(1u<<8):0u));
    add(9,animated&&(m.uv_scroll[0]!=0||m.uv_scroll[1]!=0||m.uv_rotation!=0));
    return mask;
}
}
// MToon + Outline draws authored outlines wider by one derived factor. Only the
// uploaded copy changes: the authored width, mode, width map and legacy maximum
// distance are read, never written. The width map still multiplies the result,
// so painted thin and zero regions keep their proportion and stay zero.
// The factor never lifts a width past the ceiling: one centimetre in world
// mode, 0.01 of clip space in screen mode, in the authored unit of each
// version (shaders/mtoon_outline.hlsl). A width already authored above the
// ceiling is uploaded unchanged, so this can widen but never narrow.
inline constexpr float native_mtoon_outline_width_factor=1.5f;
inline float native_mtoon_outline_width_ceiling(const vrm::MToonMaterial& m)noexcept{
    if(m.version==vrm::MToonVersion::Vrm0)return 1.f;
    return m.outline_mode==vrm::MToonOutlineMode::Screen?.005f:.01f;
}
inline float native_mtoon_outline_width(const vrm::MToonMaterial& m)noexcept{
    const float authored=m.outline_width;
    if(!std::isfinite(authored)||authored<=0)return authored;
    const float widened=authored*native_mtoon_outline_width_factor;
    if(!std::isfinite(widened))return authored;
    return std::max(authored,std::min(widened,native_mtoon_outline_width_ceiling(m)));
}
inline NativeMToonResourcePlan make_native_mtoon_resource_plan(const vrm::Avatar& avatar,
    const AvatarAppearance* appearance,std::uint32_t image_dimension_limit)noexcept{
    NativeMToonResourcePlan out;
    try{
        bool any=false;
        for(std::size_t i=0;i<avatar.materials.size();++i)
            if(mtoon_plan_detail::requested(avatar_material_preset(appearance,i))){any=true;break;}
        if(!any)return out; // Old presets must not allocate even empty optional banks.
        const auto require=[](bool ok,const char* message){if(!ok)throw std::invalid_argument(message);};
        const auto reference=[](int index,std::size_t size){return index>=0&&std::size_t(index)<size;};
        const auto add=[&](std::size_t& count,std::size_t amount,std::size_t limit){
            require(count<=limit&&amount<=limit-count,"MToon geometry exceeds native limits");count+=amount;
        };
        // Match NativeAvatarPose's bounded source and instanced geometry layout.
        require(avatar.materials.size()<=4096&&avatar.images.size()<=4096&&avatar.meshes.size()<=4096&&avatar.nodes.size()<=2048,
            "MToon metadata exceeds native limits");
        std::size_t source_vertices=0,source_indices=0,source_draws=0;
        for(const auto& mesh:avatar.meshes){add(source_draws,mesh.primitives.size(),4096);
            for(const auto& p:mesh.primitives){add(source_vertices,p.vertices.size(),2000000);add(source_indices,p.indices.size(),6000000);}}
        std::vector<bool> used(avatar.materials.size(),false);
        std::size_t vertex_count=0,index_count=0,draw_count=0;
        for(const auto& node:avatar.nodes){
            require(node.mesh==-1||reference(node.mesh,avatar.meshes.size()),"Invalid MToon mesh node");
            if(!node.active||node.mesh<0)continue;
            for(const auto& p:avatar.meshes[node.mesh].primitives){
                require(p.material==-1||reference(p.material,avatar.materials.size()),"Invalid MToon primitive material");
                require(!p.vertices.empty()&&!p.indices.empty()&&p.indices.size()%3==0,"Invalid MToon triangle layout");
                add(vertex_count,p.vertices.size(),2000000);add(index_count,p.indices.size(),6000000);add(draw_count,1,4096);
                for(const auto index:p.indices)require(index<p.vertices.size(),"MToon index outside native primitive");
                if(p.material>=0)used[p.material]=true;
            }
        }
        out.materials.resize(avatar.materials.size());bool prepared=false;
        for(std::size_t i=0;i<avatar.materials.size();++i){
            const auto preset=avatar_material_preset(appearance,i);
            if(!used[i]||!mtoon_plan_detail::requested(preset))continue;
            auto& plan=out.materials[i];const auto& material=avatar.materials[i];
            const auto selection=mtoon_material_selection(material,preset,true);
            if(!selection.ready){plan.status=MToonResourceStatus::missing_metadata;continue;}
            const auto& m=*material.mtoon;plan.uniform=pack_mtoon_material(m);
            plan.uniform.shade[3]=avatar_preset_is_nilltoon(preset)?1.f:0.f; // owned profile padding; source metadata unchanged
            // A derived upload only: source colour and authored width/alpha stay immutable.
            if(selection.outline&&avatar_material_outline_color(appearance,i)==AvatarOutlineColor::Black)
                plan.uniform.outline[0]=plan.uniform.outline[1]=plan.uniform.outline[2]=0;
            // selection.outline already requires the outline preset, an authored
            // World/Screen mode and a positive finite width. Plain MToon, None
            // and zero keep the authored value. Transparent and menu-converted
            // layers carry this number too but no outline stage draws them.
            if(selection.outline)plan.uniform.outline_settings[0]=native_mtoon_outline_width(m);
            plan.textures=mtoon_plan_detail::texture_mask(m,selection.outline);plan.outline=selection.outline;
            for(std::size_t slot=0;slot<vrm::mtoon_texture_count;++slot){
                auto& texture=plan.uniform.textures[slot];const bool active=(plan.textures&(1u<<slot))!=0;
                texture.info[1]=active?1u:0u;
                if(!active)continue;
                const auto& source=avatar.textures.at(std::size_t(m.textures[slot].texture));
                require(reference(source.image,avatar.images.size()),"Invalid MToon texture image");
                const auto& image=avatar.images[source.image];
                require(image_dimension_limit&&image.width>0&&image.height>0&&
                    std::uint32_t(image.width)<=image_dimension_limit&&std::uint32_t(image.height)<=image_dimension_limit,
                    "MToon image exceeds device dimensions");
                const auto w=std::size_t(image.width),h=std::size_t(image.height);
                require(w<=std::numeric_limits<std::size_t>::max()/4/h&&image.rgba.size()==w*h*4,
                    "Invalid MToon decoded image layout");
                const bool srgb=texture.info[3]!=0;
                const auto found=std::find_if(out.images.begin(),out.images.end(),[&](const auto& request){return request.image==source.image&&request.srgb==srgb;});
                if(found==out.images.end())out.images.push_back({source.image,srgb});
            }
            plan.status=MToonResourceStatus::prepared_cpu;prepared=true;
        }
        if(!prepared)return out;
        // Share immutable raw UV channels across repeated mesh instances, while
        // retaining each instance's actual native flattened vertex/draw base.
        std::map<std::tuple<std::size_t,std::size_t,int>,std::uint32_t> offsets;
        std::uint32_t vertex_base=0,draw_index=0;
        for(std::size_t n=0;n<avatar.nodes.size();++n){const auto& node=avatar.nodes[n];if(!node.active||node.mesh<0)continue;
            const auto& mesh=avatar.meshes[node.mesh];
            for(std::size_t p=0;p<mesh.primitives.size();++p){const auto& primitive=mesh.primitives[p];
                if(primitive.material>=0&&out.materials[primitive.material].status==MToonResourceStatus::prepared_cpu){
                    const auto& material=out.materials[primitive.material];NativeMToonDrawUv draw;
                    draw.vertex_base=vertex_base;draw.vertex_count=static_cast<std::uint32_t>(primitive.vertices.size());draw.draw_index=draw_index;
                    draw.node=static_cast<std::uint32_t>(n);draw.mesh=static_cast<std::uint32_t>(node.mesh);draw.primitive=static_cast<std::uint32_t>(p);draw.material=primitive.material;
                    draw.uv_offsets.fill(std::numeric_limits<std::uint32_t>::max());
                    for(std::size_t slot=0;slot<vrm::mtoon_texture_count;++slot){
                        if(!(material.textures&(1u<<slot))||slot==7)continue; // Matcap uses view-space UVs.
                        const int texcoord=static_cast<int>(material.uniform.textures[slot].info[0]);
                        const auto key=std::make_tuple(std::size_t(node.mesh),p,texcoord);auto found=offsets.find(key);
                        if(found==offsets.end()){
                            const auto channel=std::find_if(primitive.mtoon_uvs.begin(),primitive.mtoon_uvs.end(),[&](const auto& uv){return uv.texcoord==texcoord;});
                            require(channel!=primitive.mtoon_uvs.end()&&channel->values.size()==primitive.vertices.size(),"Missing MToon raw UV channel");
                            for(const auto& uv:channel->values)for(float value:uv)require(std::isfinite(value),"Nonfinite MToon raw UV");
                            require(out.uv.size()<=std::numeric_limits<std::uint32_t>::max()-channel->values.size(),"MToon UV offset overflow");
                            const auto offset=static_cast<std::uint32_t>(out.uv.size());
                            out.uv.insert(out.uv.end(),channel->values.begin(),channel->values.end());found=offsets.emplace(key,offset).first;
                        }
                        draw.uv_offsets[slot]=found->second;
                    }
                    out.draws.push_back(draw);
                }
                vertex_base+=static_cast<std::uint32_t>(primitive.vertices.size());++draw_index;
            }
        }
        return out;
    }catch(const std::exception& e){out={};out.error=e.what();return out;}
    catch(...){out={};out.error="MToon plan allocation failed";return out;}
}
}
