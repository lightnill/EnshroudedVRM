#pragma once
#include "voice_profiles.hpp"
#include "voice_stream.hpp"
#include "voice_automatic.hpp"

namespace evrm {
struct VoiceAnalysisTuning {float sensitivity=.5f;std::uint32_t smoothing_ms=60;};
struct VoiceAnalysisDiagnostics {float rms_db=-120,flatness=0,activation=0;bool valid=false;};
class VoiceVowelAnalyser {
public:
    explicit VoiceVowelAnalyser(const VoiceProfiles* profiles=nullptr)noexcept;
    void reset()noexcept;
    MouthSnapshot consume(const VoicePcmBlock&,const VoiceAnalysisTuning&)noexcept;
    VoiceAnalysisDiagnostics diagnostics()const noexcept{return diagnostics_;}
    bool ready()const noexcept{return custom_profiles_||automatic_.ready();}
    const char* diagnostic()const noexcept{return custom_profiles_?"custom-mfcc":automatic_.diagnostic();}
private:
    VoiceFeatureExtractor extractor_;
    AutomaticVoiceClassifier automatic_;
    VoiceProfiles profiles_;
    bool custom_profiles_=false;
    vrm::MouthWeights smoothed_{};
    std::uint64_t epoch_=0,last_timestamp_=0;
    unsigned quiet_ms_=0;
    VoiceAnalysisDiagnostics diagnostics_;
};
}
