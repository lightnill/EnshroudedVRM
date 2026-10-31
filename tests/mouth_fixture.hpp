#pragma once
#include "../vendor/tinygltf/json.hpp"
#include "../src/vrm_asset.hpp"
#include <cstring>
#include <cstdint>
#include <vector>
namespace mouth_test {
using Json=nlohmann::json;using Bytes=std::vector<std::uint8_t>;
struct Fixture {
    Json doc;Bytes binary;bool legacy=false;
    template<class T> int accessor(const std::vector<T>& values,unsigned count,const char* type,int component){
        while(binary.size()%4)binary.push_back(0);
        const auto offset=binary.size(),bytes=values.size()*sizeof(T);
        const auto* start=reinterpret_cast<const std::uint8_t*>(values.data());binary.insert(binary.end(),start,start+bytes);
        const auto view=doc["bufferViews"].size();doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",bytes}});
        const auto index=doc["accessors"].size();doc["accessors"].push_back({{"bufferView",view},{"componentType",component},{"count",count},{"type",type}});
        return int(index);
    }
    explicit Fixture(bool old=false,unsigned targets=137):legacy(old){
        doc={{"asset",{{"version","2.0"}}},{"scene",0},{"scenes",Json::array({{{"nodes",{0,15,16}}}})},
             {"nodes",Json::array()},{"skins",Json::array()},{"bufferViews",Json::array()},{"accessors",Json::array()},
             {"materials",Json::array({{{"pbrMetallicRoughness",{{"baseColorFactor",{1,1,1,1}}}}}})},
             {"extensionsUsed",Json::array({old?"VRM":"VRMC_vrm"})}};
        const char* names[]={"hips","spine","head","leftUpperLeg","leftLowerLeg","leftFoot","rightUpperLeg","rightLowerLeg","rightFoot","leftUpperArm","leftLowerArm","leftHand","rightUpperArm","rightLowerArm","rightHand"};
        const int parents[]={-1,0,1,0,3,4,0,6,7,1,9,10,1,12,13};Json human=Json::object();
        for(int n=0;n<15;++n){doc["nodes"].push_back({{"translation",{0,n==0?1.f:.1f,0}},{"children",Json::array()}});
            if(parents[n]>=0)doc["nodes"][parents[n]]["children"].push_back(n);
            human[names[n]]={{"node",n}};}
        doc["nodes"].push_back({{"mesh",0}});doc["nodes"].push_back({{"mesh",0},{"translation",{2,0,0}}});
        if(old){auto& vrm=doc["extensions"]["VRM"];vrm={{"specVersion","0.0"},{"meta",{{"title","Synthetic mouth fixture"}}},{"humanoid",{{"humanBones",Json::array()}}}};
            for(auto it=human.begin();it!=human.end();++it)vrm["humanoid"]["humanBones"].push_back({{"bone",it.key()},{"node",it.value()["node"]}});
        }else doc["extensions"]["VRMC_vrm"]={{"specVersion","1.0"},{"meta",{{"name","Synthetic mouth fixture"},{"authors",{"Fixture"}}}},{"humanoid",{{"humanBones",human}}}};
        const auto position=accessor<float>({9,9,9, 0,0,0, 1,0,0, 0,1,0},4,"VEC3",5126);
        const auto normal=accessor<float>({0,0,1, 0,0,1, 0,0,1, 0,0,1},4,"VEC3",5126);
        const auto delta=accessor<float>({.01f,0,0, 1,0,0, 0,2,0, 0,0,3},4,"VEC3",5126);
        const auto delta_normal=accessor<float>({0,0,0, 0,.5f,0, .5f,0,0, 0,0,-1},4,"VEC3",5126);
        const auto indices=accessor<std::uint16_t>({1,2,3},3,"SCALAR",5123);
        Json primitive={{"attributes",{{"POSITION",position},{"NORMAL",normal}}},{"indices",indices},{"material",0},{"mode",4},{"targets",Json::array()}};
        for(unsigned n=0;n<targets;++n)primitive["targets"].push_back({{"POSITION",delta},{"NORMAL",delta_normal}});
        doc["meshes"]=Json::array({{{"primitives",Json::array({primitive})}}});
    }
    void bind(const char* preset,double weight=1,int target=136,int owner=15,bool binary_value=false){
        if(legacy)doc["extensions"]["VRM"]["blendShapeMaster"]["blendShapeGroups"].push_back({{"presetName",preset},{"isBinary",binary_value},
            {"binds",Json::array({{{"mesh",owner==15?0:owner},{"index",target},{"weight",weight*100}}})}});
        else doc["extensions"]["VRMC_vrm"]["expressions"]["preset"][preset]={{"isBinary",binary_value},
            {"morphTargetBinds",Json::array({{{"node",owner},{"index",target},{"weight",weight}}})}};
    }
    Json& expression(const char* preset){return legacy?doc["extensions"]["VRM"]["blendShapeMaster"]["blendShapeGroups"][0]:doc["extensions"]["VRMC_vrm"]["expressions"]["preset"][preset];}
    void write_float(std::size_t offset,float value){std::memcpy(binary.data()+offset,&value,4);}
    Bytes bytes(){
        doc["buffers"]=Json::array({{{"byteLength",binary.size()}}});auto text=doc.dump();while(text.size()%4)text+=' ';
        auto payload=binary;while(payload.size()%4)payload.push_back(0);Bytes out;
        const auto word=[&](std::uint32_t value){const auto* p=reinterpret_cast<const std::uint8_t*>(&value);out.insert(out.end(),p,p+4);};
        word(0x46546c67);word(2);word(std::uint32_t(28+text.size()+payload.size()));word(std::uint32_t(text.size()));word(0x4e4f534a);
        out.insert(out.end(),text.begin(),text.end());word(std::uint32_t(payload.size()));word(0x004e4942);out.insert(out.end(),payload.begin(),payload.end());return out;
    }
};
}
