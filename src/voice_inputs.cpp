#include "voice_inputs.hpp"
#include "local_voice_observer.hpp"
#include "microphone_source.hpp"
#include "runtime_log.hpp"
#include <windows.h>
namespace evrm {
namespace {
std::atomic<MicrophoneSource*> microphone{nullptr};
VoiceInputRouter& router(){
    static VoiceInputRouter value({nullptr,
        [](void*,bool enabled,VoiceAnalysisTuning tuning)noexcept{request_local_voice_feature(enabled,tuning);},
        [](void*,bool enabled,VoiceAnalysisTuning tuning,MicrophoneDeviceId device)noexcept{auto* input=microphone.load(std::memory_order_acquire);return input&&input->request(enabled,tuning,std::move(device));},
        [](void*)noexcept{return local_voice_mouth_snapshot();},
        [](void*)noexcept{auto* input=microphone.load(std::memory_order_acquire);MouthSnapshot out;out.activity=VoiceActivity::UnsupportedSource;return input?input->snapshot():out;}});
    return value;
}
}
void initialize_voice_inputs()noexcept{
    initialize_local_voice_observer();static std::once_flag once;
    try{std::call_once(once,[]{
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&initialize_voice_inputs),&pinned))return;
        auto input=std::make_unique<MicrophoneSource>();
        if(input->ready())microphone.store(input.release(),std::memory_order_release);
        if(const auto current=router().selection())router().request(current->settings);
    });}catch(...){log_event("voice-input.microphone-service-unavailable");}
}
void request_voice_inputs(const ModSettings& settings)noexcept{
    const auto previous=router().selection();if(!router().request(settings))return;
    if(!previous||previous->settings.voice_source!=settings.voice_source||previous->settings.voice_enabled!=settings.voice_enabled||
        microphone_device_id(previous->settings)!=microphone_device_id(settings))log_event_lazy([&]{return "voice-input.source mode="+
            std::string(settings.voice_source==VoiceInputSource::Game?"game":"microphone")+" enabled="+std::to_string(settings.voice_enabled)+
            " microphone-selection="+std::string(microphone_device_id(settings).empty()?"windows-default":"specific");});
}
void service_voice_inputs()noexcept{service_local_voice_feature_requests();}
void poll_voice_inputs(std::uint64_t now)noexcept{poll_local_voice_observer(now);if(auto* input=microphone.load(std::memory_order_acquire))input->poll(now);}
MouthSnapshot selected_voice_mouth_snapshot()noexcept{return router().snapshot();}
bool selected_voice_starting()noexcept{
    const auto current=router().selection();if(!current||!current->settings.voice_enabled||!current->usable)return false;
    if(current->settings.voice_source==VoiceInputSource::Game)return local_voice_feature_starting();
    return local_microphone_state()==MicrophoneState::Starting;
}
MicrophoneState local_microphone_state()noexcept{
    const auto current=router().selection();if(current&&current->settings.voice_source==VoiceInputSource::Microphone&&!current->usable)return MicrophoneState::Unavailable;
    if(auto* input=microphone.load(std::memory_order_acquire))return input->state();
    return MicrophoneState::Unavailable;
}
std::shared_ptr<const MicrophoneDeviceList> local_microphone_devices()noexcept{
    if(auto* input=microphone.load(std::memory_order_acquire))return input->devices();
    return {};
}
}
