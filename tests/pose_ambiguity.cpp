#include "../src/enshrouded_bridge.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using Json=nlohmann::json;
namespace {
void need(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
// Keep the pre-instrumentation build runnable so missing detail fails behaviorally.
template<class T> auto detail(const T& frame,int)->decltype(frame.diagnostic){return frame.diagnostic;}
template<class T> std::string detail(const T&,long){return {};}
void u32(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint32_t value){std::memcpy(bytes.data()+at,&value,4);}
void f32(std::vector<std::uint8_t>& bytes,std::size_t at,float value){std::memcpy(bytes.data()+at,&value,4);}
const std::string hash(64,'1');
const char* names[]={"hips","spine","chest","upperChest","neck","head","leftUpperArm","leftLowerArm","leftHand",
                    "rightUpperArm","rightLowerArm","rightHand","leftUpperLeg","leftLowerLeg","leftFoot","rightUpperLeg","rightLowerLeg","rightFoot"};
evrm::GpuSnapshot capture(unsigned actors){
    evrm::GpuSnapshot snapshot;
    for(const auto& item:{std::pair<const char*,std::size_t>{"camera",1024},{"instances",128*actors},{"skin",48},{"mesh-draw",80},{"culled-instances",8*actors}}){
        auto& buffer=snapshot.buffers[item.first];buffer.bytes.resize(item.second);buffer.binding.range=item.second;
    }
    auto& camera=snapshot.buffers.at("camera").bytes;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)f32(camera,848+r*16+c*4,r==c?1.f:0.f);
    auto& skin=snapshot.buffers.at("skin").bytes;f32(skin,0,1);f32(skin,20,1);f32(skin,40,1);
    auto& mesh=snapshot.buffers.at("mesh-draw").bytes;
    for(unsigned i=0;i<3;++i){f32(mesh,i*4,float(i+2)/2097151.f);f32(mesh,16+i*4,float(i+1)*.1f);}
    u32(mesh,36,15);u32(mesh,44,10);
    auto& instances=snapshot.buffers.at("instances").bytes;
    auto& culled=snapshot.buffers.at("culled-instances").bytes;
    for(unsigned i=0;i<actors;++i){const auto at=i*128;u32(instances,at,i*131072);f32(instances,at+12,1);f32(instances,at+28,1);u32(instances,at+120,123+i);u32(culled,i*8,0);u32(culled,i*8+4,i);}
    snapshot.culled_spans.push_back({0,actors});return snapshot;
}
void metadata(const std::filesystem::path& folder,bool reject,bool long_guids=false){
    const Json identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    Json profile={{"schema","enshrouded-native-rig-profile-v1"},{"game",{{"executable_sha256",hash}}},
                  {"hierarchy_guid","synthetic"},{"joint_count",1},{"source_rest_global",Json::array({identity})},{"semantic_to_joint",Json::object()}};
    for(const auto* name:names)profile["semantic_to_joint"][name]=0;
    Json entry={{"nativePositionScale",{2,3,4}},{"positionOffset",{.1,.2,.3}},{"must_reject_pose_binding",reject},
                {"palette_options",Json::array({Json::array({0})})},{"auxiliary_palette_options",Json::array({Json::array()})},
                {"matches",Json::array({Json{{"guid","synthetic-player-part"}}})},{"vertexCount",10},{"indexCount",15}};
    if(long_guids)entry["matches"]=Json::array({Json{{"guid",std::string("first\n\r\t")+std::string(300,'a')}},
        Json{{"guid",std::string("second")+std::string(300,'b')}},Json{{"guid",std::string("third")+std::string(300,'c')}}});
    Json catalog={{"schema","enshrouded-player-catalog-v2"},{"game",{{"executable_sha256",hash}}},{"hierarchy_guid","synthetic"},
                  {"joint_count",1},{"match_tolerance_native_units",1e-6},{"descriptor_signatures",Json::array({entry})}};
    std::ofstream(folder/"profile.json")<<profile.dump();std::ofstream(folder/"catalog.json")<<catalog.dump();
}
}
int main(int argc,char** argv){try{
    need(argc==3,"temporary folder and scenario required");const auto folder=std::filesystem::path(argv[1]);const std::string mode=argv[2];
    metadata(folder,mode=="palette"||mode=="guid-bounds",mode=="guid-bounds");evrm::EnshroudedBridge bridge(folder/"profile.json",folder/"catalog.json",hash);need(bridge.ready(),bridge.error().c_str());
    auto snapshot=capture(mode=="many-groups"?100:mode=="many-parts"?12:mode=="groups"?2:1);
    if(mode=="many-parts")for(unsigned i=0;i<12;++i)u32(snapshot.buffers.at("instances").bytes,i*128,i<6?0:131072);
    if(mode=="generic")snapshot.buffers.at("camera").bytes.resize(4);
    const auto frame=bridge.read_player_frame(snapshot);
    const auto diagnostic=detail(frame,0);
    if(mode=="palette"){
        need(!frame&&frame.parts.empty()&&frame.source_pose.current.empty(),"ambiguous palette must remain rejected");
        need(frame.error=="Catalog rejects ambiguous palette signature","canonical palette failure changed");
        need(diagnostic.find("vertices=10")!=std::string::npos&&diagnostic.find("indices=15")!=std::string::npos,"missing ambiguous descriptor counts");
        need(diagnostic.find("mesh=0")!=std::string::npos&&diagnostic.find("instance=0")!=std::string::npos&&diagnostic.find("model=123")!=std::string::npos,"missing offending mesh/instance identity");
        need(diagnostic.find("synthetic-player-part")!=std::string::npos,"missing catalog provenance");
    }else if(mode=="groups"||mode=="many-groups"||mode=="many-parts"){
        need(!frame&&frame.parts.empty()&&frame.source_pose.current.empty(),"multiple actors must remain rejected");
        need(frame.error=="Ambiguous compatible actor groups","canonical group failure changed");
        need(diagnostic.find(mode=="many-groups"?"groups=100":"groups=2")!=std::string::npos&&diagnostic.find("model=123")!=std::string::npos&&diagnostic.find("model=124")!=std::string::npos,"missing conflicting actor identities");
        need(diagnostic.find("actor=[")!=std::string::npos,"missing actor transform comparison");
        if(mode=="many-groups")need(diagnostic.find("group3{")!=std::string::npos&&diagnostic.find("group4{")==std::string::npos,"actor group detail is not bounded");
        if(mode=="many-parts")need(diagnostic.find("model=126")!=std::string::npos&&diagnostic.find("model=127")==std::string::npos&&diagnostic.find("model=132")!=std::string::npos&&diagnostic.find("model=133")==std::string::npos,"part detail is not bounded per group");
    }else if(mode=="guid-bounds"){
        need(!frame&&frame.error=="Catalog rejects ambiguous palette signature","long provenance changed rejection");
        need(diagnostic.find("first???")!=std::string::npos&&diagnostic.find_first_of("\n\r\t")==std::string::npos,"control characters escaped the single log row");
        need(diagnostic.find(std::string(65,'a'))==std::string::npos&&diagnostic.find("third")==std::string::npos,"catalog provenance detail is not bounded");
    }else if(mode=="generic"){
        need(!frame&&frame.error=="Truncated capture buffer: camera"&&diagnostic.empty(),"generic rejection changed or inherited ambiguity detail");
    }else{
        need(mode=="valid"&&frame&&frame.parts.size()==1&&diagnostic.empty(),"valid capture changed or acquired failure diagnostics");
    }
    need(diagnostic.size()<=3072,"ambiguity detail exceeded its log bound");
    std::cout<<"PASS "<<mode<<" identity guard preserved; diagnostic-bytes="<<diagnostic.size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
