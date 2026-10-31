#include "../src/voice_analyser.hpp"
#include "../src/voice_clock.hpp"
#include "../src/mouth_deformation.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846;
void need(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
double total(const evrm::MouthSnapshot& value){double sum=0;for(float w:value.weights){need(std::isfinite(w)&&w>=0&&w<=1,"invalid analysed weight");sum+=w;}
    need(sum<=1.0000002,"analysed weights exceed one");return sum;}
unsigned dominant(const evrm::MouthSnapshot& value){return unsigned(std::max_element(value.weights.begin(),value.weights.end())-value.weights.begin());}
// Independent time-domain all-pole synthesis: different centers, widths and pitch
// from the analytic defaults. These fixtures do not measure natural speech.
std::vector<evrm::VoicePcmBlock> vowel(unsigned index,double pitch=125){
    constexpr double centers[5][3]={{760,1280,2650},{320,2200,2900},{370,780,2400},{530,1740,2500},{530,980,2450}};
    constexpr double widths[3]={100,140,180};
    std::array<double,3> radius{},coefficient{},previous{},older{};
    for(unsigned k=0;k<3;++k){radius[k]=std::exp(-pi*widths[k]/48000);coefficient[k]=2*radius[k]*std::cos(2*pi*centers[index][k]/48000);}
    std::vector<evrm::VoicePcmBlock> result(100);double peak=0;
    for(unsigned n=0;n<48000;++n){double x=2*std::fmod(n*pitch/48000,1.)-1;
        for(unsigned k=0;k<3;++k){const double y=(1-radius[k])*x+coefficient[k]*previous[k]-radius[k]*radius[k]*older[k];
            older[k]=previous[k];previous[k]=y;x=y;}
        result[n/480].samples[n%480]=float(x);peak=std::max(peak,std::abs(x));
    }
    for(unsigned n=0;n<100;++n){auto& block=result[n];block.epoch=1;block.sequence=n;block.timestamp_us=(n+1)*10000;
        for(auto& sample:block.samples)sample=float(sample*.35/peak);}
    return result;
}
std::uint64_t cpu_time(){FILETIME creation{},exit{},kernel{},user{};
    need(GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user)!=FALSE,"CPU benchmark clock unavailable");
    return ((std::uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+((std::uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime);
}
}
int main(){
    const auto legacy_profiles=evrm::default_voice_profiles();
    evrm::VoiceAnalysisTuning tuning;evrm::VoiceVowelAnalyser quiet;
    evrm::VoicePcmBlock blank;blank.epoch=1;
    for(unsigned n=0;n<30;++n){blank.sequence=n;blank.timestamp_us=(n+1)*10000;const auto out=quiet.consume(blank,tuning);
        need(out.epoch==blank.epoch&&out.timestamp_us==blank.timestamp_us,"analysis lost source metadata");
        need(total(out)==0&&out.activity!=evrm::VoiceActivity::Talking,"silence animated the mouth");}
    for(unsigned label=0;label<5;++label){auto audio=vowel(label);evrm::VoiceVowelAnalyser analyser(&legacy_profiles);evrm::MouthSnapshot out;
        bool early=false;
        for(unsigned n=0;n<audio.size();++n){out=analyser.consume(audio[n],tuning);total(out);if(n<10&&total(out)>.005)early=true;}
        std::printf("synthetic vowel %u dominant=%u total=%.3f\n",label,dominant(out),total(out));
        need(early,"no synthetic voice response within 100 ms of input blocks");
        need(total(out)>.4&&out.activity==evrm::VoiceActivity::Talking,"voiced input stayed neutral");
        need(dominant(out)==label,"synthetic vowel channel confused or swapped");
    }
    auto audio=vowel(0);evrm::VoiceVowelAnalyser analyser(&legacy_profiles);evrm::MouthSnapshot out;
    // Classification ambiguity must not average incompatible authored poses
    // back into a closed mouth. Voice level controls amount; the selected vowel
    // controls pose, with temporal crossfade preserving bounded weights.
    evrm::VoiceFeatureExtractor reference;evrm::VoiceFeatures reference_feature;
    for(const auto& block:audio)reference_feature=reference.consume(block.samples);
    evrm::VoiceProfiles ambiguous;ambiguous.count=5;
    for(unsigned n=0;n<5;++n){ambiguous.data[n].vowel=std::uint8_t(n);ambiguous.data[n].mfcc=reference_feature.mfcc;}
    evrm::VoiceVowelAnalyser uncertain(&ambiguous);
    for(const auto& block:audio)out=uncertain.consume(block,{1,20});
    vrm::Vertex mouth_vertex;mouth_vertex.normal={0,0,1};vrm::MouthDeltaRecord shape;
    shape.position[0][1]=-.01f;for(unsigned n=1;n<5;++n)shape.position[n][1]=.0025f;
    need(vrm::apply_mouth_delta(mouth_vertex,shape,out.weights),"uncertain authored-pose fixture failed");
    std::printf("ambiguous profile authored mouth displacement=%f peak=%f\n",mouth_vertex.position[1],out.weights[dominant(out)]);
    need(mouth_vertex.position[1]<-.009f,"uncertain vowel probabilities cancel visible authored mouth pose");
    for(const auto& block:audio)out=analyser.consume(block,tuning);
    const auto before=total(out);tuning.smoothing_ms=140;
    for(unsigned n=0;n<18;++n){blank.sequence=100+n;blank.timestamp_us=1010000+n*10000;out=analyser.consume(blank,tuning);
        if(n==5)need(total(out)<before,"silent release ignored");}
    need(total(out)==0,"slow tuning left the mouth open after bounded release");
    for(auto block:audio){block.timestamp_us+=2000000;out=analyser.consume(block,tuning);}
    auto changed=audio.front();changed.epoch=2;changed.timestamp_us=4000000;
    need(total(analyser.consume(changed,tuning))==0,"epoch change replayed previous speech");
    changed.timestamp_us=4100000;need(total(analyser.consume(changed,tuning))==0,"input gap retained old feature window");
    changed.timestamp_us=4090000;need(total(analyser.consume(changed,tuning))==0,"backward timestamp replayed old speech");
    changed.timestamp_us=4200000;changed.samples.back()=std::numeric_limits<float>::quiet_NaN();
    need(total(analyser.consume(changed,tuning))==0,"invalid sample retained old mouth weights");

    evrm::VoiceVowelAnalyser noise;evrm::VoiceFeatureExtractor noise_features;std::uint32_t seed=0x12345678;
    double minimum_flatness=1,maximum_flatness=0;
    for(unsigned n=0;n<100;++n){evrm::VoicePcmBlock block;block.epoch=1;block.sequence=n;block.timestamp_us=(n+1)*10000;
        for(auto& sample:block.samples){seed=seed*1664525u+1013904223u;sample=float((double(seed>>8)/16777216*2-1)*.2);}
        const auto feature=noise_features.consume(block.samples);
        if(feature.valid){minimum_flatness=std::min(minimum_flatness,feature.flatness);maximum_flatness=std::max(maximum_flatness,feature.flatness);}
        out=noise.consume(block,{});total(out);}
    std::printf("synthetic white noise flatness=%.3f..%.3f mouth=%.3f\n",minimum_flatness,maximum_flatness,total(out));
    need(total(out)<.01&&out.activity!=evrm::VoiceActivity::Talking,"wide-band noise drove sustained speech weights");
    evrm::VoiceVowelAnalyser low_volume(&legacy_profiles);
    double tail_mean=0,tail_square=0;
    for(std::size_t n=audio.size()-4;n<audio.size();++n)for(float value:audio[n].samples)tail_mean+=value;
    tail_mean/=1920;
    for(std::size_t n=audio.size()-4;n<audio.size();++n)for(float value:audio[n].samples)tail_square+=(value-tail_mean)*(value-tail_mean);
    const double tail_rms=std::sqrt(tail_square/1920);
    // Fix the fixture at a physical RMS between the default and maximum gates;
    // a relative peak multiplier alone had accidentally fallen below both.
    const float quiet_scale=float(.001/tail_rms);
    for(auto block:audio){for(auto& sample:block.samples)sample*=quiet_scale;out=low_volume.consume(block,{});}
    need(total(out)<.01,"sub-threshold voice bypassed sensitivity");
    low_volume.reset();tuning.sensitivity=1;tuning.smoothing_ms=60;
    for(auto block:audio){for(auto& sample:block.samples)sample*=quiet_scale;out=low_volume.consume(block,tuning);}
    need(total(out)>.02,"higher sensitivity did not admit quiet voice");
    evrm::VoiceProfiles invalid;invalid.count=1;invalid.data[0].mfcc[0]=std::numeric_limits<double>::infinity();
    evrm::VoiceVowelAnalyser fallback(&invalid);tuning.sensitivity=std::numeric_limits<float>::quiet_NaN();tuning.smoothing_ms=0;
    for(const auto& block:audio)out=fallback.consume(block,tuning);
    total(out);need(fallback.diagnostics().valid&&fallback.diagnostics().activation>.4,"invalid optional profiles/tuning broke gate defaults");

    evrm::VoiceVowelAnalyser benchmark;evrm::VoiceClock clock;constexpr unsigned frames=2500;
    const auto cpu_start=cpu_time(),start=clock.ticks();
    for(unsigned n=0;n<frames;++n){auto& block=audio[n%audio.size()];block.epoch=7;block.sequence=n;block.timestamp_us=(n+1)*10000;benchmark.consume(block,{});}
    const auto elapsed=evrm::counter_duration_ns(start,clock.ticks(),clock.frequency()),cpu=cpu_time()-cpu_start;
    const double cpu_ms_per_second=double(cpu)*.0001/(frames*.01);
    std::printf("synthetic active analysis cpu-ms/audio-second=%.3f wall-ns/block=%.1f (not gameplay)\n",cpu_ms_per_second,double(elapsed)/frames);
    need(cpu_ms_per_second<=20,"isolated active analysis exceeds initial one-core budget");
    evrm::VoiceVowelAnalyser silent_benchmark;const auto silent_cpu=cpu_time(),silent_start=clock.ticks();
    for(unsigned n=0;n<frames;++n){blank.epoch=7;blank.sequence=n;blank.timestamp_us=(n+1)*10000;silent_benchmark.consume(blank,{});}
    const auto silent_elapsed=evrm::counter_duration_ns(silent_start,clock.ticks(),clock.frequency());
    const double silent_cpu_ms=double(cpu_time()-silent_cpu)*.0001/(frames*.01);
    std::printf("synthetic silent analysis cpu-ms/audio-second=%.3f wall-ns/block=%.1f (not gameplay)\n",silent_cpu_ms,double(silent_elapsed)/frames);
    need(silent_cpu_ms<=20,"isolated silent analysis exceeds initial one-core budget");
    std::puts("PASS vowel targets, neutral resets and isolated budget; natural-speech accuracy unmeasured");
}
