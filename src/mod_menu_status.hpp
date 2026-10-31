#pragma once
#include "voice_stream.hpp"
#include "voice_input_types.hpp"
#include "avatar_service_types.hpp"
namespace evrm {
enum class ModMouthSupport:std::uint8_t {Ready,Partial,NoAvatar,NoShapes,Unavailable};
struct ModMenuStatus {
    VoiceActivity voice=VoiceActivity::Off;
    ModMouthSupport mouth=ModMouthSupport::NoAvatar;
    bool starting=false,save_failed=false;
    MicrophoneState microphone=MicrophoneState::Off;
    std::shared_ptr<const MicrophoneDeviceList> microphones{};
    std::shared_ptr<const AvatarMenuSnapshot> avatar;
};
}
