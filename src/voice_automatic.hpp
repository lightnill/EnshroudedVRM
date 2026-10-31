#pragma once
#include <array>
#include <cstdint>
namespace evrm {
struct AutomaticVoicePrediction {bool ready=false;std::array<float,15> visemes{};int frame_number=0,delay_ms=0;};
class AutomaticVoiceClassifier {
public:
    explicit AutomaticVoiceClassifier(bool enabled=true)noexcept;
    ~AutomaticVoiceClassifier();
    AutomaticVoiceClassifier(const AutomaticVoiceClassifier&)=delete;
    AutomaticVoiceClassifier& operator=(const AutomaticVoiceClassifier&)=delete;
    bool ready()const noexcept;
    const char* diagnostic()const noexcept;
    void reset()noexcept;
    AutomaticVoicePrediction consume(const std::array<float,480>&)noexcept;
private:
    const void* library_=nullptr;
    std::uint32_t context_=0;
    bool failed_=false;
};
}
