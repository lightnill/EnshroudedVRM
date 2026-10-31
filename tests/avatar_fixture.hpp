#pragma once
#include "../vendor/tinygltf/json.hpp"
#include <vector>
#include <cstring>
inline std::vector<std::uint8_t> avatar_fixture_bytes(const char* name="Synthetic avatar",float root_height=1.f){
    using Json=nlohmann::json;using Bytes=std::vector<std::uint8_t>;
    Json doc={{"asset",{{"version","2.0"}}},{"scene",0},{"scenes",Json::array({{{"nodes",{0,15}}}})},
        {"nodes",Json::array()},{"skins",Json::array()},{"materials",Json::array({{{"name","Skin"},{"alphaMode","OPAQUE"}}})},{"extensionsUsed",{"VRMC_vrm"}}};
    const char* bones[]={"hips","spine","head","leftUpperLeg","leftLowerLeg","leftFoot","rightUpperLeg","rightLowerLeg","rightFoot","leftUpperArm","leftLowerArm","leftHand","rightUpperArm","rightLowerArm","rightHand"};
    const int parent[]={-1,0,1,0,3,4,0,6,7,1,9,10,1,12,13};Json human=Json::object();
    for(int i=0;i<15;++i){doc["nodes"].push_back({{"translation",{0,i==0?root_height:.1f,0}},{"children",Json::array()}});
        if(parent[i]>=0)doc["nodes"][parent[i]]["children"].push_back(i);
        human[bones[i]]={{"node",i}};}
    doc["nodes"].push_back({{"mesh",0}});
    doc["extensions"]["VRMC_vrm"]={{"specVersion","1.0"},{"meta",{{"name",name},{"authors",{"Fixture"}}}},{"humanoid",{{"humanBones",human}}}};
    const float positions[]={0,0,0,1,0,0,0,1,0},normals[]={0,0,1,0,0,1,0,0,1};Bytes bin(sizeof positions+sizeof normals);
    std::memcpy(bin.data(),positions,sizeof positions);std::memcpy(bin.data()+sizeof positions,normals,sizeof normals);
    doc["bufferViews"]=Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",sizeof positions}},{{"buffer",0},{"byteOffset",sizeof positions},{"byteLength",sizeof normals}}});
    doc["accessors"]=Json::array({{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"}},{{"bufferView",1},{"componentType",5126},{"count",3},{"type","VEC3"}}});
    doc["meshes"]=Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",0},{"NORMAL",1}}},{"material",0},{"mode",4}}})}}});
    doc["buffers"]=Json::array({{{"byteLength",bin.size()}}});auto text=doc.dump();while(text.size()%4)text+=' ';
    Bytes out;const auto word=[&](std::uint32_t v){const auto* p=reinterpret_cast<const std::uint8_t*>(&v);out.insert(out.end(),p,p+4);};
    word(0x46546c67);word(2);word(28+std::uint32_t(text.size()+bin.size()));word(std::uint32_t(text.size()));word(0x4e4f534a);
    out.insert(out.end(),text.begin(),text.end());word(std::uint32_t(bin.size()));word(0x004e4942);out.insert(out.end(),bin.begin(),bin.end());return out;
}
