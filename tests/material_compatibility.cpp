// Synthetic import regression. No player avatar or graphics/game dependency.
#include "../src/vrm_asset.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
using Json=nlohmann::json;
using Bytes=std::vector<std::uint8_t>;
namespace {
int checks=0,failures=0;
void expect(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<message<<'\n';}}
void word(Bytes& bytes,std::uint32_t value){const auto* p=reinterpret_cast<const std::uint8_t*>(&value);bytes.insert(bytes.end(),p,p+4);}
Bytes fixture(const Json& material,bool legacy,bool mtoon){
    Json doc={{"asset",{{"version","2.0"}}},{"scene",0},{"scenes",Json::array({{{"nodes",{0,15}}}})},
        {"nodes",Json::array()},{"skins",Json::array()},{"materials",Json::array({material})},{"extensionsUsed",Json::array()}};
    const char* bones[]={"hips","spine","head","leftUpperLeg","leftLowerLeg","leftFoot","rightUpperLeg","rightLowerLeg","rightFoot","leftUpperArm","leftLowerArm","leftHand","rightUpperArm","rightLowerArm","rightHand"};
    const int parent[]={-1,0,1,0,3,4,0,6,7,1,9,10,1,12,13};
    Json human=Json::object();
    for(int i=0;i<15;++i){doc["nodes"].push_back({{"translation",{0,i==0?1.f:.1f,0}},{"children",Json::array()}});
        if(parent[i]>=0){doc["nodes"][parent[i]]["children"].push_back(i);}
        human[bones[i]]={{"node",i}};}
    doc["nodes"].push_back({{"mesh",0}});
    if(legacy){
        auto& vrm=doc["extensions"]["VRM"];vrm={{"specVersion","0.0"},{"meta",{{"title","Synthetic material regression"}}},{"humanoid",{{"humanBones",Json::array()}}}};
        for(auto it=human.begin();it!=human.end();++it)vrm["humanoid"]["humanBones"].push_back({{"bone",it.key()},{"node",it.value()["node"]}});
        if(mtoon)vrm["materialProperties"]=Json::array({{{"name",material["name"]},{"shader","VRM/MToon"}}});
        doc["extensionsUsed"].push_back("VRM");
    }else{
        doc["extensions"]["VRMC_vrm"]={{"specVersion","1.0"},{"meta",{{"name","Synthetic material regression"},{"authors",{"Fixture"}}}},{"humanoid",{{"humanBones",human}}}};
        doc["extensionsUsed"].push_back("VRMC_vrm");
        if(mtoon){doc["materials"][0]["extensions"]["VRMC_materials_mtoon"]={{"specVersion","1.0"}};doc["extensionsUsed"].push_back("VRMC_materials_mtoon");}
    }
    const float positions[]={0,0,0,1,0,0,0,1,0},normals[]={0,0,1,0,0,1,0,0,1};
    Bytes bin(sizeof positions+sizeof normals);std::memcpy(bin.data(),positions,sizeof positions);std::memcpy(bin.data()+sizeof positions,normals,sizeof normals);
    doc["bufferViews"]=Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",sizeof positions}},{{"buffer",0},{"byteOffset",sizeof positions},{"byteLength",sizeof normals}}});
    doc["accessors"]=Json::array({{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"}},{{"bufferView",1},{"componentType",5126},{"count",3},{"type","VEC3"}}});
    doc["meshes"]=Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",0},{"NORMAL",1}}},{"material",0},{"mode",4}}})}}});
    doc["buffers"]=Json::array({{{"byteLength",bin.size()}}});
    auto text=doc.dump();while(text.size()%4)text+=' ';Bytes out;word(out,0x46546c67);word(out,2);word(out,28+static_cast<std::uint32_t>(text.size()+bin.size()));
    word(out,static_cast<std::uint32_t>(text.size()));word(out,0x4e4f534a);out.insert(out.end(),text.begin(),text.end());word(out,static_cast<std::uint32_t>(bin.size()));word(out,0x004e4942);out.insert(out.end(),bin.begin(),bin.end());return out;
}
Json material(const std::string& name="N00_000_00_FaceBrow_00_FACE (Instance)"){
    return {{"name",name},{"alphaMode","BLEND"},{"alphaCutoff",.2},{"doubleSided",true},
        {"pbrMetallicRoughness",{{"baseColorFactor",{.8,.7,.6,1.0}}}},{"extensions",{{"KHR_materials_unlit",Json::object()}}}};
}
void check_import(const Json& input,bool legacy,bool mtoon,vrm::AlphaMode wanted,float cutoff,bool warning){
    const auto bytes=fixture(input,legacy,mtoon);const auto original=bytes;auto result=vrm::load_vrm_bytes(bytes);
    if(!result){std::cerr<<"Synthetic fixture import failed: "<<result.error<<'\n';++failures;return;}
    const auto& m=result.avatar.materials.at(0);
    expect(m.alpha==wanted,"Eyebrow material took the wrong render path");
    expect(m.alpha_cutoff==cutoff,"Unexpected alpha cutoff");
    expect(m.base_color[0]==.8f&&m.base_color[1]==.7f&&m.base_color[2]==.6f,"Material tint changed");
    expect(m.base_color[3]==input["pbrMetallicRoughness"]["baseColorFactor"][3].get<float>(),"Material fade changed");
    expect(m.double_sided&&m.unlit&&m.base_texture==-1,"Unrelated material state changed");
    const bool noted=std::any_of(result.avatar.warnings.begin(),result.avatar.warnings.end(),[](const std::string& x){return x.find("FaceBrow")!=std::string::npos;});
    expect(noted==warning,"Compatibility warning missing or applied to another material");
    expect(bytes==original,"Importer changed source avatar bytes");
}
}
int main(int argc,char** argv){
    // Removing/narrowing the compatibility rule must break these real imports.
    for(bool legacy:{true,false})check_import(material(),legacy,true,vrm::AlphaMode::Mask,.5f,true);
    // Broadening it must break these negative controls.
    for(const char* name:{"N00_000_00_EyeIris_00_EYE","Accessory_GlassesLowLens_01_MATCAP","N00_000_00_Hair_00_HAIR","FaceBrow","CustomFaceBrowOverlay"})
        check_import(material(name),false,true,vrm::AlphaMode::Blend,.2f,false);
    check_import(material(),true,false,vrm::AlphaMode::Blend,.2f,false);
    check_import(material(),false,false,vrm::AlphaMode::Blend,.2f,false);
    auto faded=material();faded["pbrMetallicRoughness"]["baseColorFactor"][3]=.4;check_import(faded,true,true,vrm::AlphaMode::Blend,.2f,false);
    auto mask=material();mask["alphaMode"]="MASK";check_import(mask,true,true,vrm::AlphaMode::Mask,.2f,false);
    auto opaque=material();opaque["alphaMode"]="OPAQUE";check_import(opaque,true,true,vrm::AlphaMode::Opaque,.2f,false);
    if(argc==2){
        const auto result=vrm::load_vrm(std::filesystem::u8path(argv[1]));expect(bool(result),"Local avatar import failed");int brows=0,blends=0;
        if(result)for(const auto& m:result.avatar.materials){if(m.name.find("_FaceBrow_")!=std::string::npos){++brows;expect(m.alpha==vrm::AlphaMode::Mask&&m.alpha_cutoff==.5f,"Original avatar brow correction missing");}else if(m.alpha==vrm::AlphaMode::Blend)++blends;}
        expect(brows==1&&blends>=3,"Original avatar transparency controls changed");
    }
    std::cout<<checks<<" material import checks, "<<failures<<" failures\n";return failures?1:0;
}
