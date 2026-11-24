#pragma once
#include "native_mtoon_request.hpp"
#include "native_avatar_pose.hpp"
#include <cstddef>
#include <cmath>

namespace evrm {
// Shared with native_mtoon_raster.hlsli. This is prepared CPU draw data, never
// resource/pose provenance, an upload receipt or native scene admission. The
// caller joins the request's avatar to its current model and keeps the actual
// geometry/frame/UV buffers leased through every GPU consumer.
struct alignas(16) NativeMToonRasterUniform {
    std::array<std::uint32_t,4> domain{}; // width,height,first triangle rank,triangle count
    std::array<std::uint32_t,4> geometry{}; // flattened vertex base/count,index first/count
    std::array<std::uint32_t,12> uv{}; // eleven raw UV offsets; slot11 is selected0/1
    std::array<float,4> time{}; // same animation seconds in all passes; reserved0
};
static_assert(sizeof(NativeMToonRasterUniform)==96&&alignof(NativeMToonRasterUniform)==16);
static_assert(offsetof(NativeMToonRasterUniform,geometry)==16&&offsetof(NativeMToonRasterUniform,uv)==32&&offsetof(NativeMToonRasterUniform,time)==80);
struct NativeMToonRasterDraw {std::uint32_t draw_index=0;std::array<std::uint32_t,3> reserved{};NativeMToonRasterUniform uniform;};
enum class NativeMToonRasterStatus {not_requested,prepared,invalid};
struct NativeMToonRasterPlan {
    NativeMToonRasterStatus status=NativeMToonRasterStatus::not_requested;
    std::uint32_t triangles=0;
    std::vector<NativeMToonRasterDraw> draws;
    explicit operator bool()const noexcept{return status!=NativeMToonRasterStatus::invalid;}
};
inline NativeMToonRasterPlan make_native_mtoon_raster_plan(const std::shared_ptr<const NativeMToonRequest>& request,
    const NativeAvatarPose& geometry,const std::vector<std::uint32_t>& order,std::uint32_t width,std::uint32_t height,float seconds)noexcept {
    if(!request)return {};
    try{
        const auto require=[](bool value){if(!value)throw std::invalid_argument("Invalid MToon raster geometry/UV plan");};
        const auto& source=request->plan();
        require(bool(geometry)&&bool(source)&&geometry.draws.size()<=4096&&geometry.vertices.size()<=2000000&&
            geometry.indices.size()<=12000000&&order.size()<=4096&&width&&height&&std::isfinite(seconds));
        std::vector<bool> seen(geometry.draws.size());bool selected=false;
        for(auto index:order){require(index<geometry.draws.size()&&!seen[index]);seen[index]=true;
            const auto material=geometry.draws[index].material;
            if(material>=0&&std::size_t(material)<source.materials.size()&&source.materials[material].status==MToonResourceStatus::prepared_cpu)selected=true;
        }
        if(!selected)return {}; // No identity buffer or draw packets for an ordinary view.
        NativeMToonRasterPlan out;out.status=NativeMToonRasterStatus::prepared;out.draws.reserve(order.size());
        for(auto index:order){const auto& native=geometry.draws[index];
            require(native.index_count&&native.index_count%3==0&&native.first_index<=geometry.indices.size()&&
                native.index_count<=geometry.indices.size()-native.first_index&&native.material>=-1&&
                (native.material<0||std::size_t(native.material)<request->avatar()->materials.size()));
            const auto triangles=native.index_count/3;require(out.triangles<=0x7fffffffu-triangles);
            NativeMToonRasterDraw draw;draw.draw_index=index;auto& u=draw.uniform;
            u.domain={width,height,out.triangles,triangles};u.geometry={0,std::uint32_t(geometry.vertices.size()),native.first_index,native.index_count};
            u.uv.fill(UINT32_MAX);u.uv[11]=0;u.time[0]=seconds;
            const auto match=std::find_if(source.draws.begin(),source.draws.end(),[&](const auto& d){return d.draw_index==index;});
            const bool requested=native.material>=0&&std::size_t(native.material)<source.materials.size()&&
                source.materials[native.material].status==MToonResourceStatus::prepared_cpu;
            require((match!=source.draws.end())==requested);
            if(requested){
                require(match->material==native.material&&match->vertex_count&&match->vertex_base<=geometry.vertices.size()&&
                    match->vertex_count<=geometry.vertices.size()-match->vertex_base);
                u.geometry[0]=match->vertex_base;u.geometry[1]=match->vertex_count;u.uv[11]=1;
                const auto mask=source.materials[native.material].textures;
                for(unsigned slot=0;slot<vrm::mtoon_texture_count;++slot){const auto offset=match->uv_offsets[slot];
                    if(slot==7||!(mask&(1u<<slot))){require(offset==UINT32_MAX);continue;}
                    require(offset<=source.uv.size()&&match->vertex_count<=source.uv.size()-offset);u.uv[slot]=offset;
                }
            }
            for(std::uint32_t i=0;i<native.index_count;++i){const auto vertex=geometry.indices[std::size_t(native.first_index)+i];
                require(vertex<geometry.vertices.size());
                if(requested)require(vertex>=u.geometry[0]&&vertex-u.geometry[0]<u.geometry[1]);
            }
            out.triangles+=triangles;out.draws.push_back(draw);
        }
        return out;
    }catch(...){return {NativeMToonRasterStatus::invalid,0,{}};}
}
}
