#pragma once
#include "avatar_service.hpp"
namespace evrm {
// Actions retain their captured player even if the menu selection changes.
inline void apply_avatar_action_to(AvatarService& service,const AvatarMenuAction& action,std::uintptr_t window)noexcept {
    switch(action.kind){
    case AvatarActionKind::SelectPlayer:service.select_player(action.subject);break;
    case AvatarActionKind::SelectModel:service.request_model(action.path,action.subject);break;
    case AvatarActionKind::Browse:service.request_browse(window,action.subject);break;
    case AvatarActionKind::SetWholePreset:if(action.preset)service.set_whole_preset(action.fingerprint,*action.preset);break;
    case AvatarActionKind::SetMaterialPreset:service.set_material_preset(action.fingerprint,action.material,action.preset);break;
    case AvatarActionKind::ResetMaterials:service.reset_materials(action.fingerprint);break;
    case AvatarActionKind::CancelLoad:service.cancel_request(action.subject);break;
    case AvatarActionKind::RefreshCatalogue:service.refresh_catalogue();break;
    case AvatarActionKind::SetReplacementEnabled:service.set_replacement_enabled(action.replacement_enabled,action.subject);break;
    default:break;
    }
}
}
