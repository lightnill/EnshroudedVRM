#pragma once
#include "mod_settings.hpp"
#include "voice_analyser.hpp"
#include <mutex>
namespace evrm {
struct VoiceInputCallbacks {
    void* context=nullptr;
    void (*game_request)(void*,bool,VoiceAnalysisTuning)noexcept=nullptr;
    bool (*microphone_request)(void*,bool,VoiceAnalysisTuning,MicrophoneDeviceId)noexcept=nullptr;
    MouthSnapshot (*game_snapshot)(void*)noexcept=nullptr;
    MouthSnapshot (*microphone_snapshot)(void*)noexcept=nullptr;
};
class VoiceInputRouter {
public:
    struct Selection {ModSettings settings;bool usable=true;};
    explicit VoiceInputRouter(VoiceInputCallbacks callbacks)noexcept:callbacks_(callbacks){}
    bool request(const ModSettings& settings)noexcept {
        if(!valid_mod_settings(settings))return false;
        try{std::lock_guard<std::mutex> lock(changes_);auto pending=std::make_shared<Selection>(Selection{settings,true});
            const VoiceAnalysisTuning tuning{settings.sensitivity/100.f,settings.smoothing_ms};
            if(settings.voice_source==VoiceInputSource::Game){
                if(callbacks_.microphone_request)callbacks_.microphone_request(callbacks_.context,false,tuning,settings.microphone_device);
                if(callbacks_.game_request)callbacks_.game_request(callbacks_.context,settings.voice_enabled,tuning);
            }else{
                if(callbacks_.game_request)callbacks_.game_request(callbacks_.context,false,tuning);
                pending->usable=callbacks_.microphone_request&&callbacks_.microphone_request(callbacks_.context,settings.voice_enabled,tuning,settings.microphone_device);
            }
            std::shared_ptr<const Selection> next=std::move(pending);std::atomic_store_explicit(&selection_,std::move(next),std::memory_order_release);return true;
        }catch(...){return false;}
    }
    std::shared_ptr<const Selection> selection()const noexcept{return std::atomic_load_explicit(&selection_,std::memory_order_acquire);}
    MouthSnapshot snapshot()const noexcept {
        MouthSnapshot out;const auto selected=selection();if(!selected||!selected->settings.voice_enabled)return out;
        if(!selected->usable){out.activity=VoiceActivity::UnsupportedSource;return out;}
        const auto callback=selected->settings.voice_source==VoiceInputSource::Game?callbacks_.game_snapshot:callbacks_.microphone_snapshot;
        if(callback)out=callback(callbacks_.context);else out.activity=VoiceActivity::UnsupportedSource;
        if(selection()!=selected)return {};
        return out;
    }
private:
    VoiceInputCallbacks callbacks_;
    std::shared_ptr<const Selection> selection_;
    std::mutex changes_;
};
void initialize_voice_inputs()noexcept;
void request_voice_inputs(const ModSettings&)noexcept;
void service_voice_inputs()noexcept;
void poll_voice_inputs(std::uint64_t now_ms)noexcept;
MouthSnapshot selected_voice_mouth_snapshot()noexcept;
bool selected_voice_starting()noexcept;
MicrophoneState local_microphone_state()noexcept;
std::shared_ptr<const MicrophoneDeviceList> local_microphone_devices()noexcept;
}
