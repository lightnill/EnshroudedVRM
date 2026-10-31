#include "../src/voice_features.hpp"
#include "../src/voice_profiles.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
constexpr double pi=3.14159265358979323846;
void need(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
evrm::VoiceFeatures tone(double frequency){
    evrm::VoiceFeatureExtractor extractor;evrm::VoiceFeatures result;
    for(unsigned frame=0;frame<8;++frame){std::array<float,480> data;
        for(unsigned n=0;n<480;++n)data[n]=float(.3*std::sin(2*pi*frequency*(frame*480+n)/48000));
        result=extractor.consume(data);
    }
    return result;
}
}
int main(){
    evrm::VoiceFeatureExtractor extractor;
    std::array<double,512> input{};std::array<double,257> power{};
    for(unsigned n=0;n<512;++n)input[n]=.7*std::cos(2*pi*16*n/512)+.25*std::sin(2*pi*29*n/512);
    extractor.fft_power(input,power);
    need(std::abs(power[16]-.1225)<1e-11&&std::abs(power[29]-.015625)<1e-11,"literal FFT tone powers");
    for(unsigned bin:{0u,1u,3u,16u,29u,64u,128u,256u}){
        double re=0,im=0;
        for(unsigned n=0;n<512;++n){re+=input[n]*std::cos(2*pi*bin*n/512);im-=input[n]*std::sin(2*pi*bin*n/512);}
        need(std::abs(power[bin]-(re*re+im*im)/(512.*512))<1e-11,"FFT differs from independent direct DFT");
    }
    double energy=power[0]+power[256];for(unsigned n=1;n<256;++n)energy+=2*power[n];
    need(std::abs(energy-.27625)<1e-11,"FFT Parseval energy mismatch");
    input.fill(0);input[200]=1;extractor.fft_power(input,power);
    for(double p:power)need(std::abs(p-1./262144)<1e-12,"impulse FFT power mismatch");

    std::array<double,400> window{};
    auto features=extractor.features_for_window(window);
    need(features.valid&&features.rms==0&&features.flatness==1,"silence features invalid");
    for(double c:features.mfcc)need(c==0,"silence cepstrum not neutral");
    window.fill(.5);features=extractor.features_for_window(window);
    need(features.valid&&features.rms==0,"DC offset counted as voice energy");
    for(unsigned n=0;n<400;++n)window[n]=.5*std::sin(2*pi*1000*n/16000);
    const auto loud=extractor.features_for_window(window);
    need(loud.valid&&std::abs(loud.rms-std::sqrt(.125))<1e-9,"literal AC RMS incorrect");
    for(auto& value:window)value*=.5;
    const auto quiet=extractor.features_for_window(window);
    need(quiet.valid&&std::abs(quiet.rms/loud.rms-.5)<1e-10,"RMS volume scaling incorrect");
    for(unsigned n=0;n<12;++n)need(std::isfinite(loud.mfcc[n])&&std::abs(loud.mfcc[n]-quiet.mfcc[n])<1e-7,"non-energy cepstrum changes with volume");
    window.back()=std::numeric_limits<double>::quiet_NaN();
    need(!extractor.features_for_window(window).valid,"nonfinite window accepted");

    const auto pass=tone(1000),alias=tone(12000);
    need(pass.valid&&alias.valid&&pass.rms>.19&&pass.rms<.23,"resampler pass band damaged");
    need(alias.rms<pass.rms*.03,"12 kHz alias not attenuated before decimation");
    std::array<float,480> zeros{};extractor.reset();
    need(!extractor.consume(zeros).valid&&!extractor.consume(zeros).valid,"feature window warmed before 400 samples");
    need(extractor.consume(zeros).valid,"feature window failed to warm");
    zeros.back()=std::numeric_limits<float>::infinity();need(!extractor.consume(zeros).valid,"invalid streaming sample accepted");
    zeros.fill(0);need(!extractor.consume(zeros).valid,"invalid input left an old feature window live");
    evrm::VoiceFeatureExtractor white;std::uint32_t seed=0x12345678;double lowest=1;
    for(unsigned frame=0;frame<40;++frame){std::array<float,480> block;
        for(auto& sample:block){seed=seed*1664525u+1013904223u;sample=float((double(seed>>8)/16777216*2-1)*.2);}
        const auto result=white.consume(block);if(result.valid)lowest=std::min(lowest,result.flatness);}
    need(lowest>.6,"analysis filters incorrectly color the noise-flatness gate");

    const auto profiles=evrm::default_voice_profiles();
    need(profiles.count==45&&evrm::validate_voice_profiles(profiles),"owned default profiles invalid");
    std::array<unsigned,5> per_vowel{};for(std::size_t n=0;n<profiles.count;++n)++per_vowel[profiles.data[n].vowel];
    for(unsigned n:per_vowel)need(n==9,"default profile variants missing");
    auto changed=profiles;changed.count=161;need(!evrm::validate_voice_profiles(changed),"unbounded profile count accepted");
    changed=profiles;changed.data[0].vowel=5;need(!evrm::validate_voice_profiles(changed),"unknown profile vowel accepted");
    changed=profiles;changed.data[0].mfcc[11]=std::numeric_limits<double>::infinity();
    need(!evrm::validate_voice_profiles(changed),"nonfinite profile accepted");
    changed=profiles;changed.data[0].mfcc[0]=101;need(!evrm::validate_voice_profiles(changed),"unbounded profile feature accepted");
    changed=profiles;changed.count=0;need(!evrm::validate_voice_profiles(changed),"empty profile set accepted");
    changed=profiles;changed.count=9;need(!evrm::validate_voice_profiles(changed),"incomplete vowel bank accepted");
    std::puts("PASS independent spectral math, resampling and owned profile bounds (no natural-speech accuracy claim)");
}
