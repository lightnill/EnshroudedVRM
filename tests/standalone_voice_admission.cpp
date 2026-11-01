#include "../src/standalone_startup.hpp"
#include "../src/voice_inputs.hpp"
#include "../src/microphone_source.hpp"
#include <iostream>
namespace evrm {
RuntimeStartupContext fixture;const RuntimeStartupContext& runtime_startup_context()noexcept{return fixture;}
unsigned observers=0,microphones=0,services=0,polls=0;bool game_on=false,microphone_on=false;
void initialize_local_voice_observer()noexcept{++observers;}
void request_local_voice_feature(bool enabled,VoiceAnalysisTuning)noexcept{game_on=enabled;}
void service_local_voice_feature_requests()noexcept{++services;}
void poll_local_voice_observer(std::uint64_t)noexcept{++polls;}
bool local_voice_feature_starting()noexcept{return false;}
MouthSnapshot local_voice_mouth_snapshot()noexcept{MouthSnapshot result;result.activity=game_on?VoiceActivity::Talking:VoiceActivity::Off;return result;}
struct MicrophoneSource::Impl{};
MicrophoneSource::MicrophoneSource(MicrophoneBackendFactory,void*,VoiceTimeSource,unsigned)noexcept{++microphones;}
MicrophoneSource::~MicrophoneSource()=default;
bool MicrophoneSource::ready()const noexcept{return true;}
bool MicrophoneSource::request(bool enabled,VoiceAnalysisTuning,MicrophoneDeviceId)noexcept{microphone_on=enabled;return true;}
MicrophoneState MicrophoneSource::state()const noexcept{return microphone_on?MicrophoneState::Ready:MicrophoneState::Off;}
std::shared_ptr<const MicrophoneDeviceList> MicrophoneSource::devices()const noexcept{return {};}
MouthSnapshot MicrophoneSource::snapshot()const noexcept{MouthSnapshot result;result.activity=microphone_on?VoiceActivity::Talking:VoiceActivity::Off;return result;}
void MicrophoneSource::poll(std::uint64_t)noexcept{}
}
int main() {
    using namespace evrm;ModSettings settings;settings.voice_enabled=true;
    initialize_voice_inputs();request_voice_inputs(settings);service_voice_inputs();poll_voice_inputs(0);
    if(observers || microphones || services || polls || game_on || microphone_on)return 10;
    fixture.status=RuntimeStartupStatus::Ready;initialize_voice_inputs();initialize_voice_inputs();
    if(observers!=1 || microphones!=1 || game_on || microphone_on)return 11;
    request_voice_inputs(settings);if(!game_on || microphone_on || selected_voice_mouth_snapshot().activity!=VoiceActivity::Talking)return 12;
    settings.voice_source=VoiceInputSource::Microphone;request_voice_inputs(settings);if(game_on || !microphone_on)return 13;
    settings.voice_enabled=false;request_voice_inputs(settings);if(game_on || microphone_on)return 14;
    std::cout<<"native voice admission, once-only setup, off-by-default and explicit game/mic routing passed; fake backends only\n";return 0;
}
