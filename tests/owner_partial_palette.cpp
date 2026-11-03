#include "../src/enshrouded_bridge.hpp"
#include "../src/native_pose_owner.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>

namespace {
using Json=nlohmann::json;
const std::string hash(64,'2');
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void u32(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint32_t value){std::memcpy(bytes.data()+at,&value,4);}
void f32(std::vector<std::uint8_t>& bytes,std::size_t at,float value){std::memcpy(bytes.data()+at,&value,4);}
std::vector<unsigned> short_head(){return {16,17,21,22,23,25,26,27,28,29,30,31,32,33};}
std::vector<unsigned> long_head(){auto result=short_head();for(unsigned j=34;j<=38;++j)result.push_back(j);return result;}
std::vector<unsigned> body(){return {1,11,12,15,70,72,74,40,42,44,6,7,8,2,3,4};}
Json descriptor(unsigned vertices,unsigned indices,Json palettes,bool reject,std::vector<std::string> guids){
    Json matches=Json::array();for(const auto& guid:guids)matches.push_back({{"guid",guid}});
    return {{"nativePositionScale",{2,3,4}},{"positionOffset",{.1,.2,.3}},
        {"must_reject_pose_binding",reject},{"palette_options",std::move(palettes)},
        {"auxiliary_palette_options",Json::array({Json::array()})},{"matches",std::move(matches)},
        {"vertexCount",vertices},{"indexCount",indices}};
}
struct Metadata {Json profile,catalog;std::vector<unsigned> torso=body(),prefix=short_head();};
Metadata metadata(const std::string& mode){
    Metadata out;const Json identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    out.profile={{"schema","enshrouded-native-rig-profile-v1"},{"game",{{"executable_sha256",hash}}},
        {"hierarchy_guid","synthetic"},{"joint_count",102},{"source_rest_global",Json::array()},{"semantic_to_joint",Json::object()}};
    for(unsigned n=0;n<102;++n)out.profile["source_rest_global"].push_back(identity);
    const std::vector<std::pair<std::string,unsigned>> semantics={{"hips",1},{"spine",11},{"chest",12},{"upperChest",15},{"neck",16},{"head",17},
        {"leftUpperArm",70},{"leftLowerArm",72},{"leftHand",74},{"rightUpperArm",40},{"rightLowerArm",42},{"rightHand",44},
        {"leftUpperLeg",6},{"leftLowerLeg",7},{"leftFoot",8},{"rightUpperLeg",2},{"rightLowerLeg",3},{"rightFoot",4}};
    for(const auto& item:semantics)out.profile["semantic_to_joint"][item.first]=item.second;
    if(mode=="missing-core")out.torso.erase(out.torso.begin());
    if(mode=="duplicate-joint")out.torso.push_back(17);
    auto head=descriptor(20,30,Json::array({short_head(),long_head()}),true,{"head-a","head-b"});
    head["owner_partial_palette"]={{"policy","owned-prefix-v1"},{"palette_joint_ids",short_head()}};
    if(mode=="uncertified"||mode=="independent-reject"||mode=="full-short"||mode=="full-long"||mode=="full-bad-tail")head.erase("owner_partial_palette");
    if(mode=="independent-reject")head["palette_options"]=Json::array({short_head()});
    if(mode=="full-short"){head["palette_options"]=Json::array({short_head()});head["must_reject_pose_binding"]=false;}
    if(mode=="full-long"||mode=="full-bad-tail"){head["palette_options"]=Json::array({long_head()});head["must_reject_pose_binding"]=false;}
    if(mode=="bad-policy")head["owner_partial_palette"]["policy"]="unknown";
    if(mode=="extra-field")head["owner_partial_palette"]["extra"]=true;
    if(mode=="wrong-prefix")head["owner_partial_palette"]["palette_joint_ids"]=Json::array({16});
    if(mode=="wrong-type")head["owner_partial_palette"]="owned-prefix-v1";
    if(mode=="nonprefix"){auto different=long_head();different[5]=99;head["palette_options"]=Json::array({short_head(),different});}
    if(mode=="policy-on-independent")head["palette_options"]=Json::array({short_head()});
    if(mode=="policy-flag-false")head["must_reject_pose_binding"]=false;
    if(mode=="auxiliary"||mode=="auxiliary-ambiguity"){
        auto prefix=short_head(),larger=long_head();prefix.insert(prefix.begin(),0);larger.insert(larger.begin(),0);
        head["palette_options"]=Json::array({prefix,larger});head["owner_partial_palette"]["palette_joint_ids"]=prefix;
        head["auxiliary_palette_options"]=mode=="auxiliary"?Json::array({Json::array({0})}):Json::array({Json::array(),Json::array({0})});
    }
    if(mode=="mapped-tail")out.profile["semantic_to_joint"]["optionalTail"]=34;
    Json entries=Json::array({descriptor(10,15,Json::array({out.torso}),false,{"body"}),head});
    if(mode=="overlap"||mode=="rejecting-overlap"){
        auto other=head;other.erase("owner_partial_palette");
        other["nativePositionScale"][0]=2.0000015;entries.push_back(std::move(other));
    }
    if(mode=="guid-disagree"||mode=="guid-agree")entries.push_back(descriptor(21,33,Json::array({short_head()}),false,{mode=="guid-agree"?"head-b":"head-other"}));
    out.catalog={{"schema","enshrouded-player-catalog-v2"},{"game",{{"executable_sha256",hash}}},
        {"hierarchy_guid","synthetic"},{"joint_count",102},{"match_tolerance_native_units",1e-6},{"descriptor_signatures",std::move(entries)}};
    return out;
}
evrm::NativeOwnerModel model(unsigned slot,unsigned index){evrm::NativeOwnerModel m;m.equipment_slot=slot;m.index=index;m.generation=1;m.handle=0x01000000u|index;m.record=0x20000+index*256;return m;}
evrm::NativePoseOwner owner(){
    evrm::NativePoseOwner result;result.source_epoch=9;result.source.info={reinterpret_cast<VkBuffer>(0x501),0,384};result.source.size=384;
    result.source.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;result.source.generation=7;
    auto& c=result.candidate;c.image.base=0x140000000;c.image.size=0x100000;c.image.sha256[0]=1;
    c.root=0x10000;c.client=0x11000;c.controller=0x12000;c.entity=0x13000;c.entity_id=1;c.entity_generation=3;c.account=71;c.connection=64;
    c.native_buffer=0x501;c.native_buffer_bytes=384;c.instance_capacity=3;c.models={model(0,0),model(1,1)};
    evrm::NativeOwnerGuard guard;guard.address=0x30000;guard.bytes={1};c.guards=evrm::NativeOwnerGuards({guard});return result;
}
evrm::GpuSnapshot capture(const Metadata& data,const std::string& mode){
    const bool larger=mode=="long"||mode=="bad-tail"||mode=="full-long"||mode=="full-bad-tail";
    const auto head=larger?long_head():short_head();const auto matrices=data.torso.size()+head.size();
    evrm::GpuSnapshot snapshot;snapshot.source_epoch=9;snapshot.captured_milliseconds=100;snapshot.sequence=1;
    for(const auto& item:std::vector<std::pair<const char*,std::size_t>>{{"camera",1024},{"instances",384},{"skin",48*matrices},{"mesh-draw",240},{"culled-instances",24}}){auto& b=snapshot.buffers[item.first];b.bytes.resize(item.second);b.binding.range=item.second;}
    auto& instances=snapshot.buffers.at("instances");instances.binding.buffer=reinterpret_cast<VkBuffer>(0x501);
    auto& camera=snapshot.buffers.at("camera").bytes;for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)f32(camera,848+r*16+c*4,r==c?1.f:0.f);
    auto& skin=snapshot.buffers.at("skin").bytes;
    std::vector<unsigned> joints=data.torso;joints.insert(joints.end(),head.begin(),head.end());
    for(std::size_t i=0;i<joints.size();++i){f32(skin,i*48,1);f32(skin,i*48+20,1);f32(skin,i*48+40,1);f32(skin,i*48+12,joints[i]*.01f);}
    if(mode=="bad-tail"||mode=="full-bad-tail")for(std::size_t i=data.torso.size()+short_head().size();i<joints.size();++i)f32(skin,i*48,std::nanf(""));
    if(mode=="duplicate-joint")f32(skin,(data.torso.size()-1)*48+12,1.5f);
    if(mode=="truncated-prefix")skin.resize(data.torso.size()*48+short_head().size()*48-1);
    auto& mesh=snapshot.buffers.at("mesh-draw").bytes;
    for(unsigned i=0;i<3;++i){for(unsigned n=0;n<3;++n){f32(mesh,i*80+n*4,float(n+2)/2097151.f);f32(mesh,i*80+16+n*4,float(n+1)*.1f);}u32(mesh,i*80+36,i==0?15:i==1?30:33);u32(mesh,i*80+44,i==0?10:i==1?20:21);}
    auto& bytes=instances.bytes;
    for(unsigned i=0;i<2;++i){f32(bytes,i*128+12,1);f32(bytes,i*128+28,1);u32(bytes,i*128+108,i?unsigned(data.torso.size()):0);u32(bytes,i*128+120,100+i);}
    if(mode=="two-groups")u32(bytes,128,5*131072);
    auto& culled=snapshot.buffers.at("culled-instances").bytes;u32(culled,0,0);u32(culled,4,0);u32(culled,8,1);u32(culled,12,1);u32(culled,16,2);u32(culled,20,1);
    snapshot.culled_spans={{0,mode=="undrawn"?1u:(mode=="guid-agree"||mode=="guid-disagree"?3u:2u)}};
    return snapshot;
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    try{
        need(argc==3,"fixture folder and mode required");const std::filesystem::path folder=argv[1];const std::string mode=argv[2];
        auto data=metadata(mode);std::ofstream(folder/"profile.json")<<data.profile.dump();std::ofstream(folder/"catalog.json")<<data.catalog.dump();
        evrm::EnshroudedBridge bridge(folder/"profile.json",folder/"catalog.json",hash);
        const bool invalid_policy=mode=="bad-policy"||mode=="extra-field"||mode=="wrong-prefix"||mode=="wrong-type"||mode=="nonprefix"||mode=="policy-on-independent"||mode=="policy-flag-false"||mode=="auxiliary"||mode=="auxiliary-ambiguity"||mode=="mapped-tail"||mode=="overlap"||mode=="rejecting-overlap";
        if(invalid_policy){need(!bridge.ready(),"invalid or conflicting partial-palette certificate accepted");std::cout<<"PASS "<<mode<<" policy refused\n";return 0;}
        need(bridge.ready(),bridge.error().c_str());auto snapshot=capture(data,mode);auto selected=owner();
        if(mode=="accountless")selected.candidate.account=0;
        if(mode=="bad-connection")selected.candidate.connection=65;
        if(mode=="zero-connection")selected.candidate.connection=0;
        if(mode=="out-of-roster"){selected.candidate.connection=80;selected.candidate.entity_id=17;}
        if(mode=="wrong-source")snapshot.buffers.at("instances").binding.buffer=reinterpret_cast<VkBuffer>(0x502);
        if(mode=="wrong-epoch")++selected.source_epoch;
        if(mode=="preview-owner")snapshot.preview_subject.source.epoch=9;
        if(mode=="bad-handle")selected.candidate.models[1].handle=0;
        if(mode=="unowned")selected.candidate.models.resize(1);
        const auto frame=bridge.read_player_frame(snapshot,mode=="ownerless"?nullptr:&selected);
        const bool accepted=mode=="short"||mode=="long"||mode=="bad-tail"||mode=="guid-agree"||mode=="full-short"||mode=="full-long";
        if(accepted){
            need(bool(frame),frame.error.c_str());need(frame.parts.size()==2,"partial observation lost an owned part");
            const auto expected=mode=="full-long"?long_head():short_head();need(frame.parts[1].palette_joint_ids==expected,"wrong observed palette slots");
            const std::vector<std::string> guids=mode=="guid-agree"?std::vector<std::string>{"head-b"}:std::vector<std::string>{"head-a","head-b"};
            need(frame.parts[1].native_model_guids==guids,"partial observation guessed or lost GUID provenance");
            need(frame.source_pose.current.size()==18&&std::abs(frame.source_pose.current.at("head")[12]-.17f)<1e-6f&&std::abs(frame.source_pose.current.at("neck")[12]-.16f)<1e-6f,"common core semantics are not the actual prefix matrices");
        }else need(!frame&&frame.parts.empty()&&frame.source_pose.current.empty(),"unqualified or inconsistent pose was published");
        // A refusal must identify the missing prerequisite without exposing the
        // account/connection values. These expectations are hand-derived cases.
        std::string context;
        if(mode=="ownerless")context="owner-filter=0 account-present=0 association-valid=0 preview-tagged=0 partial-policy=1 partial-eligible=0";
        if(mode=="accountless")context="owner-filter=1 account-present=0 association-valid=1 preview-tagged=0 partial-policy=1 partial-eligible=0";
        if(mode=="bad-connection"||mode=="zero-connection"||mode=="out-of-roster")context="owner-filter=1 account-present=1 association-valid=0 preview-tagged=0 partial-policy=1 partial-eligible=0";
        if(mode=="preview-owner")context="owner-filter=1 account-present=1 association-valid=1 preview-tagged=1 partial-policy=1 partial-eligible=0";
        if(mode=="uncertified"||mode=="independent-reject")context="owner-filter=1 account-present=1 association-valid=1 preview-tagged=0 partial-policy=0 partial-eligible=1";
        if(!context.empty())need(frame.diagnostic.find(context)==0,"palette refusal does not identify the absent caller prerequisite");
        std::cout<<"PASS "<<mode<<" actual owner-filtered partial/full palette behavior\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
