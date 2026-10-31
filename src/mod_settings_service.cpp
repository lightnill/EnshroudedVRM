#include "mod_settings_service.hpp"
#include "voice_inputs.hpp"
#include "runtime_log.hpp"
#include <atomic>
#include <memory>
namespace evrm {
namespace {
ModSettingsMailbox mailbox;
std::mutex changes;
std::atomic<bool> loaded{false},save_failed{false};
struct Service {std::filesystem::path path;HANDLE event=nullptr,thread=nullptr;};
std::atomic<Service*> service{nullptr};
void apply_voice()noexcept{
    request_voice_inputs(mailbox.snapshot());
}
DWORD WINAPI run(void* raw)noexcept{
    auto& state=*static_cast<Service*>(raw);SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    try{
        const auto disk=read_mod_settings(state.path);bool edited=false;
        {std::lock_guard<std::mutex> guard(changes);
            edited=bool(mailbox.ticket());
            if(disk.status==ModSettingsStatus::Loaded)mailbox.accept_loaded(disk.settings);
            apply_voice();loaded.store(true,std::memory_order_release);}
        if(disk.status!=ModSettingsStatus::Loaded&&disk.status!=ModSettingsStatus::Missing)log_event("mod-settings.defaults reason=unavailable-or-invalid");
        if(edited)SetEvent(state.event);
        while(WaitForSingleObject(state.event,INFINITE)==WAIT_OBJECT_0){
            auto ticket=mailbox.ticket();auto deadline=GetTickCount64()+300;
            for(;;){const auto now=GetTickCount64();if(now>=deadline)break;
                const auto result=WaitForSingleObject(state.event,DWORD(deadline-now));if(result==WAIT_FAILED)return 0;
                const auto current=mailbox.ticket();if(current!=ticket){ticket=current;deadline=GetTickCount64()+300;}}
            const auto value=ticket?ticket->settings:ModSettings{};
            const auto saved=write_mod_settings(state.path,value);save_failed.store(!saved,std::memory_order_release);
            log_event(saved?"mod-settings.saved":"mod-settings.save-failed");
        }
    }catch(...){save_failed.store(true,std::memory_order_release);loaded.store(true,std::memory_order_release);log_event("mod-settings.background-failed");}
    return 0;
}
}
void initialize_mod_settings_service(const std::filesystem::path& directory)noexcept{
    static std::once_flag once;try{std::call_once(once,[&]{
        HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&run),&pinned))return;
        auto state=std::make_unique<Service>();auto root=directory;
        if(root.empty()){wchar_t capture[32768]{};const auto count=GetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_DIR",capture,32768);
            if(count&&count<32768)root=std::filesystem::path(capture).parent_path();}
        if(root.empty()){loaded=true;save_failed=true;return;}
        state->path=root/L"mod-settings.json";state->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!state->event){loaded=true;save_failed=true;return;}
        // Publish under the same short lock as live changes so a request cannot
        // miss its wake between thread creation and service publication.
        std::lock_guard<std::mutex> guard(changes);
        state->thread=CreateThread(nullptr,0,&run,state.get(),0,nullptr);
        if(!state->thread){CloseHandle(state->event);loaded=true;save_failed=true;return;}
        service.store(state.release(),std::memory_order_release);
    });}catch(...){loaded=true;save_failed=true;}
}
ModSettings mod_settings_snapshot()noexcept{return mailbox.snapshot();}
bool request_mod_settings(const ModSettings& value)noexcept{
    try{std::lock_guard<std::mutex> guard(changes);if(!mailbox.update(value))return false;apply_voice();
        if(auto* state=service.load(std::memory_order_acquire))SetEvent(state->event);
        return true;
    }catch(...){return false;}
}
bool mod_settings_loaded()noexcept{return loaded.load(std::memory_order_acquire);}
bool mod_settings_save_failed()noexcept{return save_failed.load(std::memory_order_acquire);}
}
