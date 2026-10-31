#pragma once
#include <array>
#include <complex>
#include <cstddef>

namespace evrm {
inline constexpr char voice_feature_convention[]="evrm-mfcc-16k-400hann-512-26area-c1c12-v1";
struct VoiceFeatures {
    std::array<double,12> mfcc{};
    double rms=0,flatness=1;
    bool valid=false;
};
class VoiceFeatureExtractor {
public:
    VoiceFeatureExtractor()noexcept;
    void reset()noexcept;
    VoiceFeatures consume(const std::array<float,480>&)noexcept;
    void fft_power(const std::array<double,512>&,std::array<double,257>&)const noexcept;
    VoiceFeatures features_for_window(const std::array<double,400>&)const noexcept;
    VoiceFeatures features_from_power(const std::array<double,257>&,double rms)const noexcept;
    double resampler_power_gain(double frequency)const noexcept;
private:
    std::array<std::complex<double>,256> twiddles_{};
    std::array<double,400> hann_{},window_{};
    std::array<std::array<double,257>,26> mel_{};
    std::array<double,26> noise_inverse_{};
    double noise_log_mean_=0;
    std::array<std::array<double,26>,12> dct_{};
    std::array<double,31> fir_{},history_{};
    std::size_t history_at_=0,window_at_=0,window_count_=0;
    unsigned phase_=0;
};
}
