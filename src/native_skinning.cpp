#include "native_skinning.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace evrm {
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
vrm::Matrix affine(vrm::Matrix m){
    for(float v:m)require(std::isfinite(v),"Nonfinite skin transform");
    require(std::abs(m[3])<=1e-5f&&std::abs(m[7])<=1e-5f&&std::abs(m[11])<=1e-5f&&std::abs(m[15]-1)<=1e-5f,"Nonaffine skin transform");
    const double a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    require(std::isfinite(det)&&det!=0,"Singular skin transform");
    m[3]=m[7]=m[11]=0;m[15]=1;return m;
}
void point(NativeSkinBounds& b,const std::array<double,3>& p){
    for(unsigned a=0;a<3;++a){require(std::isfinite(p[a]),"Nonfinite skin bound");b.low[a]=b.used?std::min(b.low[a],p[a]):p[a];b.high[a]=b.used?std::max(b.high[a],p[a]):p[a];}b.used=true;
}
void transformed(NativeSkinBounds& out,const NativeSkinBounds& source,const vrm::Matrix& m){
    if(!source.used)return;
    for(unsigned corner=0;corner<8;++corner){std::array<double,3> p{};
        for(unsigned r=0;r<3;++r){p[r]=m[12+r];for(unsigned c=0;c<3;++c)p[r]+=double(m[4*c+r])*((corner&(1u<<c))?source.high[c]:source.low[c]);}
        point(out,p);
    }
}
// Bound FP32 error using absolute intermediate magnitudes, not the final
// endpoints: large matrix/position terms can cancel to a tiny posed position.
// Eight rounded operations cover each four-weight sum, affine composition and
// point evaluation, with or without fused multiply-add. The tiny terms also
// cover flushing subnormal inputs/results to zero.
std::array<double,3> deformation_error(const NativeSkinBounds& source,const vrm::Matrix& world,
    const NativeSkinNode* node,const std::vector<vrm::Matrix>& matrices,std::uint32_t base){
    using Rows=std::array<std::array<double,4>,3>;
    constexpr double unit=0x1p-24,gamma=8*unit/(1-8*unit);
    constexpr double tiny=std::numeric_limits<float>::min();
    Rows magnitude{},error{};
    if(node){
        Rows bone{};
        for(std::size_t j=0;j<node->joint_blend.size();++j)if(node->joint_blend[j].used)
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
                bone[r][c]=std::max(bone[r][c],std::abs(double(matrices.at(base+j)[4*c+r])));
        Rows blend{},blend_error{};
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c){
            blend[r][c]=bone[r][c]*node->max_weight_sum;
            blend_error[r][c]=gamma*blend[r][c]+tiny*(node->max_weight_sum+8);
        }
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c){
            double work=0,propagated=0;
            for(unsigned k=0;k<3;++k){
                const double w=std::abs(double(world[k*4+r]));
                magnitude[r][c]+=w*blend[k][c];
                propagated+=w*blend_error[k][c]+tiny*(blend[k][c]+blend_error[k][c]);
                work+=(w+tiny)*(blend[k][c]+blend_error[k][c]);
            }
            if(c==3){const double t=std::abs(double(world[12+r]));magnitude[r][c]+=t;work+=t+tiny;propagated+=tiny;}
            error[r][c]=propagated+gamma*work+8*tiny;
        }
    }else{
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c){magnitude[r][c]=std::abs(double(world[4*c+r]));error[r][c]=tiny;}
    }
    std::array<double,3> out{};
    for(unsigned r=0;r<3;++r){
        double work=magnitude[r][3]+error[r][3],propagated=error[r][3];
        for(unsigned c=0;c<3;++c){
            const double p=std::max(std::abs(source.low[c]),std::abs(source.high[c]));
            work+=(magnitude[r][c]+error[r][c])*(p+tiny);
            propagated+=error[r][c]*p+(magnitude[r][c]+error[r][c])*tiny;
        }
        out[r]=propagated+gamma*work+8*tiny;
        require(std::isfinite(out[r]),"Nonfinite GPU deformation error bound");
    }
    return out;
}
void expanded(NativeSkinBounds& out,const NativeSkinBounds& source,const std::array<double,3>& error){
    if(!source.used)return;
    auto low=source.low,high=source.high;
    for(unsigned a=0;a<3;++a){low[a]-=error[a];high[a]+=error[a];}
    point(out,low);point(out,high);
}
float outward(double v,bool high){
    const double margin=2e-5+std::abs(v)*8e-6;
    v+=high?margin:-margin;require(std::isfinite(v)&&std::abs(v)<std::numeric_limits<float>::max(),"Skin bound exceeds float range");
    return std::nextafter(static_cast<float>(v),high?std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity());
}
}
NativeSkinAsset make_native_skin_asset(const vrm::Avatar& avatar,const vrm::RetargetResult& pose)noexcept{
    try{
        auto layout=std::make_shared<NativeAvatarPose>(make_native_avatar_pose(avatar,pose));require(bool(*layout),layout->error.c_str());
        NativeSkinAsset out;require(make_native_material_plan(avatar,*layout,out.material_plan),"Invalid skin material partition");
        out.node_count=avatar.nodes.size();out.vertices.reserve(layout->vertices.size());
        for(std::size_t n=0;n<avatar.nodes.size();++n){const auto& source=avatar.nodes[n];if(!source.active||source.mesh<0)continue;
            NativeSkinNode node;node.node=static_cast<std::uint32_t>(n);node.skin=source.skin;
            for(const auto& palette:pose.palettes)if(palette.mesh_node==static_cast<int>(n))node.joints=static_cast<std::uint32_t>(palette.matrices.size());
            node.joint_blend.resize(node.joints);
            for(const auto& primitive:avatar.meshes[source.mesh].primitives){
                const bool blend=primitive.material>=0&&avatar.materials[primitive.material].alpha==vrm::AlphaMode::Blend;
                node.skinned|=primitive.skinned;
                for(const auto& v:primitive.vertices){out.vertices.push_back({v,static_cast<std::uint32_t>(out.nodes.size()),primitive.skinned?1u:0u});
                    if(!blend)continue;
                    const std::array<double,3> p{v.position[0],v.position[1],v.position[2]};
                    if(!primitive.skinned){point(node.static_blend,p);continue;}
                    double sum=0;
                    for(unsigned i=0;i<4;++i){sum+=v.weights[i];if(v.weights[i]>0)point(node.joint_blend.at(v.joints[i]),p);}
                    node.min_weight_sum=std::min(node.min_weight_sum,sum);node.max_weight_sum=std::max(node.max_weight_sum,sum);
                }
            }
            out.nodes.push_back(std::move(node));
        }
        require(out.vertices.size()==layout->vertices.size()&&!out.nodes.empty()&&out.nodes.size()<=2048,"Skin layout mismatch");
        out.layout=std::move(layout);return out;
    }catch(const std::exception& e){NativeSkinAsset failed;failed.error=e.what();return failed;}catch(...){NativeSkinAsset failed;failed.error="Skin asset allocation failed";return failed;}
}
NativeSkinPose make_native_skin_pose(std::shared_ptr<const NativeSkinAsset> asset,const vrm::RetargetResult& pose)noexcept{
    try{
        require(asset&&bool(*asset)&&bool(pose)&&pose.node_world.size()==asset->node_count&&pose.palettes.size()<=2048,"Invalid skin pose metadata");
        std::vector<vrm::Matrix> worlds;worlds.reserve(pose.node_world.size());for(const auto& m:pose.node_world)worlds.push_back(affine(m));
        std::vector<const vrm::SkinPalette*> palettes(asset->node_count,nullptr);
        for(const auto& p:pose.palettes){require(p.mesh_node>=0&&static_cast<std::size_t>(p.mesh_node)<palettes.size()&&!palettes[p.mesh_node],"Ambiguous skin palette");palettes[p.mesh_node]=&p;}
        NativeSkinPose out;out.asset=asset;out.material_plan=asset->material_plan;out.batches.reserve(asset->nodes.size());NativeSkinBounds blend;
        std::size_t used=0;
        for(const auto& node:asset->nodes){NativeSkinBatch batch{};batch.world=worlds.at(node.node);batch.base=static_cast<std::uint32_t>(out.matrices.size());
            const auto* p=palettes[node.node];require(!node.skinned||p,"Missing skin palette");
            if(p){++used;require(p->skin==node.skin&&p->matrices.size()==node.joints&&node.joints>0&&node.joints<=512,"Skin palette layout changed");
                require(out.matrices.size()<=1048576-node.joints,"Skin matrix limit");batch.count=node.joints;
                for(const auto& m:p->matrices)out.matrices.push_back(affine(m));
            }
            NativeSkinBounds mesh,source_bounds;
            for(std::size_t j=0;j<node.joint_blend.size();++j)if(node.joint_blend[j].used){require(p,"Missing blend palette");transformed(mesh,node.joint_blend[j],out.matrices[batch.base+j]);point(source_bounds,node.joint_blend[j].low);point(source_bounds,node.joint_blend[j].high);}
            if(mesh.used)for(unsigned axis=0;axis<3;++axis){
                const std::array<double,4> ends{mesh.low[axis]*node.min_weight_sum,mesh.low[axis]*node.max_weight_sum,mesh.high[axis]*node.min_weight_sum,mesh.high[axis]*node.max_weight_sum};
                mesh.low[axis]=*std::min_element(ends.begin(),ends.end());mesh.high[axis]=*std::max_element(ends.begin(),ends.end());
            }
            if(mesh.used){NativeSkinBounds model;transformed(model,mesh,batch.world);expanded(blend,model,deformation_error(source_bounds,batch.world,&node,out.matrices,batch.base));}
            if(node.static_blend.used){NativeSkinBounds model;transformed(model,node.static_blend,batch.world);expanded(blend,model,deformation_error(node.static_blend,batch.world,nullptr,out.matrices,0));}
            out.batches.push_back(batch);
        }
        require(used==pose.palettes.size(),"Skin palette references inactive mesh");
        if(out.material_plan.mixed){require(blend.used,"Missing blend bounds");for(unsigned axis=0;axis<3;++axis){out.material_plan.blend_min[axis]=outward(blend.low[axis],false);out.material_plan.blend_max[axis]=outward(blend.high[axis],true);}}
        return out;
    }catch(const std::exception& e){NativeSkinPose failed;failed.error=e.what();return failed;}catch(...){NativeSkinPose failed;failed.error="Skin pose allocation failed";return failed;}
}
}
