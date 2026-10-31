#include "voice_analyser.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace evrm {
VoiceVowelAnalyser::VoiceVowelAnalyser(const VoiceProfiles* profiles)noexcept:automatic_(!(profiles&&validate_voice_profiles(*profiles))) {
    if(profiles&&validate_voice_profiles(*profiles)){profiles_=*profiles;custom_profiles_=true;}
}
void VoiceVowelAnalyser::reset()noexcept {
    extractor_.reset();automatic_.reset();smoothed_.fill(0);epoch_=last_timestamp_=0;quiet_ms_=0;diagnostics_={};
}
MouthSnapshot VoiceVowelAnalyser::consume(const VoicePcmBlock& block,const VoiceAnalysisTuning& tuning)noexcept {
    MouthSnapshot out;out.epoch=block.epoch;out.timestamp_us=block.timestamp_us;out.activity=VoiceActivity::Waiting;
    if(!ready()){out.activity=VoiceActivity::UnsupportedSource;return out;}
    if(!block.epoch||!block.timestamp_us){reset();return out;}
    if(epoch_==block.epoch&&last_timestamp_&&block.timestamp_us<=last_timestamp_){reset();return out;}
    // Timestamps measure arrival, not audio duration: a slow game frame can
    // deliver several contiguous 10 ms blocks in a burst. Match the source and
    // worker's interruption boundary; the worker separately resets on lost
    // sequence numbers, discarded blocks and source-generation changes.
    if(epoch_!=block.epoch||(last_timestamp_&&block.timestamp_us-last_timestamp_>100000))reset();
    epoch_=block.epoch;last_timestamp_=block.timestamp_us;
    const auto automatic=custom_profiles_?AutomaticVoicePrediction{}:automatic_.consume(block.samples);
    const auto features=extractor_.consume(block.samples);
    if(!features.valid){smoothed_.fill(0);quiet_ms_=0;diagnostics_={};return out;}
    const double sensitivity=std::isfinite(tuning.sensitivity)?std::clamp(double(tuning.sensitivity),0.,1.):.5;
    const double threshold=std::pow(10.,(-30-35*sensitivity)/20);
    const double level=std::clamp((features.rms/threshold-1)/2,0.,1.);
    const double clarity=std::clamp((.35-features.flatness)/.25,0.,1.);
    const double activation=level*clarity;
    diagnostics_={float(features.rms>0?std::clamp(20*std::log10(features.rms),-120.,24.):-120.),float(features.flatness),float(activation),true};
    vrm::MouthWeights target{};
    const bool voiced=activation>.02;
    if(voiced){
        if(!custom_profiles_){
            // Official SDK enum: aa/E/ih/oh/ou = 10/11/12/13/14.
            // Project its actual vowel weights; consonants have no authored
            // targets in this five-shape path and are not fabricated.
            static constexpr unsigned vowels[5]={10,12,14,11,13};
            if(automatic.ready)for(unsigned n=0;n<5;++n)target[n]=float(activation)*automatic.visemes[vowels[n]];
            target=vrm::bounded_mouth_weights(target);
        }else{
        std::array<double,5> distances;distances.fill(std::numeric_limits<double>::infinity());
        for(std::size_t n=0;n<profiles_.count;++n){const auto& profile=profiles_.data[n];double distance=0;
            for(unsigned c=0;c<12;++c){const double delta=features.mfcc[c]-profile.mfcc[c];distance+=delta*delta*(c<6?1:.5);}
            distances[profile.vowel]=std::min(distances[profile.vowel],distance/9);
        }
        const auto nearest=std::min_element(distances.begin(),distances.end());
        // Voice level determines amount; the nearest vowel determines pose.
        // Temporal smoothing below crossfades poses without diluting uncertain
        // classification into five simultaneous, potentially opposing shapes.
        if(std::isfinite(*nearest))target[std::size_t(nearest-distances.begin())]=float(activation);
        }
        quiet_ms_=0;
    }else quiet_ms_=std::min(120u,quiet_ms_+10);
    const auto smoothing=std::clamp(tuning.smoothing_ms,20u,140u);
    const double tau=voiced?smoothing:std::min(smoothing,40u),blend=1-std::exp(-10/tau);
    // Each accepted block is exactly 10 ms of audio, including burst delivery.
    for(unsigned n=0;n<5;++n){smoothed_[n]=float(smoothed_[n]+blend*(target[n]-smoothed_[n]));
        if(smoothed_[n]<.0001f)smoothed_[n]=0;}
    if(quiet_ms_>=120)smoothed_.fill(0);
    smoothed_=vrm::bounded_mouth_weights(smoothed_);out.weights=smoothed_;
    out.activity=voiced?VoiceActivity::Talking:VoiceActivity::Quiet;
    return out;
}
}
