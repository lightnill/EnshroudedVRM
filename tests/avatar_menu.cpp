#include "../src/mod_menu.hpp"
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/imgui_internal.h"
#include <iostream>
#include <stdexcept>
void require(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
template<class T>auto set_memory_usage(T& value,int)->decltype(value.decoded_texture_bytes=0,value.retained_cpu_buffer_bytes=0,void()){
    value.decoded_texture_bytes=314572800;value.retained_cpu_buffer_bytes=419430400;
}
template<class T>void set_memory_usage(T&,long){}
int main(){try{
    ImGui::CreateContext();evrm::configure_mod_menu(1,false);auto& io=ImGui::GetIO();io.DisplaySize={1100,1200};io.DeltaTime=1.f/60;
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    evrm::ModSettings settings;evrm::ModMenuStatus status;auto snapshot=std::make_shared<evrm::AvatarMenuSnapshot>();
    snapshot->name="Model A";snapshot->path="C:\\fixture\\A.vrm";snapshot->fingerprint=std::string(64,'a');snapshot->preferences_loaded=true;
    snapshot->choices=std::make_shared<const std::vector<evrm::AvatarChoice>>(std::vector<evrm::AvatarChoice>{{snapshot->path,"Model A"},{"C:\\fixture\\B.vrm","Model B"}});
    snapshot->materials=std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"Skin","Skin",""});
    set_memory_usage(*snapshot,0);
    status.avatar=snapshot;bool expanded=false,close=false;evrm::ModMenuLayout layout;evrm::ModMenuPlacement placement;evrm::AvatarMenuAction action,last;
    std::string displayed;
    const auto frame=[&]{ImGui::NewFrame();ImGui::LogToBuffer();evrm::draw_mod_menu(settings,status,expanded,close,placement,&layout,&action);
        displayed=ImGui::GetCurrentContext()->LogBuffer.c_str();ImGui::LogFinish();ImGui::Render();if(action.kind!=evrm::AvatarActionKind::None)last=action;};
    const auto click=[&](evrm::ModMenuRect r){require(r.x1>r.x0&&r.y1>r.y0,"control has no bounds");last={};io.AddMousePosEvent((r.x0+r.x1)/2,(r.y0+r.y1)/2);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();};
    frame();frame();
    require(displayed.find("Decoded textures: 300.0 MiB")!=std::string::npos&&displayed.find("CPU buffers (estimate): 400.0 MiB")!=std::string::npos,"F8 does not show cached, separately labelled texture and CPU memory");
    click(layout.model);const auto second_model=layout.second_model;click(second_model);
    require(last.kind==evrm::AvatarActionKind::SelectModel&&last.path=="C:\\fixture\\B.vrm","model choice not emitted");
    snapshot->replacement_enabled=false;frame();frame();click(layout.model);click(second_model);
    require(last.kind==evrm::AvatarActionKind::None,"disabled replacement still accepted model selection");
    click(layout.replacement_toggle);require(last.kind==evrm::AvatarActionKind::SetReplacementEnabled&&last.replacement_enabled,"original-character toggle did not restore replacement");
    snapshot->replacement_enabled=true;frame();frame();
    click(layout.replacement_toggle);require(last.kind==evrm::AvatarActionKind::SetReplacementEnabled&&!last.replacement_enabled,"original-character toggle did not disable replacement");
    frame();frame();
    snapshot->preferences_loaded=false;frame();click(layout.appearance);require(placement.appearance_expanded,"appearance did not expand");click(layout.whole);
    require(layout.whole_matte.x1<=layout.whole_matte.x0&&last.kind==evrm::AvatarActionKind::None,"appearance edits opened before saved profiles loaded");
    snapshot->preferences_loaded=true;frame();frame();click(layout.whole);click(layout.whole_matte);
    require(last.kind==evrm::AvatarActionKind::SetWholePreset&&last.preset==evrm::AvatarShaderPreset::Matte&&last.fingerprint==snapshot->fingerprint,"whole preset action wrong");
    // Missing/wrong entries or crossed handlers must fail through actual ImGui clicks.
    struct PresetChoice {evrm::ModMenuRect evrm::ModMenuLayout::*whole;evrm::ModMenuRect evrm::ModMenuLayout::*material;evrm::AvatarShaderPreset preset;};
    const PresetChoice choices[]={
        {&evrm::ModMenuLayout::whole_enhanced,&evrm::ModMenuLayout::override_enhanced,evrm::AvatarShaderPreset::Enhanced},
        {&evrm::ModMenuLayout::whole_fast,&evrm::ModMenuLayout::override_fast,evrm::AvatarShaderPreset::Fast},
        {&evrm::ModMenuLayout::whole_game,&evrm::ModMenuLayout::override_game,evrm::AvatarShaderPreset::GameLighting},
        {&evrm::ModMenuLayout::whole_matte,&evrm::ModMenuLayout::override_matte,evrm::AvatarShaderPreset::Matte}};
    for(const auto& choice:choices){click(layout.whole);click(layout.*choice.whole);
        require(last.kind==evrm::AvatarActionKind::SetWholePreset&&last.preset==choice.preset&&last.fingerprint==snapshot->fingerprint,"Auto/Enhanced/Fast/Matte whole action wrong");}
    click(layout.material);click(layout.second_material);require(placement.material_index==1,"duplicate material labels share selection");
    click(layout.material_override);click(layout.override_game);
    require(last.kind==evrm::AvatarActionKind::SetMaterialPreset&&last.material==1&&last.preset==evrm::AvatarShaderPreset::GameLighting,"material action wrong");
    for(const auto& choice:choices){click(layout.material_override);click(layout.*choice.material);
        require(last.kind==evrm::AvatarActionKind::SetMaterialPreset&&last.material==1&&last.preset==choice.preset&&last.fingerprint==snapshot->fingerprint,"Auto/Enhanced/Fast/Matte material action wrong");}
    click(layout.material_override);click(layout.inherit);
    require(last.kind==evrm::AvatarActionKind::SetMaterialPreset&&!last.preset,"inherit action wrong");
    click(layout.reset_materials);require(last.kind==evrm::AvatarActionKind::ResetMaterials,"reset action wrong");
    snapshot->state=evrm::AvatarLoadState::Loading;frame();frame();click(layout.cancel_model);require(last.kind==evrm::AvatarActionKind::CancelLoad,"cancel action missing");
    snapshot->state=evrm::AvatarLoadState::Error;snapshot->error="Invalid model; current avatar kept";frame();frame();
    click(layout.browse);require(close&&last.kind==evrm::AvatarActionKind::Browse,"browse must release menu input");
    close=false;io.DisplaySize={400,480};frame();frame();require(layout.scroll_max_y>0&&placement.x>=8&&placement.y>=8&&placement.y+layout.height<=480,"small menu bounds/scroll wrong");
    ImGui::DestroyContext();std::cout<<"PASS avatar model/whole/material actions, duplicate labels, inheritance, reset, loading/cancel, browse-close and small viewport\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
