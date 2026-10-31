#pragma once
#include "voice_analyser.hpp"
#include <cstdint>
namespace evrm {
void initialize_local_voice_observer() noexcept;
void poll_local_voice_observer(std::uint64_t now_ms) noexcept;
void request_local_voice_feature(bool enabled,VoiceAnalysisTuning tuning)noexcept;
void service_local_voice_feature_requests()noexcept;
bool local_voice_feature_starting()noexcept;
MouthSnapshot local_voice_mouth_snapshot()noexcept;
}
