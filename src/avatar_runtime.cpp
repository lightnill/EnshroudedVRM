#include "avatar_runtime.hpp"
#include "avatar_actions.hpp"
#include "native_owner_process.hpp"
#include "mod_input_win32.hpp"
#include "standalone_startup.hpp"
#include <atomic>
#include <mutex>
namespace evrm {
namespace {std::atomic<AvatarService*> service{nullptr};
bool released(std::uintptr_t window,void*)noexcept{return !mod_input_open(reinterpret_cast<HWND>(window));}
std::shared_ptr<const NativePlayerRoster> players(void*)noexcept{
    try{
        // First use is on the avatar worker, never the graphics thread.
        static NativeOwnerProcess source;
        auto result=std::make_shared<NativePlayerRoster>();std::string reason;
        NativeOwnerLimits limits;limits.max_milliseconds=40;
        if(source.players(*result,reason,limits))return result;
    }catch(...){}
    return {};
}
}
void initialize_avatar_runtime(std::shared_ptr<const PreparedAvatar> initial)noexcept{
    const auto& startup=runtime_startup_context();if(!startup.ready())return;
    static std::once_flag once;try{std::call_once(once,[&]{
        HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&initialize_avatar_runtime),&pinned))return;
        AvatarServiceOptions options;options.directory=startup.state_directory;
        options.catalogue_directory=startup.avatars_directory;options.save_available=startup.state_writable;
        options.initial=std::move(initial);options.input_released=&released;
        options.players_reader=&players;
        auto created=std::make_unique<AvatarService>(std::move(options));if(created->ready())service.store(created.release(),std::memory_order_release);
    });}catch(...){}
}
AvatarService* avatar_runtime_service()noexcept{return service.load(std::memory_order_acquire);}
std::shared_ptr<const AvatarMenuSnapshot> avatar_runtime_snapshot()noexcept{if(auto* value=avatar_runtime_service())return value->snapshot();return {};}
std::shared_ptr<const AvatarMenuSnapshot> avatar_menu_snapshot()noexcept{if(auto* value=avatar_runtime_service())return value->menu_snapshot();return {};}
std::shared_ptr<const std::vector<AvatarSubjectSelection>> avatar_runtime_subjects()noexcept{if(auto* value=avatar_runtime_service())return value->subjects();return {};}
void apply_avatar_action(const AvatarMenuAction& action,std::uintptr_t window)noexcept{
    auto* value=avatar_runtime_service();if(!value)return;
    apply_avatar_action_to(*value,action,window);
}
}
