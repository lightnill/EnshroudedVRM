#include "voice_profiles.hpp"
#include <algorithm>
#include <cmath>
namespace evrm {
VoiceProfiles default_voice_profiles()noexcept {
    // Project-authored envelope heuristics, not measurements of a speaker or a
    // copied/trained corpus. See docs/research/2026-10-29-voice-profiles.md.
    constexpr double pi=3.14159265358979323846;
    constexpr double formants[5][3]={{800,1200,2600},{300,2300,3000},{350,800,2400},{500,1850,2600},{500,950,2400}};
    constexpr double bandwidths[3]={90,110,160},pitches[3]={110,170,230},tracts[3]={.9,1,1.1};
    VoiceFeatureExtractor reference;VoiceProfiles profiles;
    std::array<std::array<double,80>,3> excitation{};
    std::array<std::array<std::complex<double>,80>,3> delay{},delay2{};
    for(unsigned p=0;p<3;++p)for(unsigned h=1;h*pitches[p]<7800;++h){
        const double f=h*pitches[p],preemphasis=1+.97*.97-2*.97*std::cos(2*pi*f/16000);
        excitation[p][h]=reference.resampler_power_gain(f)*preemphasis/(h*h);
        const double angle=-2*pi*f/48000;delay[p][h]={std::cos(angle),std::sin(angle)};delay2[p][h]=delay[p][h]*delay[p][h];
    }
    for(std::uint8_t vowel=0;vowel<5;++vowel)for(double tract:tracts)for(unsigned p=0;p<3;++p){
        std::array<double,257> power{};
        std::array<double,3> a1{},a2{},gain{};
        for(unsigned pole=0;pole<3;++pole){const double radius=std::exp(-pi*bandwidths[pole]*tract/48000);
            a1[pole]=2*radius*std::cos(2*pi*formants[vowel][pole]*tract/48000);a2[pole]=radius*radius;gain[pole]=(1-radius)*(1-radius);}
        for(unsigned h=1;h*pitches[p]<7800;++h){const double f=h*pitches[p];double envelope=excitation[p][h];
            // A real vocal-tract resonator has a conjugate pole pair. Keeping
            // only the positive-frequency Lorentzian over-brightens envelopes.
            for(unsigned pole=0;pole<3;++pole){const auto denominator=std::complex<double>(1,0)-a1[pole]*delay[p][h]+a2[pole]*delay2[p][h];
                envelope*=gain[pole]/std::norm(denominator);}
            const double center=f*512/16000;
            const int low=std::max(0,int(std::floor(center))-3),high=std::min(256,int(std::ceil(center))+3);
            for(int bin=low;bin<=high;++bin){const double distance=std::abs(bin*16000./512-f)/80;
                if(distance<1)power[bin]+=envelope*(1-distance)*(1-distance);}
        }
        const double peak=*std::max_element(power.begin(),power.end());
        if(!(peak>0))return {};
        for(auto& value:power)value/=peak;
        const auto feature=reference.features_from_power(power,1);
        if(!feature.valid)return {};
        auto& profile=profiles.data[profiles.count++];profile.vowel=vowel;profile.mfcc=feature.mfcc;
    }
    return profiles;
}
bool validate_voice_profiles(const VoiceProfiles& profiles)noexcept {
    if(!profiles.count||profiles.count>profiles.data.size())return false;
    std::array<unsigned,5> counts{};
    for(std::size_t n=0;n<profiles.count;++n){const auto& profile=profiles.data[n];
        if(profile.vowel>=5||++counts[profile.vowel]>32)return false;
        for(double value:profile.mfcc)if(!std::isfinite(value)||std::abs(value)>100)return false;
    }
    return std::all_of(counts.begin(),counts.end(),[](unsigned n){return n>0;});
}
}
