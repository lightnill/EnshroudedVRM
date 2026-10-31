#include "mouth_fixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
using namespace mouth_test;
namespace {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
vrm::ImportResult load(Fixture& fixture,vrm::ImportLimits limits={}){auto result=vrm::load_vrm_bytes(fixture.bytes(),limits);
    if(!result)std::fprintf(stderr,"import error: %s\n",result.error.c_str());
    return result;}
void optional_failure(Fixture fixture,vrm::ImportLimits limits={}){const auto result=load(fixture,limits);
    need(bool(result)&&result.avatar.meshes[0].primitives[0].vertices.size()==3,"optional mouth failure suppressed base avatar");
    need(!result.avatar.mouth&&!result.avatar.mouth_limitation.empty(),"optional mouth failure published partial data or no reason");}
}
int main(int argc,char** argv){
    Fixture modern;modern.bind("aa",.5);auto result=load(modern);need(bool(result),"baseline GLB invalid");
    need(result.avatar.mouth&&result.avatar.mouth->available==1,"standard VRM1 mouth target 136 was not imported");
    const auto& mouth=*result.avatar.mouth;need(mouth.ranges.size()==1&&mouth.ranges[0].node==15&&mouth.ranges[0].primitive==0,"VRM1 node binding leaked onto shared mesh instance");
    need(mouth.records.size()==3&&mouth.records[0].vertex==0&&mouth.records[1].vertex==1,"retained morph indices were not remapped");
    need(mouth.records[0].position[0][0]==.5f&&mouth.records[1].position[0][1]==1&&mouth.records[2].position[0][2]==1.5f,"literal delta/weight values wrong");
    need(mouth.records[0].normal[0][1]==.25f,"optional normal delta missing");
    Fixture legacy(true);legacy.bind("a",.5);result=load(legacy);need(bool(result)&&result.avatar.mouth,"legacy mouth not imported");
    need(result.avatar.mouth->ranges.size()==2&&result.avatar.mouth->records.size()==6,"legacy mesh binding did not reach both active instances");
    need(result.avatar.mouth->records[0].position[0][0]==.5f,"VRM0 weight 50 was not normalized");
    Fixture channels;const char* names[]={"aa","ih","ou","ee","oh"};for(unsigned n=0;n<5;++n)channels.bind(names[n],(n+1)*.2,n==4?136:0,15,n==2);
    result=load(channels);need(bool(result)&&result.avatar.mouth&&result.avatar.mouth->available==31&&result.avatar.mouth->binary==4,"five aliases/binary flags mapped incorrectly");
    for(unsigned n=0;n<5;++n)need(std::abs(result.avatar.mouth->records[0].position[n][0]-float((n+1)*.2))<1e-6,"canonical vowel order changed");
    Fixture multiple;multiple.bind("aa");multiple.doc["meshes"][0]["primitives"].push_back(multiple.doc["meshes"][0]["primitives"][0]);
    result=load(multiple);need(bool(result)&&result.avatar.mouth&&result.avatar.mouth->ranges.size()==2&&result.avatar.mouth->records.size()==6,"multiple primitive binding incomplete");
    Fixture sparse;sparse.bind("aa");const auto indices=sparse.accessor<std::uint8_t>({2},1,"SCALAR",5121);
    const auto values=sparse.accessor<float>({0,4,0},1,"VEC3",5126);
    auto& accessor=sparse.doc["accessors"][2];accessor.erase("bufferView");accessor["sparse"]={{"count",1},
        {"indices",{{"bufferView",sparse.doc["accessors"][indices]["bufferView"]},{"componentType",5121}}},
        {"values",{{"bufferView",sparse.doc["accessors"][values]["bufferView"]}}}};
    result=load(sparse);need(bool(result)&&result.avatar.mouth&&result.avatar.mouth->records[1].position[0][1]==4,"sparse morph override/remap failed");
    Fixture sparse_normal;sparse_normal.bind("aa");const auto ni=sparse_normal.accessor<std::uint8_t>({3},1,"SCALAR",5121);
    const auto nv=sparse_normal.accessor<float>({0,0,-.75f},1,"VEC3",5126);auto& normal_accessor=sparse_normal.doc["accessors"][3];normal_accessor.erase("bufferView");
    normal_accessor["sparse"]={{"count",1},{"indices",{{"bufferView",sparse_normal.doc["accessors"][ni]["bufferView"]},{"componentType",5121}}},
        {"values",{{"bufferView",sparse_normal.doc["accessors"][nv]["bufferView"]}}}};
    result=load(sparse_normal);need(bool(result)&&result.avatar.mouth&&result.avatar.mouth->records[2].normal[0][2]==-.75f&&result.avatar.mouth->records[0].normal[0][1]==0,"sparse normal delta import failed");
    Fixture duplicate_bind;duplicate_bind.bind("aa",.25);auto& binds=duplicate_bind.expression("aa")["morphTargetBinds"];binds.push_back(binds[0]);
    result=load(duplicate_bind);need(bool(result)&&result.avatar.mouth&&result.avatar.mouth->records[0].position[0][0]==.5f,"duplicate bind contributions did not accumulate");

    Fixture absent;optional_failure(absent);Fixture custom;custom.doc["extensions"]["VRMC_vrm"]["expressions"]["custom"]["aa"]={{"morphTargetBinds",Json::array({{{"node",15},{"index",136},{"weight",1}}})}};optional_failure(custom);
    for(int owner:{-1,999}){auto bad=modern;bad.expression("aa")["morphTargetBinds"][0]["node"]=owner;optional_failure(bad);}
    {auto bad=legacy;bad.expression("a")["binds"][0]["mesh"]=999;optional_failure(bad);}
    {auto bad=modern;bad.expression("aa")["morphTargetBinds"][0]["index"]=137;optional_failure(bad);}
    for(double weight:{-1.,1.01}){auto bad=modern;bad.expression("aa")["morphTargetBinds"][0]["weight"]=weight;optional_failure(bad);}
    {auto bad=modern;bad.expression("aa")["isBinary"]=1;optional_failure(bad);}
    {auto bad=legacy;auto& groups=bad.doc["extensions"]["VRM"]["blendShapeMaster"]["blendShapeGroups"];groups.push_back(groups[0]);optional_failure(bad);}
    {auto bad=modern;bad.doc["accessors"][2]["count"]=3;optional_failure(bad);}
    {auto bad=modern;bad.doc["accessors"][2]["componentType"]=5122;bad.doc["accessors"][2]["normalized"]=true;optional_failure(bad);}
    {auto bad=modern;bad.doc["accessors"][3]["type"]="VEC2";optional_failure(bad);}
    {auto bad=modern;const auto offset=bad.doc["bufferViews"][2]["byteOffset"].get<std::size_t>();bad.write_float(offset,std::numeric_limits<float>::quiet_NaN());optional_failure(bad);}
    {auto bad=modern;const auto offset=bad.doc["bufferViews"][3]["byteOffset"].get<std::size_t>();bad.write_float(offset,std::numeric_limits<float>::infinity());optional_failure(bad);}
    {auto bad=sparse;const auto view=bad.doc["accessors"][2]["sparse"]["indices"]["bufferView"].get<std::size_t>();
        bad.binary[bad.doc["bufferViews"][view]["byteOffset"].get<std::size_t>()]=255;optional_failure(bad);}
    {auto bad=modern;auto& expression=bad.expression("aa");expression["morphTargetBinds"]=Json::array();expression["materialColorBinds"]=Json::array({Json::object()});optional_failure(bad);}
    {auto bad=modern;auto& list=bad.expression("aa")["morphTargetBinds"];const auto item=list[0];for(unsigned n=1;n<33;++n)list.push_back(item);optional_failure(bad);}
    {Fixture bad(true);for(const char* name:{"a","i","u","e","o"})bad.bind(name);
        auto& groups=bad.doc["extensions"]["VRM"]["blendShapeMaster"]["blendShapeGroups"];
        for(auto& group:groups){const auto item=group["binds"][0];for(unsigned n=1;n<32;++n)group["binds"].push_back(item);}
        for(unsigned n=0;n<101;++n){const auto node=bad.doc["nodes"].size();bad.doc["nodes"].push_back({{"mesh",0}});bad.doc["scenes"][0]["nodes"].push_back(node);}
        optional_failure(bad);}
    {auto bad=legacy;auto& groups=bad.doc["extensions"]["VRM"]["blendShapeMaster"]["blendShapeGroups"];
        for(unsigned n=0;n<512;++n)groups.push_back({{"presetName","unknown"}});
        optional_failure(bad);}
    vrm::ImportLimits limits;limits.mouth_records=1;optional_failure(modern,limits);limits={};limits.mouth_source_vertex_visits=1;optional_failure(modern,limits);
    limits={};limits.mouth_bindings=0;optional_failure(modern,limits);limits={};limits.mouth_ranges=0;optional_failure(modern,limits);
    {auto bad=modern;bad.doc["bufferViews"][2]["byteOffset"]=1000000;need(!load(bad),"invalid underlying buffer bounds became an optional warning");}
    {auto bad=modern;bad.doc["meshes"][0]["weights"]=Json::array({.5});need(!load(bad),"nonzero default mesh morph weights accepted");}
    {auto bad=modern;bad.doc["nodes"][15]["weights"]=Json::array({.5});need(!load(bad),"nonzero default node morph weights accepted");}
    if(argc==2){const auto avatar=vrm::load_vrm(std::filesystem::u8path(argv[1]));need(bool(avatar)&&avatar.avatar.mouth,"existing avatar has no usable imported mouth data");
        std::printf("local avatar aggregate available=%u binary=%u ranges=%zu records=%zu retained-bytes=%zu\n",unsigned(avatar.avatar.mouth->available),unsigned(avatar.avatar.mouth->binary),avatar.avatar.mouth->ranges.size(),avatar.avatar.mouth->records.size(),avatar.avatar.mouth->records.size()*sizeof(vrm::MouthDeltaRecord));}
    std::puts("PASS standard mouth import, sparse/retained mapping, limits and base-avatar failure isolation");
}
