#include "../src/debug_report_sources.hpp"
#include "../src/debug_report_policy.hpp"
#include "../src/enshrouded_bridge.hpp"
#include "../src/avatar_model.hpp"
#include "../src/avatar_service_types.hpp"
#include "../src/mod_settings.hpp"
#include "../src/native_body_coverage.hpp"
#include <iostream>
#include <stdexcept>
void need(bool value,const char* text){if(!value)throw std::runtime_error(text);}
std::array<std::uint8_t,32> hex(const char* value){std::array<std::uint8_t,32> result{};const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};for(unsigned i=0;i<32;++i)result[i]=std::uint8_t(digit(value[i*2])*16+digit(value[i*2+1]));return result;}
int main(){try{
    evrm::NativeBodyShaderIdentity terrain;terrain.vertex_bytes=35200;terrain.fragment_bytes=3696;
    terrain.vertex_digest=hex("dc1b3a77be743a3bae9422d76d1f4c5f195405b851d3211eab6cdd70b7ed4dac");
    terrain.fragment_digest=hex("8f31a706d837f9c14acd2ba51d1325631e1e1a96961cab3acd4b27db0de293b0");
    need(evrm::classify_debug_report_scene(terrain)==evrm::DebugReportSceneFamily::Terrain,"exact terrain classification missing");
    terrain.fragment_digest[0]^=1;need(evrm::classify_debug_report_scene(terrain)==evrm::DebugReportSceneFamily::Unknown,"partial terrain signature trusted");
    evrm::NativeBodyShaderIdentity building;building.vertex_bytes=8036;building.fragment_bytes=5284;
    building.vertex_digest=hex("13e2ccd24b50e99c55a8001a6f77144f57931fa0f6c742dd9fe3f9dce59d06f4");
    building.fragment_digest=hex("db9368b153c6ed5864ec205c3f1fd9b9c9a5dc1950481b468f8064916266d51f");
    need(evrm::classify_debug_report_scene(building)==evrm::DebugReportSceneFamily::Building,"exact building classification missing");
    for(unsigned i=0;i<20;++i)evrm::debug_report_presented(1000000+i*33333,1,false,false);
    evrm::debug_report_begin_sources(7,1000);
    evrm::PlayerFrame frame;frame.parts.resize(1);frame.map_position_known=true;frame.map_position={3507.75,1047,1981.75};
    for(unsigned i=0;i<4;++i){frame.actor_world[i*4+i]=1;frame.camera_world_to_clip[i*4+i]=1;}
    evrm::debug_report_observe_player(frame,1010);evrm::debug_report_observe_scene(evrm::DebugReportSceneFamily::Terrain);
    evrm::debug_report_observe_scene(evrm::DebugReportSceneFamily::Building);evrm::debug_report_observe_view(1920,1080,126,1);
    evrm::ModSettings settings;settings.microphone_device=std::make_shared<const std::string>("SECRET_CANARY");settings.voice_enabled=true;
    evrm::AvatarMenuSnapshot menu;menu.name="SECRET_CANARY";menu.path="C:/Users/Alice/private.vrm";
    evrm::debug_report_observe_settings(settings,&menu);
    evrm::PreparedAvatar prepared;prepared.name="SECRET_CANARY";prepared.path=menu.path;
    auto asset=std::make_shared<vrm::Avatar>();asset->title="SECRET_CANARY";asset->authors={"SECRET_CANARY"};asset->meshes.resize(1);
    asset->meshes[0].primitives.resize(1);asset->meshes[0].primitives[0].vertices.resize(3);asset->meshes[0].primitives[0].indices={0,1,2};
    asset->materials.resize(1);asset->materials[0].name="SECRET_CANARY";prepared.asset=asset;evrm::debug_report_observe_model(&prepared);
    evrm::debug_report_end_source_frame(1020);need(!evrm::debug_report_sources_ready(7),"request was completed before two scene frames");
    evrm::debug_report_end_source_frame(1030);need(evrm::debug_report_sources_ready(7),"source request did not complete");
    need(!evrm::debug_report_sources_pending(),"render collection remained active after its two-frame budget");
    auto data=evrm::debug_report_take_sources(7,1030);
    need(data.scene.location.available&&data.scene.location.local_world_player&&data.scene.location.xyz==frame.map_position,"qualified location lost");
    need(data.scene.terrain_draws==1&&data.scene.building_draws==1&&data.scene.width==1920,"scene counters not scoped to request");
    need(data.model.vertices==3&&data.model.indices==3&&data.model.materials==1&&data.settings.voice_enabled,"safe model/settings observation lost");
    need(!data.frame_intervals_us.empty(),"bounded pre-request frame intervals missing");
    data.diagnostics=evrm::debug_report_engine_events("[E 00:00] [graphics] VK_ERROR_DEVICE_LOST C:/Users/Alice SECRET_CANARY\n[I 00:01] private chat SECRET_CANARY\n");
    need(data.diagnostics.find("code=3")!=std::string::npos&&data.diagnostics.find("SECRET")==std::string::npos,"engine error normalization leaked or lost category");
    for(const auto& file:evrm::make_debug_report_contents(data))if(file.name=="diagnostics.log"){
        const std::string text(file.bytes.begin(),file.bytes.end());need(text.find("code=3")!=std::string::npos,"safe engine category was dropped at final export boundary");}
    for(const auto& file:evrm::make_debug_report_contents(data)){const std::string text(file.bytes.begin(),file.bytes.end());need(text.find("SECRET_CANARY")==std::string::npos&&text.find("Alice")==std::string::npos,"private model or microphone metadata copied");}
    need(!evrm::debug_report_take_sources(8,1030).scene.location.available,"different job borrowed scene data");
    evrm::debug_report_cancel_sources(7);evrm::debug_report_begin_sources(8,2000);frame.map_position_known=false;
    evrm::debug_report_observe_player(frame,2010);evrm::debug_report_end_source_frame(2020);evrm::debug_report_end_source_frame(2030);
    need(!evrm::debug_report_take_sources(8,2030).scene.location.available,"preview/unqualified or previous-job coordinates borrowed");
    evrm::debug_report_cancel_sources(8);evrm::debug_report_presented(300000000,1,true,false);evrm::debug_report_begin_sources(9,3000);
    need(evrm::debug_report_take_sources(9,3000).frame_intervals_us.empty(),"long-idle history was incorrectly reported as recent timing evidence");
    std::cout<<"PASS readonly sources: exact terrain/building families, qualified game map location, scoped frames and private model/device fields omitted\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL report sources: "<<error.what()<<'\n';return 1;}}
