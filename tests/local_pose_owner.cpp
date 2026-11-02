#include "../src/enshrouded_bridge.hpp"
#include "../src/native_owner.hpp"
#include "../src/native_state.hpp"
#include "../vendor/tinygltf/json.hpp"
#if __has_include("../src/native_pose_owner.hpp")
#include "../src/native_pose_owner.hpp"
#else
// Let the old production reader run for a behavioral RED, without fake filtering.
namespace evrm {struct NativePoseOwner {NativeOwnerCandidate candidate;NativeBufferBinding source;std::uint64_t source_epoch=0;};}
#endif
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Json=nlohmann::json;
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void u32(std::vector<std::uint8_t>& data,std::size_t at,std::uint32_t value){std::memcpy(data.data()+at,&value,4);}
void f32(std::vector<std::uint8_t>& data,std::size_t at,float value){std::memcpy(data.data()+at,&value,4);}
template<class B>auto read(B& bridge,const evrm::GpuSnapshot& snapshot,const evrm::NativePoseOwner& owner,int)->decltype(bridge.read_player_frame(snapshot,&owner)){return bridge.read_player_frame(snapshot,&owner);}
template<class B>evrm::PlayerFrame read(B& bridge,const evrm::GpuSnapshot& snapshot,const evrm::NativePoseOwner&,long){return bridge.read_player_frame(snapshot);}
template<class F>auto map_known(const F& frame,int)->decltype(frame.map_position_known){return frame.map_position_known;}
template<class F>bool map_known(const F&,long){return false;}
template<class F>auto map_xyz(const F& frame,int)->decltype(frame.map_position){return frame.map_position;}
template<class F>std::array<double,3> map_xyz(const F&,long){return {};}
const std::string hash(64,'2');
void metadata(const std::filesystem::path& folder,bool owned_reject,bool foreign_reject){
    const Json identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    Json profile={{"schema","enshrouded-native-rig-profile-v1"},{"game",{{"executable_sha256",hash}}},{"hierarchy_guid","synthetic"},
        {"joint_count",1},{"source_rest_global",Json::array({identity})},{"semantic_to_joint",Json::object()}};
    for(const auto* name:{"hips","spine","chest","upperChest","neck","head","leftUpperArm","leftLowerArm","leftHand","rightUpperArm","rightLowerArm","rightHand","leftUpperLeg","leftLowerLeg","leftFoot","rightUpperLeg","rightLowerLeg","rightFoot"})profile["semantic_to_joint"][name]=0;
    Json entries=Json::array();
    for(unsigned i=0;i<2;++i)entries.push_back(Json{{"nativePositionScale",{2,3,4}},{"positionOffset",{.1,.2,.3}},
        {"must_reject_pose_binding",i?foreign_reject:owned_reject},{"palette_options",Json::array({Json::array({0})})},
        {"auxiliary_palette_options",Json::array({Json::array()})},{"matches",Json::array({Json{{"guid",i?"synthetic-foreign":"synthetic-local"}}})},
        {"vertexCount",10+i},{"indexCount",15+i}});
    Json catalog={{"schema","enshrouded-player-catalog-v2"},{"game",{{"executable_sha256",hash}}},{"hierarchy_guid","synthetic"},
        {"joint_count",1},{"match_tolerance_native_units",1e-6},{"descriptor_signatures",entries}};
    std::ofstream(folder/"profile.json")<<profile.dump();std::ofstream(folder/"catalog.json")<<catalog.dump();
}
evrm::GpuSnapshot capture(unsigned actors){
    evrm::GpuSnapshot result;result.source_epoch=9;result.captured_milliseconds=100;result.sequence=1;
    for(const auto& item:{std::pair<const char*,std::size_t>{"camera",1024},{"instances",128*actors},{"skin",96},{"mesh-draw",160},{"culled-instances",8*actors}}){
        auto& buffer=result.buffers[item.first];buffer.bytes.resize(item.second);buffer.binding.range=item.second;
    }
    auto& instance_buffer=result.buffers.at("instances");instance_buffer.binding.buffer=reinterpret_cast<VkBuffer>(0x501);instance_buffer.binding.range=384;
    auto& camera=result.buffers.at("camera").bytes;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)f32(camera,848+r*16+c*4,r==c?1.f:0.f);
    auto& skin=result.buffers.at("skin").bytes;
    for(unsigned i=0;i<2;++i){f32(skin,i*48,1);f32(skin,i*48+20,1);f32(skin,i*48+40,1);}
    auto& mesh=result.buffers.at("mesh-draw").bytes;
    for(unsigned i=0;i<2;++i){for(unsigned n=0;n<3;++n){f32(mesh,i*80+n*4,float(n+2)/2097151.f);f32(mesh,i*80+16+n*4,float(n+1)*.1f);}u32(mesh,i*80+36,15+i);u32(mesh,i*80+44,10+i);}
    auto& instances=instance_buffer.bytes;auto& culled=result.buffers.at("culled-instances").bytes;
    for(unsigned i=0;i<actors;++i){const auto at=i*128;u32(instances,at,i*5*131072);f32(instances,at+12,1);f32(instances,at+28,1);u32(instances,at+108,i);u32(instances,at+120,100+i);u32(culled,i*8,i);u32(culled,i*8+4,i);}
    result.culled_spans.push_back({0,actors});return result;
}
evrm::NativeOwnerModel model(unsigned slot,unsigned index){evrm::NativeOwnerModel result;result.equipment_slot=slot;result.index=index;result.generation=1;result.handle=0x01000000u|index;result.record=0x20000+index*256;return result;}
evrm::NativePoseOwner owner(){
    evrm::NativePoseOwner result;result.source_epoch=9;result.source.info={reinterpret_cast<VkBuffer>(0x501),0,384};result.source.size=384;
    result.source.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;result.source.generation=7;
    auto& candidate=result.candidate;candidate.image.base=0x140000000;candidate.image.size=0x100000;candidate.image.sha256[0]=1;
    candidate.root=0x10000;candidate.client=0x11000;candidate.controller=0x12000;candidate.entity=0x13000;candidate.entity_id=4;candidate.entity_generation=3;
    candidate.native_buffer=0x501;candidate.native_buffer_bytes=384;candidate.instance_capacity=3;candidate.models={model(0,0)};
    evrm::NativeOwnerGuard guard;guard.address=0x30000;guard.bytes={1};candidate.guards=evrm::NativeOwnerGuards({guard});return result;
}
}
int main(int argc,char** argv){try{
    need(argc==3,"folder and scenario required");const auto folder=std::filesystem::path(argv[1]);const std::string mode=argv[2];
    const bool mixed=mode=="foreign-palette"||mode=="foreign-actor"||mode=="foreign-missing"||mode=="foreign-bad-mesh"||mode=="owned-two-groups"||mode=="not-drawn-owned"||mode=="truncated-owned"||mode=="unfiltered-two"||mode=="shared-menu-source";
    metadata(folder,mode=="owned-palette",mode=="foreign-palette"||mode=="foreign-missing");
    evrm::EnshroudedBridge bridge(folder/"profile.json",folder/"catalog.json",hash);need(bridge.ready(),bridge.error().c_str());
    auto snapshot=capture(mixed?2:1);auto selection=owner();
    if(mode=="menu")snapshot.buffers.at("instances").binding.buffer=reinterpret_cast<VkBuffer>(0x502);
    if(mode=="shared-menu-source")snapshot.culled_spans={{1,1}};
    if(mode=="owned-two-groups")selection.candidate.models.push_back(model(1,1));
    if(mode=="not-drawn-owned")selection.candidate.models={model(0,2)};
    if(mode=="foreign-missing"||mode=="truncated-owned")snapshot.buffers.at("instances").bytes.resize(128);
    if(mode=="truncated-owned")selection.candidate.models={model(0,1)};
    if(mode=="foreign-bad-mesh")u32(snapshot.buffers.at("culled-instances").bytes,8,900000);
    if(mode=="owned-catalog-count"||mode=="owned-catalog-duplicates"||mode=="unfiltered-catalog")
        u32(snapshot.buffers.at("mesh-draw").bytes,36,999);
    if(mode=="owned-catalog-geometry")f32(snapshot.buffers.at("mesh-draw").bytes,16,9.f);
    if(mode=="owned-catalog-duplicates")snapshot.culled_spans={{0,1},{0,1}};
    if(mode=="map-world"||mode=="map-menu"){
        auto& bytes=snapshot.buffers.at("instances").bytes;auto& camera=snapshot.buffers.at("camera").bytes;
        for(unsigned axis=0;axis<3;++axis){const double world[]={3507.75,1047.0,1981.75};
            u32(bytes,axis*4,std::uint32_t(world[axis]*131072.0));u32(camera,816+axis*4,std::uint32_t((world[axis]-5)*131072.0));}
    }
    if(mode=="wrong-source"){selection.source.info.buffer=reinterpret_cast<VkBuffer>(0x502);selection.candidate.native_buffer=0x502;}
    if(mode=="wrong-epoch")++selection.source_epoch;
    if(mode=="empty-owner")selection.candidate.models.clear();
    if(mode=="no-guards")selection.candidate.guards={};
    if(mode=="duplicate-owner")selection.candidate.models.push_back(model(1,0));
    if(mode=="bad-handle")selection.candidate.models[0].handle=0;
    if(mode=="source-range")selection.source.info.range=128;
    if(mode=="source-generation")selection.source.generation=0;
    const auto frame=(mode=="menu"||mode=="map-menu"||mode=="unfiltered-two"||mode=="unfiltered-catalog")?bridge.read_player_frame(snapshot):read(bridge,snapshot,selection,0);
    if(mode=="foreign-palette"||mode=="foreign-actor"||mode=="foreign-missing"||mode=="foreign-bad-mesh"||mode=="menu"||mode=="map-world"||mode=="map-menu"){
        need(bool(frame),"unowned data rejected the local/menu pose");need(frame.parts.size()==1&&frame.parts[0].instance_id==0&&frame.parts[0].model_index==100,"wrong actor or resource-ID filtering");
        need(frame.diagnostic.empty(),"accepted pose retained a failure diagnostic");
        if(mode=="map-world")need(map_known(frame,0)&&map_xyz(frame,0)==std::array<double,3>{3507.75,1047.0,1981.75},"qualified map location is missing or camera-relative instead of native world coordinates");
        if(mode=="map-menu"||mode=="menu")need(!map_known(frame,0),"unfiltered preview published a local-world map position");
    }else{
        need(!frame&&frame.parts.empty()&&frame.source_pose.current.empty(),"invalid/ambiguous selected data published a pose");
        need(!map_known(frame,0),"rejected selection retained map location");
        if(mode=="owned-palette")need(frame.error=="Catalog rejects ambiguous palette signature","owned palette guard changed");
        if(mode=="owned-two-groups"||mode=="unfiltered-two")need(frame.error=="Ambiguous compatible actor groups","actor uniqueness guard changed");
        if(mode=="not-drawn-owned"||mode=="shared-menu-source")need(frame.error=="No catalog-compatible player parts in capture","allocated but undrawn slot or unowned same-allocation preview was selected");
        if(mode=="not-drawn-owned"||mode=="shared-menu-source")
            need(frame.diagnostic==std::string("owner-filter=1 owner-models=1 submitted-records=")+(mode=="not-drawn-owned"?"2":"1")+
                " selected-records=0 unique-selected=0 catalog-misses=0","absent owned draw records were not distinguished from catalog failure");
        if(mode=="owned-catalog-count"||mode=="owned-catalog-geometry"||mode=="owned-catalog-duplicates"||mode=="unfiltered-catalog"){
            need(frame.error=="No catalog-compatible player parts in capture","catalog miss changed the generic error/selection policy");
            const std::string prefix=mode=="unfiltered-catalog"?"owner-filter=0 owner-models=0":"owner-filter=1 owner-models=1";
            const std::string records=mode=="owned-catalog-duplicates"?"2":"1";
            need(frame.diagnostic==prefix+" submitted-records="+records+" selected-records="+records+
                " unique-selected=1 catalog-misses=1","catalog mismatch or repeated draw records were misclassified");
        }
    }
    std::cout<<"PASS "<<mode<<" local selection/guard behavior\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
