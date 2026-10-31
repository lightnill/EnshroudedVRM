#include "mod_menu.hpp"
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace evrm {
namespace {
#include "mod_menu_font.generated.inl"
ImVec4 rgb(unsigned color,bool linear){
    const auto component=[&](unsigned value){const float c=value/255.f;return linear?(c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f)):c;};
    return {component((color>>16)&255),component((color>>8)&255),component(color&255),1};
}
ModMenuRect last_rect(){const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();return {a.x,a.y,b.x,b.y};}
ImU32 color(ImGuiCol index){return ImGui::GetColorU32(index);}
void row(const char* label,const char* value,float width){
    const auto left=ImGui::GetCursorPosX();ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),"%s",label);
    ImGui::SameLine();ImGui::SetCursorPosX(left+std::max(0.f,width-ImGui::CalcTextSize(value).x));ImGui::TextUnformatted(value);
}
bool slider(const char* label,const char* id,unsigned& value,unsigned low,unsigned high,const char* suffix,float width,ModMenuRect* rectangle){
    char text[32]{};std::snprintf(text,sizeof text,"%u%s",value,suffix);row(label,text,width);
    ImGui::SetNextItemWidth(width);int next=int(value);const bool changed=ImGui::SliderInt(id,&next,int(low),int(high),"",ImGuiSliderFlags_NoInput);
    if(rectangle)*rectangle=last_rect();
    if(changed)value=unsigned(next);
    return changed;
}
ImGuiKey key(unsigned code){
    switch(code){case 9:return ImGuiKey_Tab;case 13:return ImGuiKey_Enter;case 27:return ImGuiKey_Escape;case 32:return ImGuiKey_Space;
    case 33:return ImGuiKey_PageUp;case 34:return ImGuiKey_PageDown;case 35:return ImGuiKey_End;case 36:return ImGuiKey_Home;
    case 37:return ImGuiKey_LeftArrow;case 38:return ImGuiKey_UpArrow;case 39:return ImGuiKey_RightArrow;case 40:return ImGuiKey_DownArrow;
    case 45:return ImGuiKey_Insert;case 46:return ImGuiKey_Delete;
    case 16:case 160:case 161:return ImGuiKey_LeftShift;case 17:case 162:case 163:return ImGuiKey_LeftCtrl;
    case 18:case 164:case 165:return ImGuiKey_LeftAlt;default:return ImGuiKey_None;}
}
}
void configure_mod_menu(float dpi,bool srgb){
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.ConfigFlags=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NoMouseCursorChange;
    io.BackendPlatformName="evrm-win32";dpi=std::clamp(dpi,1.f,3.f);ImFontConfig font;font.SizePixels=18*dpi;
    font.FontDataOwnedByAtlas=false;io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(mod_menu_font),sizeof mod_menu_font,font.SizePixels,&font);
    io.FontGlobalScale=1/dpi;
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();
    style.WindowPadding={24,24};style.WindowRounding=14;style.WindowBorderSize=1;style.FrameRounding=6;
    style.FramePadding={10,5};style.ItemSpacing={10,10};style.GrabMinSize=14;style.GrabRounding=5;style.DisabledAlpha=.5f;
    for(int n=0;n<ImGuiCol_COUNT;++n)style.Colors[n]=rgb(0x62676b,srgb);
    style.Colors[ImGuiCol_Text]=rgb(0xe9ebed,srgb);style.Colors[ImGuiCol_TextDisabled]=rgb(0x9b9fa4,srgb);
    style.Colors[ImGuiCol_WindowBg]=rgb(0x172126,srgb);style.Colors[ImGuiCol_ChildBg]=style.Colors[ImGuiCol_WindowBg];
    style.Colors[ImGuiCol_PopupBg]=style.Colors[ImGuiCol_WindowBg];
    style.Colors[ImGuiCol_Border]=rgb(0x3b444a,srgb);style.Colors[ImGuiCol_Separator]=style.Colors[ImGuiCol_Border];
    style.Colors[ImGuiCol_FrameBg]=rgb(0x30393f,srgb);style.Colors[ImGuiCol_FrameBgHovered]=rgb(0x41494f,srgb);style.Colors[ImGuiCol_FrameBgActive]=rgb(0x52595e,srgb);
    style.Colors[ImGuiCol_Button]=rgb(0x263037,srgb);style.Colors[ImGuiCol_ButtonHovered]=rgb(0x354047,srgb);style.Colors[ImGuiCol_ButtonActive]=rgb(0x444e55,srgb);
    style.Colors[ImGuiCol_CheckMark]=rgb(0xf0f0f0,srgb);style.Colors[ImGuiCol_SliderGrab]=rgb(0xb7b9bc,srgb);
    style.Colors[ImGuiCol_SliderGrabActive]=rgb(0xffffff,srgb);style.Colors[ImGuiCol_NavCursor]=rgb(0xffffff,srgb);
    style.Colors[ImGuiCol_ScrollbarBg]=style.Colors[ImGuiCol_WindowBg];style.Colors[ImGuiCol_ScrollbarGrab]=rgb(0x4f575c,srgb);
    style.Colors[ImGuiCol_ScrollbarGrabHovered]=rgb(0x879096,srgb);style.Colors[ImGuiCol_ScrollbarGrabActive]=rgb(0xb7b9bc,srgb);
}
void feed_mod_menu_input(const ModInputBatch& batch,float x,float y,ModMenuPlacement& placement,float dpi){
    auto& io=ImGui::GetIO();
    bool positioned=false;
    for(std::size_t n=0;n<batch.count&&n<batch.events.size();++n)positioned|=batch.events[n].kind==ModInputEventKind::Button&&batch.events[n].positioned;
    if(!positioned)io.AddMousePosEvent(x,y);
    for(std::size_t n=0;n<batch.count&&n<batch.events.size();++n){const auto& e=batch.events[n];
        if(e.kind==ModInputEventKind::Reset){placement.dragging=false;ImGui::ClearActiveID();
            for(int code=ImGuiKey_NamedKey_BEGIN;code<ImGuiKey_NamedKey_END;++code)ImGui::SetKeyOwner(ImGuiKey(code),ImGuiKeyOwner_NoOwner);
            io.ClearEventsQueue();io.ClearInputKeys();io.ClearInputMouse();if(!positioned)io.AddMousePosEvent(x,y);}
        else if(e.kind==ModInputEventKind::Button&&e.code<5){if(e.positioned)io.AddMousePosEvent(e.x/dpi,e.y/dpi);io.AddMouseButtonEvent(e.code,e.down);}
        else if(e.kind==ModInputEventKind::Wheel)io.AddMouseWheelEvent(e.x,e.y);
        else if(e.kind==ModInputEventKind::Key){const auto mapped=key(e.code);if(mapped!=ImGuiKey_None)io.AddKeyEvent(mapped,e.down);
            if(e.code==16||e.code==160||e.code==161)io.AddKeyEvent(ImGuiMod_Shift,e.down);
            if(e.code==17||e.code==162||e.code==163)io.AddKeyEvent(ImGuiMod_Ctrl,e.down);
            if(e.code==18||e.code==164||e.code==165)io.AddKeyEvent(ImGuiMod_Alt,e.down);}
    }
    if(positioned)io.AddMousePosEvent(x,y);
}
const char* mod_voice_status(const ModSettings& settings,const ModMenuStatus& status)noexcept {
    if(!settings.voice_enabled)return "Off";
    if(status.starting)return "Starting";
    if(settings.voice_source==VoiceInputSource::Microphone){
        if(status.microphone==MicrophoneState::AccessDenied)return "Microphone access denied";
        if(status.microphone==MicrophoneState::NoDevice)return "Microphone not found";
        if(status.microphone==MicrophoneState::Unavailable)return "Microphone unavailable";
    }
    switch(status.voice){case VoiceActivity::Talking:return "Talking";case VoiceActivity::Quiet:return "Listening";
        case VoiceActivity::UnsupportedSource:return "Voice to viseme unavailable";
        default:return settings.voice_source==VoiceInputSource::Microphone?"Waiting for microphone":"Waiting for game voice";}
}
bool draw_mod_menu(ModSettings& settings,const ModMenuStatus& status,bool& expanded,bool& close,ModMenuPlacement& placement,ModMenuLayout* layout,AvatarMenuAction* action){
    if(action)*action={};
    const auto& io=ImGui::GetIO();const float width=std::max(96.f,std::min(504.f,io.DisplaySize.x-24));
    if(ImGui::IsMouseClicked(0))placement.dragging=false;
    const float wanted_height=(expanded?(settings.voice_source==VoiceInputSource::Microphone?812.f:716.f):372.f)+
        (status.avatar?(placement.appearance_expanded?510.f:200.f):0.f);
    const float height=std::max(80.f,std::min(wanted_height,io.DisplaySize.y-24));
    if(!placement.initialized){placement.x=(io.DisplaySize.x-width)*.5f;placement.y=(io.DisplaySize.y-height)*.5f;placement.initialized=true;}
    if(placement.dragging){if(io.MouseDown[0]&&ImGui::IsMousePosValid(&io.MousePos)){placement.x=io.MousePos.x-placement.grab_x;placement.y=io.MousePos.y-placement.grab_y;}
        else placement.dragging=false;}
    placement.x=std::clamp(placement.x,8.f,std::max(8.f,io.DisplaySize.x-width-8));
    placement.y=std::clamp(placement.y,8.f,std::max(8.f,io.DisplaySize.y-height-8));
    ImGui::SetNextWindowPos({placement.x,placement.y},ImGuiCond_Always);
    ImGui::SetNextWindowSize({width,height},ImGuiCond_Always);
    const auto flags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("EnshroudedVRM##mod-menu",nullptr,flags);bool changed=false;const float inner=std::max(48.f,ImGui::GetContentRegionAvail().x);
    if(layout){*layout={};layout->width=width;layout->height=height;}
    const auto origin=ImGui::GetCursorPos();
    ImGui::InvisibleButton("move-menu",{std::max(1.f,inner-42),34});
    if(ImGui::IsItemActivated()){placement.dragging=true;placement.grab_x=io.MousePos.x-placement.x;placement.grab_y=io.MousePos.y-placement.y;}
    if(layout)layout->title=last_rect();
    ImGui::SetCursorPos(origin);ImGui::SetWindowFontScale(1.25f);ImGui::TextUnformatted("EnshroudedVRM");ImGui::SetWindowFontScale(1);
    ImGui::SetCursorPos({origin.x+inner-30,origin.y-2});if(ImGui::InvisibleButton("close-menu",{30,30},ImGuiButtonFlags_EnableNav))close=true;
    if(layout)layout->close=last_rect();
    auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
    const auto cross=color(ImGui::IsItemHovered()?ImGuiCol_Text:ImGuiCol_TextDisabled);
    draw->AddLine({a.x+10,a.y+10},{b.x-10,b.y-10},cross,1.6f);draw->AddLine({a.x+10,b.y-10},{b.x-10,a.y+10},cross,1.6f);
    ImGui::SetCursorPos({origin.x,origin.y+46});ImGui::Separator();ImGui::Dummy({0,4});
    if(ImGui::InvisibleButton("voice-to-viseme",{inner,72},ImGuiButtonFlags_EnableNav)){settings.voice_enabled=!settings.voice_enabled;changed=true;}
    if(layout)layout->voice=last_rect();
    a=ImGui::GetItemRectMin();b=ImGui::GetItemRectMax();
    draw->AddRectFilled(a,b,color(ImGui::IsItemHovered()?ImGuiCol_ButtonHovered:ImGuiCol_Button),9);
    if(ImGui::IsItemFocused())draw->AddRect(a,b,color(ImGuiCol_NavCursor),9,0,1);
    draw->PushClipRect({a.x+12,a.y},{b.x-84,b.y},true);draw->AddText({a.x+16,a.y+13},color(ImGuiCol_Text),"Voice to viseme");
    draw->AddText({a.x+16,a.y+41},color(ImGuiCol_TextDisabled),settings.voice_source==VoiceInputSource::Microphone?"Follow local microphone":"Follow game voice");draw->PopClipRect();
    const ImVec2 toggle={b.x-64,a.y+23};draw->AddRectFilled(toggle,{toggle.x+48,toggle.y+26},color(settings.voice_enabled?ImGuiCol_SliderGrab:ImGuiCol_FrameBgHovered),13);
    draw->AddCircleFilled({toggle.x+(settings.voice_enabled?35.f:13.f),toggle.y+13},9,color(settings.voice_enabled?ImGuiCol_WindowBg:ImGuiCol_TextDisabled));
    ImGui::Dummy({0,2});row("Source",settings.voice_source==VoiceInputSource::Microphone?"Local microphone":"Game voice chat",inner);row("Voice",mod_voice_status(settings,status),inner);
    const char* detail=settings.voice_source==VoiceInputSource::Microphone?"Works offline using the selected microphone.":"Uses your game's outgoing voice.";
    if(settings.voice_source==VoiceInputSource::Game&&settings.voice_enabled&&status.voice==VoiceActivity::Waiting)detail="Voice chat needs an online-capable session.";
    if(status.mouth==ModMouthSupport::NoAvatar)detail="Load an avatar to animate its mouth.";
    else if(status.mouth==ModMouthSupport::NoShapes)detail="This avatar has no supported mouth shapes.";
    else if(status.mouth==ModMouthSupport::Partial)detail="Some mouth shapes are missing.";
    else if(status.mouth==ModMouthSupport::Unavailable)detail="Avatar mouth animation is unavailable.";
    ImGui::SetWindowFontScale(.8f);ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),"%s",detail);ImGui::SetWindowFontScale(1);
    ImGui::Dummy({0,2});
    if(ImGui::Button(expanded?"-  Adjust mouth movement":"+  Adjust mouth movement",{inner,36}))expanded=!expanded;
    if(layout)layout->expand=last_rect();
    if(expanded){
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),"Audio source");
        if(ImGui::RadioButton("Game voice",settings.voice_source==VoiceInputSource::Game)){settings.voice_source=VoiceInputSource::Game;changed=true;}
        if(layout)layout->source_game=last_rect();
        if(inner>=380)ImGui::SameLine();
        if(ImGui::RadioButton("Local microphone",settings.voice_source==VoiceInputSource::Microphone)){settings.voice_source=VoiceInputSource::Microphone;changed=true;}
        if(layout)layout->source_microphone=last_rect();
        if(settings.voice_source==VoiceInputSource::Microphone){
            const auto selected_storage=settings.microphone_device;
            const auto& selected=selected_storage?*selected_storage:microphone_device_id(settings);
            const char* label=selected.empty()?"Windows default":"Selected microphone unavailable";
            if(status.microphones)for(const auto& device:status.microphones->devices)if(device.id==selected)label=device.name.c_str();
            ImGui::SetNextItemWidth(inner);const bool opened=ImGui::BeginCombo("##microphone-device",label);
            if(layout)layout->microphone_device=last_rect();
            if(opened){
                if(ImGui::Selectable("Windows default",selected.empty())){settings.microphone_device.reset();changed=true;}
                if(layout)layout->default_device=last_rect();
                bool first=true;
                if(status.microphones)for(const auto& device:status.microphones->devices){
                    ImGui::PushID(device.id.c_str());
                    if(ImGui::Selectable(device.name.c_str(),selected==device.id)){settings.microphone_device=std::make_shared<const std::string>(device.id);changed=true;}
                    if(layout&&first)layout->first_device=last_rect();
                    first=false;ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetWindowFontScale(.75f);ImGui::TextWrapped("Independent of game mute and push-to-talk. Turn Voice to viseme Off to stop listening.");ImGui::SetWindowFontScale(1);
        }
        ImGui::Dummy({0,2});ImGui::BeginDisabled(!settings.voice_enabled);
        changed|=slider("Mouth strength","##mouth-strength",settings.strength,0,100,"%",inner,layout?&layout->strength:nullptr);
        ImGui::Dummy({0,2});changed|=slider("Sensitivity","##sensitivity",settings.sensitivity,0,100,"%",inner,layout?&layout->sensitivity:nullptr);
        ImGui::Dummy({0,2});changed|=slider("Smoothing","##smoothing",settings.smoothing_ms,20,140," ms",inner,layout?&layout->smoothing:nullptr);
        ImGui::EndDisabled();
    }
    if(status.avatar){
        const auto& avatar=*status.avatar;
        const auto emit=[&](AvatarActionKind kind){if(action){action->kind=kind;action->fingerprint=avatar.fingerprint;}};
        ImGui::Dummy({0,3});ImGui::Separator();ImGui::TextUnformatted("Avatar");
        ImGui::SetNextItemWidth(std::max(48.f,inner-100));
        const bool models=ImGui::BeginCombo("##avatar-model",avatar.name.empty()?"Choose a VRM model":avatar.name.c_str());
        if(layout)layout->model=last_rect();
        if(models){
            if(avatar.choices)for(std::size_t n=0;n<avatar.choices->size();++n){const auto& choice=(*avatar.choices)[n];ImGui::PushID(int(n));
                if(ImGui::Selectable(choice.name.c_str(),choice.path==avatar.path)){emit(AvatarActionKind::SelectModel);if(action)action->path=choice.path;}
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",choice.path.c_str());
                if(layout&&n==1)layout->second_model=last_rect();
                ImGui::PopID();}
            ImGui::EndCombo();
        }
        if(inner>=160)ImGui::SameLine();
        if(ImGui::Button("Browse...",{inner>=160?90.f:inner,0})){emit(AvatarActionKind::Browse);close=true;}
        if(layout)layout->browse=last_rect();
        if(ImGui::Button("Refresh list"))emit(AvatarActionKind::RefreshCatalogue);
        if(layout)layout->refresh_models=last_rect();
        const bool busy=avatar.state==AvatarLoadState::Loading||avatar.state==AvatarLoadState::Preparing||avatar.state==AvatarLoadState::Choosing;
        if(busy){ImGui::SameLine();if(ImGui::Button("Cancel"))emit(AvatarActionKind::CancelLoad);if(layout)layout->cancel_model=last_rect();}
        const char* state=avatar.state==AvatarLoadState::Loading?"Loading model...":avatar.state==AvatarLoadState::Preparing?"Preparing model...":avatar.state==AvatarLoadState::Choosing?"Choosing a model...":nullptr;
        ImGui::SetWindowFontScale(.8f);
        if(state)ImGui::TextWrapped("%s",state);
        if(!avatar.error.empty())ImGui::TextWrapped("%s",avatar.error.c_str());
        if(avatar.save_failed)ImGui::TextWrapped("Avatar settings could not be saved.");
        ImGui::SetWindowFontScale(1);
        if(ImGui::Button(placement.appearance_expanded?"-  Appearance presets":"+  Appearance presets",{inner,36}))placement.appearance_expanded=!placement.appearance_expanded;
        if(layout)layout->appearance=last_rect();
        if(placement.appearance_expanded){
            if(!avatar.preferences_loaded)ImGui::TextUnformatted("Loading appearance preferences...");
            const bool valid=!avatar.fingerprint.empty()&&avatar.preferences_loaded;ImGui::BeginDisabled(!valid);
            const auto whole=avatar.profile?avatar.profile->whole:AvatarShaderPreset::GameLighting;
            ImGui::TextUnformatted("Whole model");ImGui::SetNextItemWidth(inner);
            const bool presets=ImGui::BeginCombo("##whole-preset",whole==AvatarShaderPreset::Matte?"Matte":"Game lighting");
            if(layout)layout->whole=last_rect();
            if(presets){
                if(ImGui::Selectable("Game lighting",whole==AvatarShaderPreset::GameLighting)){emit(AvatarActionKind::SetWholePreset);if(action)action->preset=AvatarShaderPreset::GameLighting;}
                if(layout)layout->whole_game=last_rect();
                if(ImGui::Selectable("Matte",whole==AvatarShaderPreset::Matte)){emit(AvatarActionKind::SetWholePreset);if(action)action->preset=AvatarShaderPreset::Matte;}
                if(layout)layout->whole_matte=last_rect();
                ImGui::EndCombo();
            }
            if(placement.material_model!=avatar.fingerprint){placement.material_model=avatar.fingerprint;placement.material_index=0;}
            const auto count=avatar.materials?avatar.materials->size():0;
            if(placement.material_index>=count)placement.material_index=0;
            const auto material_label=[&](std::size_t n){return std::to_string(n+1)+". "+((*avatar.materials)[n].empty()?"Unnamed material":(*avatar.materials)[n]);};
            ImGui::TextUnformatted("Individual material");ImGui::BeginDisabled(!count);ImGui::SetNextItemWidth(inner);
            const auto label=count?material_label(placement.material_index):"No materials";
            const bool materials=ImGui::BeginCombo("##avatar-material",label.c_str());if(layout)layout->material=last_rect();
            if(materials){ImGuiListClipper clipper;clipper.Begin(int(count));while(clipper.Step())for(int n=clipper.DisplayStart;n<clipper.DisplayEnd;++n){ImGui::PushID(n);
                    if(ImGui::Selectable(material_label(n).c_str(),placement.material_index==std::uint32_t(n)))placement.material_index=std::uint32_t(n);
                    if(layout&&n==1)layout->second_material=last_rect();
                    ImGui::PopID();}
                ImGui::EndCombo();}
            std::optional<AvatarShaderPreset> selected;
            if(avatar.profile&&count)for(const auto& override:avatar.profile->overrides)if(override.index==placement.material_index&&override.name==(*avatar.materials)[override.index])selected=override.preset;
            ImGui::SetNextItemWidth(inner);const bool overrides=ImGui::BeginCombo("##material-preset",!selected?"Use model preset":*selected==AvatarShaderPreset::Matte?"Matte":"Game lighting");
            if(layout)layout->material_override=last_rect();
            if(overrides){const auto choose=[&](const char* title,std::optional<AvatarShaderPreset> preset,ModMenuRect* rect){
                    if(ImGui::Selectable(title,selected==preset)){emit(AvatarActionKind::SetMaterialPreset);if(action){action->material=placement.material_index;action->preset=preset;}}if(rect)*rect=last_rect();};
                choose("Use model preset",{},layout?&layout->inherit:nullptr);choose("Game lighting",AvatarShaderPreset::GameLighting,layout?&layout->override_game:nullptr);
                choose("Matte",AvatarShaderPreset::Matte,layout?&layout->override_matte:nullptr);ImGui::EndCombo();}
            ImGui::EndDisabled();
            if(ImGui::Button("Reset material overrides",{inner,0}))emit(AvatarActionKind::ResetMaterials);
            if(layout)layout->reset_materials=last_rect();
            ImGui::SetWindowFontScale(.75f);ImGui::TextWrapped("Game lighting keeps the current look. Matte reduces shine. Individual overrides stay when the whole model preset changes. Changes save automatically for each model.");ImGui::SetWindowFontScale(1);ImGui::EndDisabled();
        }
    }
    ImGui::Dummy({0,1});ImGui::SetWindowFontScale(.75f);
    if(status.save_failed)ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),"Settings could not be saved.");
    else ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),"Escape to close");
    if(layout)layout->scroll_max_y=ImGui::GetScrollMaxY();
    ImGui::SetWindowFontScale(1);ImGui::End();if(close)placement.dragging=false;return changed;
}
}
