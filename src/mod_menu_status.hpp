#pragma once
#include "voice_stream.hpp"
#include "voice_input_types.hpp"
#include "avatar_service_types.hpp"
#include "debug_report_types.hpp"
namespace evrm {
enum class ModMouthSupport:std::uint8_t {Ready,Partial,NoAvatar,NoShapes,Unavailable};
struct ModMenuStatus {
    VoiceActivity voice=VoiceActivity::Off;
    ModMouthSupport mouth=ModMouthSupport::NoAvatar;
    bool starting=false,save_failed=false;
    bool startup_ready=false;
    const char* startup_version=nullptr; // Immutable process-lifetime context.
    MicrophoneState microphone=MicrophoneState::Off;
    std::shared_ptr<const MicrophoneDeviceList> microphones{};
    std::shared_ptr<const AvatarMenuSnapshot> avatar;
    std::shared_ptr<const DebugReportStatus> report;
    std::uint64_t report_texture=0; // Menu-owned texture, never exported.
};
}
