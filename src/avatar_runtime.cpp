#include "avatar_runtime.hpp"
#include "mod_input_win32.hpp"
#include "standalone_startup.hpp"
#include <atomic>
#include <mutex>
namespace evrm {
namespace {std::atomic<AvatarService*> service{nullptr};
bool released(std::uintptr_t window,void*)noexcept{return !mod_input_open(reinterpret_cast<HWND>(window));}
}
void initialize_avatar_runtime(std::shared_ptr<const PreparedAvatar> initial)noexcept{
    const auto& startup=runtime_startup_context();if(!startup.ready())return;
    static std::once_flag once;try{std::call_once(once,[&]{
        HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&initialize_avatar_runtime),&pinned))return;
        AvatarServiceOptions options;options.directory=startup.state_directory;
        options.catalogue_directory=startup.avatars_directory;options.save_available=startup.state_writable;
        options.initial=std::move(initial);options.input_released=&released;
        auto created=std::make_unique<AvatarService>(std::move(options));if(created->ready())service.store(created.release(),std::memory_order_release);
    });}catch(...){}
}
AvatarService* avatar_runtime_service()noexcept{return service.load(std::memory_order_acquire);}
std::shared_ptr<const AvatarMenuSnapshot> avatar_runtime_snapshot()noexcept{if(auto* value=avatar_runtime_service())return value->snapshot();return {};}
void apply_avatar_action(const AvatarMenuAction& action,std::uintptr_t window)noexcept{
    auto* value=avatar_runtime_service();if(!value)return;
    switch(action.kind){
    case AvatarActionKind::SelectModel:value->request_model(action.path);break;
    case AvatarActionKind::Browse:value->request_browse(window);break;
    case AvatarActionKind::SetWholePreset:if(action.preset)value->set_whole_preset(action.fingerprint,*action.preset);break;
    case AvatarActionKind::SetMaterialPreset:value->set_material_preset(action.fingerprint,action.material,action.preset);break;
    case AvatarActionKind::ResetMaterials:value->reset_materials(action.fingerprint);break;
    case AvatarActionKind::CancelLoad:value->cancel_request();break;
    case AvatarActionKind::RefreshCatalogue:value->refresh_catalogue();break;
    case AvatarActionKind::SetReplacementEnabled:value->set_replacement_enabled(action.replacement_enabled);break;
    default:break;
    }
}
}
