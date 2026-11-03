// Generated self-contained GLB fixtures exercise the actual importer, CPU only.
#include "../src/vrm_asset.hpp"
#include "../src/native_skinning.hpp"
#include "avatar_fixture.hpp"
#include "texture_depth_pngs.hpp"
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <cmath>
using Json=nlohmann::json;
using Bytes=std::vector<std::uint8_t>;
void check(bool ok,const std::string& text){if(!ok)throw std::runtime_error(text);}
struct Fixture {
    Json doc;Bytes bin;
    Fixture(){auto b=avatar_fixture_bytes();std::uint32_t n;std::memcpy(&n,b.data()+12,4);
        doc=Json::parse(b.begin()+20,b.begin()+20+n);bin={b.begin()+28+n,b.end()};
        auto uv=add<float>({0,0,1,0,0,1},"VEC2",3);
        doc["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"]=uv;
        auto offset=bin.size();bin.insert(bin.end(),png_rgba8.begin(),png_rgba8.end());
        auto view=doc["bufferViews"].size();doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",png_rgba8.size()}});
        doc["images"]=Json::array({{{"bufferView",view},{"mimeType","image/png"}}});doc["textures"]=Json::array({{{"source",0}}});
    }
    template<class T>std::size_t add(std::initializer_list<T> values,const char* type,int count,int component=5126){
        while(bin.size()%4){bin.push_back(0);}const auto offset=bin.size();auto p=reinterpret_cast<const std::uint8_t*>(values.begin());bin.insert(bin.end(),p,p+values.size()*sizeof(T));
        auto v=doc["bufferViews"].size();doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",values.size()*sizeof(T)}});
        auto a=doc["accessors"].size();doc["accessors"].push_back({{"bufferView",v},{"componentType",component},{"count",count},{"type",type}});return a;
    }
    Bytes bytes(){while(bin.size()%4)bin.push_back(0);doc["buffers"][0]["byteLength"]=bin.size();auto t=doc.dump();while(t.size()%4)t+=' ';
        Bytes out;auto word=[&](std::uint32_t w){auto p=reinterpret_cast<const std::uint8_t*>(&w);out.insert(out.end(),p,p+4);};
        word(0x46546c67);word(2);word(std::uint32_t(28+t.size()+bin.size()));word(std::uint32_t(t.size()));word(0x4e4f534a);out.insert(out.end(),t.begin(),t.end());word(std::uint32_t(bin.size()));word(0x004e4942);out.insert(out.end(),bin.begin(),bin.end());return out;
    }
    Json& material(){return doc["materials"][0];}
    void legacy(){auto h=doc["extensions"]["VRMC_vrm"]["humanoid"]["humanBones"];doc["extensions"].erase("VRMC_vrm");
        auto& e=doc["extensions"]["VRM"];e={{"specVersion","0.0"},{"meta",{{"title","Legacy synthetic"}}},{"humanoid",{{"humanBones",Json::array()}}}};
        for(auto it=h.begin();it!=h.end();++it)e["humanoid"]["humanBones"].push_back({{"bone",it.key()},{"node",it.value()["node"]}});
        doc["extensionsUsed"]={"VRM"};e["materialProperties"]=Json::array({{{"name","Skin"},{"shader","VRM/MToon"}}});
    }
};
template<class M,class=void>struct HasSurface:std::false_type{};
template<class M>struct HasSurface<M,std::void_t<decltype(std::declval<M>().surface)>>:std::true_type{};
template<class A>void metadata(const A& a){
    using M=typename std::decay_t<decltype(a.materials)>::value_type;
    if constexpr(HasSurface<M>::value){auto s=a.materials[0].surface;check(bool(s),"normal/matcap/rim surface missing");
        check(int(s->kind)==2&&s->normal.texture==0&&s->matcap.texture==0&&s->rim.texture==0,"map semantics lost");
        check(s->normal_scale==.5f&&s->rim_lighting_mix==1&&s->rim_power==5,"VRM1 defaults lost");
        check(s->rim_color[0]==.25f&&s->matcap_factor[2]==.75f,"color factors lost");
        check(a.images[0].rgba==Bytes({1,2,3,4,239,240,241,242}),"surface-only image replaced with placeholder");
    }else check(false,"normal/matcap/rim surface missing");
}
int main(int argc,char** argv){try{check(argc==2,"mode required");std::string mode=argv[1];Fixture f;
    if(mode=="metadata"){
        f.material()["normalTexture"]={{"index",0},{"scale",.5}};
        f.material()["extensions"]["VRMC_materials_mtoon"]={{"specVersion","1.0"},{"matcapTexture",{{"index",0}}},{"rimMultiplyTexture",{{"index",0}}},{"parametricRimColorFactor",{.25,.5,.75}},{"matcapFactor",{.25,.5,.75}}};
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);metadata(r.avatar);
    }else if(mode=="legacy"){
        f.legacy();auto& e=f.doc["extensions"]["VRM"]["materialProperties"][0];
        e["textureProperties"]={{"_BumpMap",0},{"_RimTexture",0},{"_SphereAdd",0}};
        e["vectorProperties"]={{"_MainTex",{.1,.2,2,.5}},{"_BumpMap",{8,8,8,8}},{"_RimColor",{.25,.5,.75,1}}};
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);auto s=r.avatar.materials[0].surface;
        check(s&&int(s->kind)==1&&s->rim_lighting_mix==0&&s->rim_power==1,"legacy kind/defaults lost");
        check(std::abs(s->normal.offset[1]-.3f)<1e-6&&s->normal.scale[0]==2&&s->rim.offset==s->normal.offset,"legacy main transform incorrect");
        check(s->matcap.offset[0]==0&&s->matcap.scale[0]==1,"legacy sphere incorrectly uses surface UV transform");
    }else if(mode=="geometry"){
        f.material()["normalTexture"]={{"index",0}};
        f.material()["extensions"]["VRMC_materials_mtoon"]={{"specVersion","1.0"},{"rimMultiplyTexture",{{"index",0},{"texCoord",1},{"extensions",{{"KHR_texture_transform",{{"offset",{.1,.2}},{"scale",{2,3}},{"rotation",1.5707963267948966}}}}}}},{"parametricRimColorFactor",{1,1,1}}};
        auto uv1=f.add<float>({.25f,.5f,.5f,.25f,.75f,1},"VEC2",3);f.doc["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"]=uv1;
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);auto& p=r.avatar.meshes[0].primitives[0];
        check(p.surface_vertices.size()==3,"optional UV/tangent geometry missing");auto v=p.surface_vertices[0];
        check(std::abs(v.rim_uv[0]+1.4f)<1e-6&&std::abs(v.rim_uv[1]-.7f)<1e-6,"rim UV transform/order wrong");
        check(v.normal_uv[0]==0&&v.tangent[0]==1&&v.tangent[1]==0&&v.tangent[3]==1,"normal tangent incorrect");
        vrm::RetargetResult pose;for(auto& n:r.avatar.nodes)pose.node_world.push_back(n.world);
        auto asset=evrm::make_native_skin_asset(r.avatar,pose);check(bool(asset),asset.error);
        check(asset.surface_vertices.size()==asset.vertices.size(),"raw skin surface stream missing");
        check(asset.surface_vertices[1].normal_uv[0]==1&&asset.surface_vertices[1].tangent[0]==1,"raw skin surface order changed");
    }else if(mode=="normal_uv_transform"){
        auto uv=f.add<float>({.25f,.5f,.5f,.25f,.75f,1},"VEC2",3);auto& p=f.doc["meshes"][0]["primitives"][0];p["attributes"]["TEXCOORD_1"]=uv;
        p["attributes"]["TANGENT"]=f.add<float>({1,0,0,1,1,0,0,1,1,0,0,1},"VEC4",3);
        f.material()["normalTexture"]={{"index",0},{"texCoord",0},{"extensions",{{"KHR_texture_transform",{{"texCoord",1},{"offset",{.2,.1}},{"scale",{-1,2}},{"rotation",1.5707963267948966}}}}}};
        f.material()["pbrMetallicRoughness"]["baseColorTexture"]={{"index",0},{"extensions",{{"KHR_texture_transform",{{"offset",{.9,.8}}}}}}};
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);const auto& out=r.avatar.meshes[0].primitives[0];auto& s=out.surface_vertices[0];
        check(std::abs(s.normal_uv[0]+.8f)<1e-6&&std::abs(s.normal_uv[1]+.15f)<1e-6,"normal selected UV/transform wrong");
        check(std::abs(out.vertices[0].uv[0]-.9f)<1e-6&&std::abs(out.vertices[0].uv[1]-.8f)<1e-6,"base and normal UV transforms became coupled");
        check(std::abs(s.tangent[0]-.894427191f)<1e-6&&std::abs(s.tangent[1]+.447213595f)<1e-6&&s.tangent[3]==-1,"transformed selected normal UV did not regenerate tangent handedness");
    }else if(mode=="large_finite_basis"){
        f.material()["normalTexture"]={{"index",0}};auto& p=f.doc["meshes"][0]["primitives"][0];
        p["attributes"]["POSITION"]=f.add<float>({0,0,0,1e30f,0,0,0,1e30f,0},"VEC3",3);
        p["attributes"]["TEXCOORD_0"]=f.add<float>({0,0,1e30f,0,0,1e30f},"VEC2",3);
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);auto& s=r.avatar.meshes[0].primitives[0].surface_vertices[0];
        check(s.tangent[0]==1&&s.tangent[1]==0&&s.tangent[2]==0&&s.tangent[3]==1,"finite large-coordinate tangent lost to intermediate overflow");
    }else if(mode=="mixed_stream"){
        f.material()["normalTexture"]={{"index",0}};f.doc["materials"].push_back({{"name","Plain"}});
        Json plain=f.doc["meshes"][0]["primitives"][0];plain["material"]=1;f.doc["meshes"][0]["primitives"].push_back(plain);
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);vrm::RetargetResult pose;for(auto& n:r.avatar.nodes)pose.node_world.push_back(n.world);
        auto raw=evrm::make_native_skin_asset(r.avatar,pose);check(bool(raw),raw.error);check(raw.surface_vertices.size()==6,"mixed raw stream not fully aligned");
        auto cpu=evrm::make_native_avatar_pose(r.avatar,pose,nullptr,true);check(bool(cpu)&&cpu.surface_vertices.size()==6,"mixed posed stream not fully aligned");
        for(std::size_t n=3;n<6;++n){check(raw.surface_vertices[n].tangent[3]==0&&cpu.surface_vertices[n].tangent[3]==0,"plain geometry padding acquired a tangent");}
    }else if(mode=="cpu_pose"){
        f.material()["normalTexture"]={{"index",0}};auto& p=f.doc["meshes"][0]["primitives"][0];
        p["attributes"]["NORMAL"]=f.add<float>({.70710678f,.70710678f,0,.70710678f,.70710678f,0,.70710678f,.70710678f,0},"VEC3",3);
        p["attributes"]["TANGENT"]=f.add<float>({.70710678f,-.70710678f,0,1,.70710678f,-.70710678f,0,1,.70710678f,-.70710678f,0,1},"VEC4",3);
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);
        const vrm::Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const vrm::Matrix stretch{-2,0,0,0,0,3,0,0,0,0,4,0,0,0,0,1};
        for(bool skinned:{false,true}){auto a=r.avatar;vrm::RetargetResult pose;for(auto& n:a.nodes)pose.node_world.push_back(n.world);
            if(skinned){a.skins.push_back({{0},{identity}});a.nodes[15].skin=0;auto& primitive=a.meshes[0].primitives[0];primitive.skinned=true;
                for(auto& v:primitive.vertices){v.weights[0]=1;}pose.palettes.push_back({15,0,{stretch}});
            }else pose.node_world[15]=stretch;
            auto basic=evrm::make_native_avatar_pose(a,pose);check(bool(basic)&&basic.surface_vertices.empty(),"legacy CPU pose allocated optional stream");
            auto enhanced=evrm::make_native_avatar_pose(a,pose,nullptr,true);check(bool(enhanced),enhanced.error);
            check(enhanced.surface_vertices.size()==enhanced.vertices.size(),"optional CPU posed stream missing");
            auto& s=enhanced.surface_vertices[0];auto& n=enhanced.vertices[0];
            check(std::abs(n.normal[0]+.832050294f)<1e-6&&std::abs(n.normal[1]-.554700196f)<1e-6,"nonuniform inverse-transpose normal incorrect");
            check(std::abs(s.tangent[0]+.554700196f)<1e-6&&std::abs(s.tangent[1]+.832050294f)<1e-6&&s.tangent[3]==-1,"reflected linear tangent/handedness incorrect");
        }
    }else if(mode=="invalid_stream"){
        f.material()["normalTexture"]={{"index",0}};auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);
        vrm::RetargetResult pose;for(auto& n:r.avatar.nodes)pose.node_world.push_back(n.world);
        r.avatar.meshes[0].primitives[0].surface_vertices[0].normal_uv[0]=std::numeric_limits<float>::infinity();
        auto asset=evrm::make_native_skin_asset(r.avatar,pose);check(!asset,"nonfinite raw optional skin UV accepted");
        auto cpu=evrm::make_native_avatar_pose(r.avatar,pose,nullptr,true);check(!cpu&&cpu.surface_vertices.empty()&&cpu.vertices.empty(),"invalid CPU side stream leaked partial output");
        r.avatar.meshes[0].primitives[0].surface_vertices[0].normal_uv[0]=0;
        r.avatar.meshes[0].primitives[0].surface_vertices[0].tangent[3]=2;
        check(!evrm::make_native_skin_asset(r.avatar,pose),"invalid raw tangent handedness accepted");
    }else if(mode=="seam"){
        f.material()["normalTexture"]={{"index",0}};auto& p=f.doc["meshes"][0]["primitives"][0];
        p["attributes"]["POSITION"]=f.add<float>({0,0,0,1,0,0,0,1,0,1,1,0},"VEC3",4);
        p["attributes"]["NORMAL"]=f.add<float>({0,0,1,0,0,1,0,0,1,0,0,1},"VEC3",4);
        p["attributes"]["TEXCOORD_0"]=f.add<float>({0,0,1,0,0,1,-1,-1},"VEC2",4);
        p["indices"]=f.add<std::uint16_t>({0,1,2,1,3,2},"SCALAR",6,5123);
        auto delta=f.add<float>({0,0,0,.125f,0,0,0,0,0,0,0,0},"VEC3",4);
        p["targets"]=Json::array({{{"POSITION",delta}}});
        f.doc["extensions"]["VRMC_vrm"]["expressions"]["preset"]["aa"]={{"morphTargetBinds",Json::array({{{"node",15},{"index",0},{"weight",1}}})}};
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);auto& out=r.avatar.meshes[0].primitives[0];
        check(out.vertices.size()==6,"mirrored shared-index tangent seam was not split");
        check(out.surface_vertices[out.indices[0]].tangent[3]==1&&out.surface_vertices[out.indices[3]].tangent[3]==-1,"mirrored face handedness lost");
        check(out.indices[1]!=out.indices[3]&&out.vertices[out.indices[1]].position==out.vertices[out.indices[3]].position,"seam source/index mapping wrong");
        check(r.avatar.mouth&&r.avatar.mouth->records.size()==2,"mouth delta missing on generated tangent seam copy");
        for(auto& d:r.avatar.mouth->records)check(d.position[0][0]==.125f,"seam mouth delta changed");
        vrm::ImportLimits limit;limit.vertices=5;check(!vrm::load_vrm_bytes(f.bytes(),limit),"seam expansion bypassed vertex budget");
        limit.vertices=6;limit.mouth_records=1;auto limited=vrm::load_vrm_bytes(f.bytes(),limit);check(bool(limited)&&!limited.avatar.mouth,"seam mouth expansion bypassed record budget");
        limit.mouth_records=10;limit.mouth_source_vertex_visits=5;limited=vrm::load_vrm_bytes(f.bytes(),limit);check(bool(limited)&&!limited.avatar.mouth,"seam mouth expansion bypassed source visit budget");
    }else if(mode=="degenerate"){
        f.material()["normalTexture"]={{"index",0}};
        f.doc["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"]=f.add<float>({0,0,0,0,0,0},"VEC2",3);
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);
        check(r.avatar.meshes[0].primitives[0].surface_vertices.size()==3,"degenerate stream missing");
        for(auto& s:r.avatar.meshes[0].primitives[0].surface_vertices)for(auto t:s.tangent)check(t==0,"degenerate UV normal-map basis not neutral");
        f.doc["meshes"][0]["primitives"][0]["attributes"]["TANGENT"]=f.add<float>({1,0,0,1,1,0,0,1,1,0,0,1},"VEC4",3);
        r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);
        for(auto& s:r.avatar.meshes[0].primitives[0].surface_vertices)check(s.tangent[3]==0,"authored tangent bypassed degenerate UV fallback");
    }else if(mode=="invalid_surface"){
        const Json values[]={true,1.5,-1,4294967296ULL,"0",Json::object()};
        for(auto v:values){f.material()["normalTexture"]={{"index",v}};auto r=vrm::load_vrm_bytes(f.bytes());check(!r,"malformed normal reference accepted");check(r.avatar.nodes.empty(),"partial invalid avatar");}
        for(const char* key:{"matcapTexture","rimMultiplyTexture"})for(auto v:values){f.material().erase("normalTexture");f.material()["extensions"]["VRMC_materials_mtoon"]={{"specVersion","1.0"},{key,{{"index",v}}}};check(!vrm::load_vrm_bytes(f.bytes()),"malformed MToon map reference accepted");}
        f.material().erase("extensions");
        f.material()["normalTexture"]={{"index",0},{"texCoord",1}};check(!vrm::load_vrm_bytes(f.bytes()),"missing selected normal UV accepted");
        f.material().erase("normalTexture");
        for(auto v:values){f.material()["extensions"]["VRMC_materials_mtoon"]={{"specVersion","1.0"},{"rimLightingMixFactor",v}};if(v==1.5||v==-1||!v.is_number())check(!vrm::load_vrm_bytes(f.bytes()),"invalid rim mix accepted");}
        f.material().erase("extensions");f.material()["normalTexture"]={{"index",0},{"scale",true}};check(!vrm::load_vrm_bytes(f.bytes()),"boolean normal scale accepted");
        f.material()["normalTexture"]={{"index",0},{"texCoord",0},{"extensions",{{"KHR_texture_transform",{{"scale",{1,true}}}}}}};check(!vrm::load_vrm_bytes(f.bytes()),"boolean UV transform accepted");
        f.material()["normalTexture"]={{"index",0}};
        auto& a=f.doc["accessors"][f.doc["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"].get<std::size_t>()];a["componentType"]=5122;a["normalized"]=true;check(!vrm::load_vrm_bytes(f.bytes()),"signed surface UV accessor accepted");
        a["componentType"]=5126;a["normalized"]=false;const auto offset=f.doc["bufferViews"][a["bufferView"].get<std::size_t>()]["byteOffset"].get<std::size_t>();const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(f.bin.data()+offset,&nan,4);check(!vrm::load_vrm_bytes(f.bytes()),"nonfinite selected normal UV accepted");
        f.material()["normalTexture"]["scale"]=0;check(!vrm::load_vrm_bytes(f.bytes()),"disabled normal effect bypassed selected UV validation");
    }else if(mode=="no_maps"){
        auto r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);auto& p=r.avatar.meshes[0].primitives[0];
        check(!r.avatar.materials[0].surface&&p.surface_vertices.empty()&&p.vertices.size()==3&&p.indices==std::vector<std::uint32_t>({0,1,2}),"ordinary topology/allocation changed");
        f.material()["extensions"]["VRMC_materials_mtoon"]={{"specVersion","1.0"},{"parametricRimColorFactor",{.25,.5,.75}}};
        r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);check(r.avatar.meshes[0].primitives[0].surface_vertices.empty(),"factor-only rim allocates side geometry");
        f.material()["normalTexture"]={{"index",0},{"scale",0}};
        f.material()["extensions"]["VRMC_materials_mtoon"]={{"specVersion","1.0"},{"matcapTexture",{{"index",0}}}};
        r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);check(r.avatar.meshes[0].primitives[0].surface_vertices.empty(),"matcap/neutral normal allocates side geometry");
        f.material()["extensions"]["VRMC_materials_mtoon"]={{"specVersion","1.0"},{"rimMultiplyTexture",{{"index",0}}}};
        r=vrm::load_vrm_bytes(f.bytes());check(bool(r),r.error);check(r.avatar.meshes[0].primitives[0].surface_vertices.empty(),"ineffective rim mask allocates side geometry");
        static_assert(sizeof(vrm::Vertex)==56&&sizeof(evrm::NativeSkinVertex)==64&&sizeof(evrm::NativeAvatarVertex)==44,"ordinary ABI changed");
    }else if(mode=="invalid_reference"){
        f.material()["normalTexture"]={{"index",4294967296ULL}};auto r=vrm::load_vrm_bytes(f.bytes());check(!r,"raw oversized normal index was accepted");
    }else check(false,"unknown mode");
    std::cout<<"PASS "<<mode<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}


