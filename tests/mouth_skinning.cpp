#include "../src/mouth_deformation.hpp"
#include "../src/native_skinning.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
vrm::Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
struct Input {vrm::Avatar avatar;vrm::RetargetResult pose;};
Input fixture(){Input f;vrm::Primitive primitive;primitive.indices={0,1,2};
    for(const auto& point:std::array<std::array<float,3>,3>{{{0,0,0},{1,0,0},{0,1,0}}}){
        vrm::Vertex v;v.position=point;v.normal={0,0,1};primitive.vertices.push_back(v);}
    primitive.material=0;auto blend=primitive;blend.material=1;f.avatar.meshes.push_back({{primitive,blend}});f.avatar.vertex_count=6;f.avatar.triangle_count=2;
    f.avatar.materials.resize(2);f.avatar.materials[1].alpha=vrm::AlphaMode::Blend;
    f.avatar.nodes.resize(2);f.pose.node_world.resize(2,identity());
    for(auto& node:f.avatar.nodes){node.mesh=0;node.active=true;node.local=node.world=identity();}
    f.pose.node_world[1][0]=2;f.pose.node_world[1][5]=3;f.pose.node_world[1][10]=.5f;f.pose.node_world[1][12]=10;
    auto data=std::make_shared<vrm::MouthData>();data->available=1;vrm::MouthDeltaRecord record;record.vertex=2;record.position[0]={2,-1,2};
    data->records.push_back(record);data->ranges.push_back({1,1,0,1});f.avatar.mouth=data;return f;
}
void optional_failure(Input input,std::shared_ptr<vrm::MouthData> data){input.avatar.mouth=std::move(data);const auto asset=evrm::make_native_skin_asset(input.avatar,input.pose);
    need(bool(asset)&&!asset.mouth&&asset.mouth_lookup.empty()&&!asset.mouth_limitation.empty(),"optional compilation suppressed base geometry or retained partial lookup");}
}
int main(){
    auto input=fixture();auto asset=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(input.avatar,input.pose));
    need(bool(*asset)&&asset->mouth==input.avatar.mouth,"skin asset did not share immutable mouth records");
    need(asset->mouth_lookup.size()==12&&asset->mouth_lookup[11]==0,"flattened node/primitive/vertex lookup wrong");
    for(unsigned n=0;n<11;++n)need(asset->mouth_lookup[n]==std::numeric_limits<std::uint32_t>::max(),"shared mesh instance or other primitive received the mouth");
    need(asset->nodes[1].static_blend.high[2]==0,"optional bounds overwrote base influence boxes");
    auto packed=evrm::make_native_skin_pose(asset,input.pose);need(bool(packed)&&packed.mouth_safe&&packed.material_plan.blend_max[0]>=14&&packed.material_plan.blend_max[2]>=1,"posed mouth bounds missing");
    auto base_input=input;base_input.avatar.mouth.reset();auto base_asset=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(base_input.avatar,base_input.pose));
    const auto base_pose=evrm::make_native_skin_pose(base_asset,base_input.pose);
    need(packed.base_blend_min==base_pose.material_plan.blend_min&&packed.base_blend_max==base_pose.material_plan.blend_max,"neutral mouth lost exact baseline transparency bounds");
    auto reference=input.avatar;auto& vertex=reference.meshes[0].primitives[1].vertices[2];
    need(vrm::apply_mouth_delta(vertex,input.avatar.mouth->records[0],{.5f,0,0,0,0}),"CPU reference rejected");
    const auto expected=evrm::make_native_avatar_pose(reference,input.pose);need(bool(expected),"posed reference rejected");
    need(expected.vertices[11].position[0]==12&&expected.vertices[11].position[1]==1.5f&&expected.vertices[11].position[2]==.5f,"mouth was applied after nonuniform transform");
    for(float weight:{0.f,.25f,.5f,.75f,1.f}){auto shaped=input.avatar;need(vrm::apply_mouth_delta(shaped.meshes[0].primitives[1].vertices[2],input.avatar.mouth->records[0],{weight,0,0,0,0}),"sampled shape rejected");
        const auto result=evrm::make_native_avatar_pose(shaped,input.pose);need(bool(result),"sampled pose rejected");
        for(unsigned axis=0;axis<3;++axis)need(result.vertices[11].position[axis]>=packed.material_plan.blend_min[axis]&&result.vertices[11].position[axis]<=packed.material_plan.blend_max[axis],"sampled shape outside publication bounds");}
    auto bad=std::make_shared<vrm::MouthData>(*input.avatar.mouth);bad->ranges[0].node=99;optional_failure(input,bad);
    bad=std::make_shared<vrm::MouthData>(*input.avatar.mouth);bad->records[0].vertex=3;optional_failure(input,bad);
    bad=std::make_shared<vrm::MouthData>(*input.avatar.mouth);bad->ranges.push_back(bad->ranges[0]);optional_failure(input,bad);
    bad=std::make_shared<vrm::MouthData>(*input.avatar.mouth);bad->available=32;optional_failure(input,bad);
    bad=std::make_shared<vrm::MouthData>(*input.avatar.mouth);bad->records[0].normal[0][0]=std::numeric_limits<float>::quiet_NaN();optional_failure(input,bad);
    auto skinned=input;for(auto& primitive:skinned.avatar.meshes[0].primitives){primitive.skinned=true;
        for(auto& v:primitive.vertices){v.joints={0,1,0,1};v.weights={.25f,.75f,0,0};}}
    for(auto& node:skinned.avatar.nodes)node.skin=0;
    skinned.avatar.skins.push_back({{0,1},{identity(),identity()}});auto shifted=identity();shifted[12]=.5f;
    skinned.pose.palettes={{0,0,{identity(),identity()}},{1,0,{identity(),shifted}}};
    auto skin_asset=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(skinned.avatar,skinned.pose));
    need(bool(*skin_asset)&&skin_asset->mouth&&skin_asset->nodes[1].joint_blend[0].high[2]==0,"mouth changed base joint influence bounds");
    const auto skin_pose=evrm::make_native_skin_pose(skin_asset,skinned.pose);
    need(bool(skin_pose)&&skin_pose.mouth_safe&&skin_pose.material_plan.blend_max[0]>=14.75&&skin_pose.material_plan.blend_max[2]>=1,"weighted mouth joint bounds incomplete");
    auto extreme=input.pose;extreme.node_world[1]=identity();extreme.node_world[1][0]=2e38f;
    const auto neutral=evrm::make_native_skin_pose(asset,extreme);need(bool(neutral)&&!neutral.mouth_safe&&std::isfinite(neutral.material_plan.blend_max[0]),"unsafe optional posed bounds suppressed a valid base pose");
    auto opaque=input;auto opaque_data=std::make_shared<vrm::MouthData>(*input.avatar.mouth);opaque_data->ranges[0].primitive=0;opaque.avatar.mouth=opaque_data;
    auto opaque_asset=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(opaque.avatar,opaque.pose));
    const auto opaque_pose=evrm::make_native_skin_pose(opaque_asset,opaque.pose);
    need(bool(opaque_pose)&&opaque_pose.mouth_safe&&opaque_pose.material_plan.blend_max[2]<.001f,"opaque mouth incorrectly expanded transparent bounds");
    const auto opaque_neutral=evrm::make_native_skin_pose(opaque_asset,extreme);
    need(bool(opaque_neutral)&&!opaque_neutral.mouth_safe,"unsafe opaque mouth positions bypassed neutral fallback");
    std::puts("PASS shared lookup, pre-skin reference, conservative mouth bounds and neutral pose fallback");
}
