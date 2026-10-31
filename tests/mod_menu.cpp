#include "../src/mod_menu.hpp"
#include "../vendor/imgui/imgui.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){try{
    IMGUI_CHECKVERSION();ImGui::CreateContext();evrm::configure_mod_menu(1,false);
    auto& io=ImGui::GetIO();io.DisplaySize={1000,800};io.DeltaTime=1.f/60;unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    require(io.IniFilename==nullptr&&io.LogFilename==nullptr,"GUI file IO disabled");
    const auto& style=ImGui::GetStyle();require(style.Colors[ImGuiCol_WindowBg].w==1&&style.Colors[ImGuiCol_WindowBg].x<.12f,"opaque dark surface");
    for(auto color:{ImGuiCol_Text,ImGuiCol_TextDisabled,ImGuiCol_SliderGrab,ImGuiCol_SliderGrabActive,ImGuiCol_CheckMark,ImGuiCol_NavCursor}){
        const auto c=style.Colors[color];require(std::abs(c.x-c.y)<.025f&&std::abs(c.y-c.z)<.025f,"neutral controls/text");}
    evrm::ModSettings settings;evrm::ModMenuStatus status;bool expanded=false,close=false;evrm::ModMenuLayout layout;evrm::ModMenuPlacement placement;
    const auto frame=[&]{ImGui::NewFrame();const bool changed=evrm::draw_mod_menu(settings,status,expanded,close,placement,&layout);ImGui::Render();return changed;};
    frame();require(!settings.voice_enabled&&!expanded&&!close&&ImGui::GetDrawData()->TotalVtxCount>0,"compact off menu draws");
    require(layout.width==504,"approved logical width");
    const auto click=[&](evrm::ModMenuRect rectangle){const float x=(rectangle.x0+rectangle.x1)*.5f,y=(rectangle.y0+rectangle.y1)*.5f;
        io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);const bool changed=frame();frame();return changed;};
    require(click(layout.voice)&&settings.voice_enabled,"click toggles on");
    click(layout.expand);require(expanded&&settings.strength==85&&settings.sensitivity==50&&settings.smoothing_ms==60,"expansion keeps defaults");
    auto devices=std::make_shared<evrm::MicrophoneDeviceList>();devices->available=true;devices->devices.push_back({"capture-fixture","Test microphone"});status.microphones=devices;
    settings.voice_enabled=false;frame();frame();
    require(click(layout.source_microphone)&&settings.voice_source==evrm::VoiceInputSource::Microphone&&!settings.voice_enabled,"source selection while Off");
    click(layout.microphone_device);require(click(layout.first_device)&&evrm::microphone_device_id(settings)=="capture-fixture","specific microphone picker");
    click(layout.microphone_device);require(click(layout.default_device)&&evrm::microphone_device_id(settings).empty(),"Windows default microphone choice");
    settings.voice_enabled=true;status.microphone=evrm::MicrophoneState::AccessDenied;
    require(!std::strcmp(evrm::mod_voice_status(settings,status),"Microphone access denied"),"microphone permission status");
    require(click(layout.source_game)&&settings.voice_source==evrm::VoiceInputSource::Game,"game voice source restored");frame();frame();
    require(layout.scroll_max_y==0,"full-size expanded menu does not clip its footer");
    const auto compact_height=layout.height;
    // Click at the strength slider's right end and verify its live bounded value.
    io.AddMousePosEvent(layout.strength.x1-2,(layout.strength.y0+layout.strength.y1)*.5f);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();require(settings.strength>=98&&settings.strength<=100,"live strength slider");
    click(layout.voice);require(!settings.voice_enabled,"disable");const auto kept=settings.strength;
    click(layout.strength);require(settings.strength==kept,"disabled slider keeps value");
    click(layout.expand);require(!expanded,"collapse");frame();require(layout.height<compact_height,"compact panel smaller");
    click(layout.expand);require(expanded&&settings.strength==kept,"reopen retains tuning");
    require(!std::strcmp(evrm::mod_voice_status(settings,status),"Off"),"off status");settings.voice_enabled=true;status.starting=true;
    require(!std::strcmp(evrm::mod_voice_status(settings,status),"Starting"),"starting status");status.starting=false;status.voice=evrm::VoiceActivity::Waiting;
    require(!std::strcmp(evrm::mod_voice_status(settings,status),"Waiting for game voice"),"waiting status");status.voice=evrm::VoiceActivity::Talking;
    require(!std::strcmp(evrm::mod_voice_status(settings,status),"Talking"),"talking status");status.voice=evrm::VoiceActivity::UnsupportedSource;
    require(!std::strcmp(evrm::mod_voice_status(settings,status),"Voice to viseme unavailable"),"unavailable status");
    click(layout.close);require(close,"close button");close=false;expanded=false;settings.voice_enabled=false;
    io.ClearEventsQueue();io.ClearInputKeys();io.ClearInputMouse();io.AddMousePosEvent(0,0);frame();frame();
    const auto target=layout.voice;const float click_x=(target.x0+target.x1)*.5f,click_y=(target.y0+target.y1)*.5f;
    evrm::ModInputBatch batch;batch.count=1;batch.events[0]={evrm::ModInputEventKind::Button,0,true};
    evrm::feed_mod_menu_input(batch,click_x,click_y,placement);frame();batch.events[0].down=false;
    evrm::feed_mod_menu_input(batch,click_x,click_y,placement);frame();frame();
    require(settings.voice_enabled,"queued click follows its new position instead of previous frame position");
    settings.voice_enabled=false;io.ClearEventsQueue();io.ClearInputMouse();io.AddMousePosEvent(0,0);frame();frame();
    batch.count=2;batch.events[0]={evrm::ModInputEventKind::Button,0,true,click_x,click_y,true};batch.events[1]=batch.events[0];batch.events[1].down=false;
    evrm::feed_mod_menu_input(batch,0,0,placement);frame();evrm::ModInputBatch empty;for(unsigned n=0;n<5;++n){evrm::feed_mod_menu_input(empty,0,0,placement);frame();}
    require(settings.voice_enabled,"queued click retains its event position after the physical pointer moves away");
    io.ClearEventsQueue();io.ClearInputMouse();expanded=false;frame();frame();
    const float original_x=layout.voice.x0,original_y=layout.voice.y0;
    const float title_x=original_x+90,title_y=original_y-55;
    io.AddMousePosEvent(title_x,title_y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(title_x+100,title_y+60);frame();frame();
    require(std::abs(layout.voice.x0-original_x-100)<1&&std::abs(layout.voice.y0-original_y-60)<1,"title drag moves the panel");
    io.AddMouseButtonEvent(0,false);frame();const auto released=layout.voice;
    io.AddMousePosEvent(title_x+180,title_y+120);frame();frame();
    require(std::abs(layout.voice.x0-released.x0)<1&&std::abs(layout.voice.y0-released.y0)<1,"released title stays in place");
    click(layout.voice);require(!settings.voice_enabled,"moved controls still toggle");
    const auto move=[&](float x,float y){io.AddMousePosEvent((layout.title.x0+layout.title.x1)*.5f,(layout.title.y0+layout.title.y1)*.5f);frame();io.AddMouseButtonEvent(0,true);frame();
        io.AddMousePosEvent(x,y);frame();frame();};
    move(-100,-100);require(placement.x==8&&placement.y==8,"drag clamps top and left");
    io.ClearEventsQueue();io.ClearInputMouse();frame();require(!placement.dragging,"input reset cancels drag");
    move(2000,1500);require(placement.x+layout.width<=io.DisplaySize.x-8&&placement.y+layout.height<=io.DisplaySize.y-8,"drag clamps right and bottom");
    io.AddMouseButtonEvent(0,false);frame();expanded=true;frame();require(placement.y+layout.height<=io.DisplaySize.y-8,"expansion remains on screen");
    io.DisplaySize={400,480};frame();frame();require(placement.x>=8&&placement.y>=8&&placement.x+layout.width<=400&&placement.y+layout.height<=480,"resized viewport retains reachable controls");
    io.DisplaySize={1000,800};expanded=false;settings.voice_enabled=false;placement.x=200;placement.y=200;frame();frame();
    move(layout.title.x0+100,layout.title.y0+70);require(placement.dragging,"reset fixture starts during title drag");
    const float fixed_x=placement.x,fixed_y=placement.y;
    const float control_x=(layout.voice.x0+layout.voice.x1)*.5f,control_y=(layout.voice.y0+layout.voice.y1)*.5f;
    batch.count=2;batch.events[0]={evrm::ModInputEventKind::Reset};batch.events[1]={evrm::ModInputEventKind::Button,0,true,control_x,control_y,true};
    evrm::feed_mod_menu_input(batch,control_x,control_y,placement);frame();
    require(!placement.dragging&&placement.x==fixed_x&&placement.y==fixed_y,"reset followed by fresh control press must not resume an old title drag");
    batch.count=1;batch.events[0]={evrm::ModInputEventKind::Button,0,false,control_x,control_y,true};
    evrm::feed_mod_menu_input(batch,control_x,control_y,placement);frame();frame();
    require(settings.voice_enabled,"fresh control click after drag reset was swallowed by an old active widget");
    ImGui::DestroyContext();
    // A replacement renderer has a fresh GUI context but retains panel position.
    // A click arriving on its first frame must not revive the old renderer's drag.
    placement.dragging=true;placement.grab_x=placement.grab_y=20;
    ImGui::CreateContext();evrm::configure_mod_menu(1,false);auto& fresh=ImGui::GetIO();fresh.DisplaySize={1000,800};fresh.DeltaTime=1.f/60;
    fresh.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);fresh.AddMousePosEvent(control_x,control_y);fresh.AddMouseButtonEvent(0,true);
    ImGui::NewFrame();evrm::draw_mod_menu(settings,status,expanded,close,placement,&layout);ImGui::Render();
    require(!placement.dragging&&placement.x==fixed_x&&placement.y==fixed_y,"replacement renderer click must not revive the previous renderer's drag");
    ImGui::DestroyContext();
    std::cout<<"PASS dark menu interaction, grayscale controls, saved values, bounds, status and no ini/log output\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
