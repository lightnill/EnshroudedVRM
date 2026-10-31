#include "voice_features.hpp"
#include <algorithm>
#include <cmath>
namespace evrm {
namespace {constexpr double pi=3.14159265358979323846;}
VoiceFeatureExtractor::VoiceFeatureExtractor()noexcept {
    for(unsigned n=0;n<256;++n){const double angle=-2*pi*n/512;twiddles_[n]={std::cos(angle),std::sin(angle)};}
    for(unsigned n=0;n<400;++n)hann_[n]=.5-.5*std::cos(2*pi*n/399);
    double total=0;
    constexpr double cutoff=7000./48000;
    for(unsigned n=0;n<31;++n){const int offset=int(n)-15;
        const double sinc=offset?std::sin(2*pi*cutoff*offset)/(pi*offset):2*cutoff;
        fir_[n]=sinc*(.54-.46*std::cos(2*pi*n/30));total+=fir_[n];}
    for(auto& value:fir_)value/=total;
    std::array<double,28> edges{};
    const double high_mel=2595*std::log10(1+8000./700);
    for(unsigned n=0;n<28;++n)edges[n]=700*(std::pow(10,high_mel*n/27/2595)-1);
    std::array<double,257> noise_power{};
    for(unsigned bin=0;bin<=256;++bin){const double f=bin*16000./512;
        noise_power[bin]=resampler_power_gain(f)*(1+.97*.97-2*.97*std::cos(2*pi*f/16000));}
    for(unsigned band=0;band<26;++band){double area=0;
        for(unsigned bin=0;bin<=256;++bin){const double f=bin*16000./512;
            double weight=0;
            if(f>edges[band]&&f<edges[band+2])weight=f<edges[band+1]?
                (f-edges[band])/(edges[band+1]-edges[band]):(edges[band+2]-f)/(edges[band+2]-edges[band+1]);
            mel_[band][bin]=weight;area+=weight;}
        if(area>0)for(auto& value:mel_[band])value/=area;
        double noise_gain=0;for(unsigned bin=0;bin<=256;++bin)noise_gain+=mel_[band][bin]*noise_power[bin];
        noise_gain=std::max(1e-12,noise_gain);noise_inverse_[band]=1/noise_gain;noise_log_mean_+=std::log(noise_gain)/26;
    }
    for(unsigned c=0;c<12;++c)for(unsigned band=0;band<26;++band)
        dct_[c][band]=std::sqrt(2./26)*std::cos(pi*(c+1)*(band+.5)/26);
}
void VoiceFeatureExtractor::reset()noexcept {
    history_.fill(0);window_.fill(0);history_at_=window_at_=window_count_=0;phase_=0;
}
VoiceFeatures VoiceFeatureExtractor::consume(const std::array<float,480>& samples)noexcept {
    for(float value:samples)if(!std::isfinite(value)||std::abs(value)>16){reset();return {};}
    for(float value:samples){
        history_[history_at_]=value;history_at_=(history_at_+1)%31;
        if(++phase_<3)continue;
        phase_=0;double filtered=0;
        for(unsigned n=0;n<31;++n)filtered+=fir_[n]*history_[(history_at_+30-n)%31];
        window_[window_at_]=filtered;window_at_=(window_at_+1)%400;
        window_count_=std::min<std::size_t>(400,window_count_+1);
    }
    if(window_count_<400)return {};
    std::array<double,400> frame;
    for(unsigned n=0;n<400;++n)frame[n]=window_[(window_at_+n)%400];
    return features_for_window(frame);
}
void VoiceFeatureExtractor::fft_power(const std::array<double,512>& input,std::array<double,257>& power)const noexcept {
    std::array<std::complex<double>,512> values;
    for(unsigned n=0;n<512;++n){unsigned reverse=0,bits=n;
        for(unsigned bit=0;bit<9;++bit){reverse=(reverse<<1)|(bits&1);bits>>=1;}
        values[reverse]={input[n],0};
    }
    for(unsigned length=2;length<=512;length*=2){const auto half=length/2,stride=512/length;
        for(unsigned start=0;start<512;start+=length)for(unsigned n=0;n<half;++n){
            const auto left=values[start+n],right=values[start+n+half]*twiddles_[n*stride];
            values[start+n]=left+right;values[start+n+half]=left-right;
        }
    }
    for(unsigned n=0;n<=256;++n)power[n]=std::norm(values[n])/(512.*512);
}
VoiceFeatures VoiceFeatureExtractor::features_for_window(const std::array<double,400>& frame)const noexcept {
    double mean=0;
    for(double value:frame){if(!std::isfinite(value)||std::abs(value)>32)return {};mean+=value;}
    mean/=400;
    double square=0,previous=0;std::array<double,512> input{};
    for(unsigned n=0;n<400;++n){const double centered=frame[n]-mean;
        square+=centered*centered/400;input[n]=(centered-.97*previous)*hann_[n];previous=centered;}
    const double rms=std::sqrt(square);
    if(rms<=1e-15){VoiceFeatures silent;silent.valid=true;return silent;}
    std::array<double,257> power;fft_power(input,power);
    return features_from_power(power,rms);
}
VoiceFeatures VoiceFeatureExtractor::features_from_power(const std::array<double,257>& power,double rms)const noexcept {
    if(!std::isfinite(rms)||rms<0||rms>32)return {};
    double maximum=0;for(double value:power){if(!std::isfinite(value)||value<0)return {};maximum=std::max(maximum,value);}
    VoiceFeatures result;result.valid=true;
    if(maximum==0)return result;
    result.rms=rms;std::array<double,26> logs{};double mean_log=0,mean_energy=0;
    const double floor=std::max(1e-30,maximum*1e-12);
    for(unsigned band=0;band<26;++band){double energy=0;
        for(unsigned bin=0;bin<=256;++bin)energy+=power[bin]*mel_[band][bin];
        energy=std::max(floor,energy);if(!std::isfinite(energy))return {};
        logs[band]=std::log(energy);mean_log+=logs[band]/26;mean_energy+=energy*noise_inverse_[band]/26;
    }
    if(!std::isfinite(mean_energy))return {};
    // Noise gating compensates for our own filter/pre-emphasis coloration. The
    // cepstrum below keeps the original energies and its profile convention.
    result.flatness=std::clamp(std::exp(mean_log-noise_log_mean_)/mean_energy,0.,1.);
    for(unsigned c=0;c<12;++c){for(unsigned band=0;band<26;++band)result.mfcc[c]+=dct_[c][band]*logs[band];
        if(!std::isfinite(result.mfcc[c]))return {};}
    return result;
}
double VoiceFeatureExtractor::resampler_power_gain(double frequency)const noexcept {
    if(!std::isfinite(frequency)||frequency<0||frequency>24000)return 0;
    std::complex<double> response{};
    for(unsigned n=0;n<31;++n){const double angle=-2*pi*frequency*n/48000;
        response+=fir_[n]*std::complex<double>(std::cos(angle),std::sin(angle));}
    return std::norm(response);
}
}
