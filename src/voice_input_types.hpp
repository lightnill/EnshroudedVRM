#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace evrm {
enum class VoiceInputSource:std::uint8_t {Game,Microphone};
enum class MicrophoneState:std::uint8_t {Off,Starting,Ready,NoDevice,AccessDenied,Unavailable};
using MicrophoneDeviceId=std::shared_ptr<const std::string>;
struct MicrophoneDevice {std::string id,name;};
struct MicrophoneDeviceList {std::vector<MicrophoneDevice> devices;bool available=false;};
}
