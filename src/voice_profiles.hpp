#pragma once
#include "voice_features.hpp"
#include <cstdint>

namespace evrm {
struct VoiceProfile {std::uint8_t vowel=0;std::array<double,12> mfcc{};};
struct VoiceProfiles {std::array<VoiceProfile,160> data{};std::size_t count=0;};
VoiceProfiles default_voice_profiles()noexcept;
bool validate_voice_profiles(const VoiceProfiles&)noexcept;
}
