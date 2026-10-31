#pragma once
#include "mod_settings.hpp"
#include "mod_menu_status.hpp"
#include "mod_input_state.hpp"
namespace evrm {
struct ModMenuRect {float x0=0,y0=0,x1=0,y1=0;};
struct ModMenuPlacement {bool initialized=false,dragging=false;float x=0,y=0,grab_x=0,grab_y=0;
    bool appearance_expanded=false;std::uint32_t material_index=0;std::string material_model;};
struct ModMenuLayout {float width=0,height=0,scroll_max_y=0;ModMenuRect voice,expand,strength,sensitivity,smoothing,close,title;
    ModMenuRect source_game,source_microphone,microphone_device,default_device,first_device;
    ModMenuRect model,second_model,browse,refresh_models,cancel_model,appearance,whole,whole_game,whole_matte;
    ModMenuRect material,second_material,material_override,inherit,override_game,override_matte,reset_materials;};
void configure_mod_menu(float dpi,bool srgb);
void feed_mod_menu_input(const ModInputBatch&,float x,float y,ModMenuPlacement&,float dpi=1);
const char* mod_voice_status(const ModSettings&,const ModMenuStatus&)noexcept;
bool draw_mod_menu(ModSettings&,const ModMenuStatus&,bool& expanded,bool& close,ModMenuPlacement&,ModMenuLayout* layout=nullptr,AvatarMenuAction* action=nullptr);
}
