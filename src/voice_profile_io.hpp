#pragma once
#include "voice_profiles.hpp"
#include <filesystem>
namespace evrm {
struct VoiceProfileLoad {VoiceProfiles profiles;bool custom=false;const char* diagnostic=nullptr;};
VoiceProfileLoad load_voice_profiles(const std::filesystem::path&)noexcept;
}
