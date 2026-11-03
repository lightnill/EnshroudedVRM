#include "../src/mod_menu.hpp"
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/imgui_internal.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Keep the RED run behavioral even before the optional selector rectangles exist.
template<class T>auto player_rect(T& layout,int)->decltype(layout.player){return layout.player;}
template<class T>evrm::ModMenuRect player_rect(T&,long){return {};}
template<class T>auto second_player_rect(T& layout,int)->decltype(layout.second_player){return layout.second_player;}
template<class T>evrm::ModMenuRect second_player_rect(T&,long){return {};}
}
int main(){try{
    ImGui::CreateContext();evrm::configure_mod_menu(1,false);auto& io=ImGui::GetIO();io.DisplaySize={1100,1800};io.DeltaTime=1.f/60;
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    evrm::ModSettings settings;evrm::ModMenuStatus status;auto snapshot=std::make_shared<evrm::AvatarMenuSnapshot>();
    const std::string remote="private-subject-CANARY-47";
    snapshot->name="Model A";snapshot->path="C:\\fixture\\A.vrm";snapshot->fingerprint=std::string(64,'a');snapshot->preferences_loaded=true;
    snapshot->players=std::make_shared<const std::vector<evrm::AvatarPlayerChoice>>(std::vector<evrm::AvatarPlayerChoice>{{"","You",true},{remote,"Player 2",false},{"private-subject-CANARY-48","Player 2",false}});
    snapshot->choices=std::make_shared<const std::vector<evrm::AvatarChoice>>(std::vector<evrm::AvatarChoice>{{snapshot->path,"Model A"},{"C:\\fixture\\B.vrm","Model B"}});
    snapshot->materials=std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"Skin","Skin"});
    status.avatar=snapshot;bool expanded=false,close=false;evrm::ModMenuLayout layout;evrm::ModMenuPlacement placement;evrm::AvatarMenuAction action,last;
    std::string displayed;
    const auto frame=[&]{ImGui::NewFrame();ImGui::LogToBuffer();evrm::draw_mod_menu(settings,status,expanded,close,placement,&layout,&action);
        displayed=ImGui::GetCurrentContext()->LogBuffer.c_str();ImGui::LogFinish();require(displayed.find("private-subject-CANARY")==std::string::npos,"private subject rendered as text");
        ImGui::Render();if(action.kind!=evrm::AvatarActionKind::None)last=action;};
    const auto click=[&](evrm::ModMenuRect r){require(r.x1>r.x0&&r.y1>r.y0,"player control has no bounds");last={};io.AddMousePosEvent((r.x0+r.x1)/2,(r.y0+r.y1)/2);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();};
    const auto key=[&](ImGuiKey k){last={};io.AddKeyEvent(k,true);frame();io.AddKeyEvent(k,false);frame();frame();};
    const auto targeted=[&](evrm::AvatarActionKind kind){require(last.kind==kind&&last.subject==remote,"avatar action lost its captured remote target");};
    frame();frame();const auto selector=player_rect(layout,0);require(selector.x1>selector.x0,"Player selector missing above Avatar controls");
    require(selector.y1<layout.replacement_toggle.y0,"Player selector is not above Avatar controls");
    click(selector);require(displayed.find("You")!=std::string::npos&&displayed.find("Player 2")!=std::string::npos,"player labels missing");
    click(second_player_rect(layout,0));targeted(evrm::AvatarActionKind::SelectPlayer);
    require(last.path.empty()&&last.fingerprint.empty(),"player selection captured unrelated model data");
    // The service supplies the next selected snapshot; the UI never resolves identities.
    snapshot->subject=remote;snapshot->replacement_enabled=false;frame();frame();click(layout.replacement_toggle);targeted(evrm::AvatarActionKind::SetReplacementEnabled);require(last.replacement_enabled,"remote None cannot be enabled");
    snapshot->replacement_enabled=true;frame();frame();click(layout.replacement_toggle);targeted(evrm::AvatarActionKind::SetReplacementEnabled);require(!last.replacement_enabled,"remote None cannot be disabled");
    frame();frame();click(layout.model);click(layout.second_model);targeted(evrm::AvatarActionKind::SelectModel);require(last.path=="C:\\fixture\\B.vrm","remote model choice wrong");
    click(layout.refresh_models);targeted(evrm::AvatarActionKind::RefreshCatalogue);
    snapshot->state=evrm::AvatarLoadState::Loading;frame();frame();click(layout.cancel_model);targeted(evrm::AvatarActionKind::CancelLoad);
    click(layout.browse);targeted(evrm::AvatarActionKind::Browse);require(close,"Browse does not release menu input");close=false;
    // Previously emitted work must keep its own target when the displayed snapshot changes.
    const auto pending=last;snapshot->subject.clear();frame();require(pending.subject==remote,"selector redirected prior Browse action");snapshot->subject=remote;frame();
    click(layout.voice);require(settings.voice_enabled&&last.kind==evrm::AvatarActionKind::None,"remote row redirected local voice controls");
    click(layout.appearance);require(displayed.find("shared by players using this model")!=std::string::npos,"shared appearance hint missing");
    click(layout.whole);click(layout.whole_matte);targeted(evrm::AvatarActionKind::SetWholePreset);
    click(layout.material);click(layout.second_material);click(layout.material_override);click(layout.override_game);targeted(evrm::AvatarActionKind::SetMaterialPreset);require(last.material==1,"material choice wrong");
    click(layout.reset_materials);targeted(evrm::AvatarActionKind::ResetMaterials);
    // A real keyboard activation inside the player popup must select its focused row.
    click(player_rect(layout,0));io.AddMousePosEvent(0,0);frame();key(ImGuiKey_DownArrow);key(ImGuiKey_Enter);
    require(last.kind==evrm::AvatarActionKind::SelectPlayer&&last.subject=="private-subject-CANARY-48","keyboard player selection or duplicate-label IDs wrong");
    snapshot->subject.clear();frame();frame();click(layout.replacement_toggle);require(last.subject.empty(),"You inherited a remote action target");
    io.DisplaySize={400,480};frame();frame();require(layout.scroll_max_y>0&&placement.x>=8&&placement.y>=8&&placement.y+layout.height<=480,"roster does not adapt to small viewport");
    placement.appearance_expanded=false;frame();frame();
    io.AddMousePosEvent(placement.x+layout.width/2,placement.y+layout.height/2);frame();
    for(unsigned n=0;n<20&&player_rect(layout,0).y1>placement.y+layout.height-25;++n){io.AddMouseWheelEvent(0,-1);frame();frame();}
    const auto small_player=player_rect(layout,0);require(small_player.y0>placement.y+20&&small_player.y1<placement.y+layout.height-20,"small viewport cannot scroll to Player selector");
    click(small_player);click(second_player_rect(layout,0));targeted(evrm::AvatarActionKind::SelectPlayer);
    snapshot->players.reset();io.DisplaySize={1100,1800};frame();frame();require(player_rect(layout,0).x1==0,"legacy no-roster snapshot gained selector");
    ImGui::DestroyContext();std::cout<<"PASS player selector mouse/keyboard, duplicate labels, targeted model/None/Browse/cancel/shader actions, local voice, privacy text and small viewport\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
