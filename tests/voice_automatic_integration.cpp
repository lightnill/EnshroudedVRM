#include "../src/voice_analyser.hpp"
#include "../src/voice_worker.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <vector>
namespace {void need(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL %s\n",text);std::exit(1);}}}
int main(int argc,char** argv){
    need(argc==2,"public PCM fixture required");std::ifstream input(argv[1],std::ios::binary);need(bool(input),"public PCM fixture");
    // Game-frame delivery can batch contiguous 10 ms audio blocks. Arrival
    // jitter must not repeatedly wipe the detector, feature window or smoothing.
    std::vector<std::array<float,480>> audio;std::array<float,480> samples;
    while(input.read(reinterpret_cast<char*>(samples.data()),sizeof samples))audio.push_back(samples);
    need(audio.size()>2000,"public burst fixture duration");input.clear();input.seekg(0);
    std::vector<evrm::MouthSnapshot> continuous;unsigned continuous_valid=0;
    evrm::VoiceVowelAnalyser reference;
    for(unsigned n=0;n<audio.size();++n){continuous.push_back(reference.consume({audio[n],1,n+1,1000000+std::uint64_t(n)*10000},{.71f,27}));
        continuous_valid+=reference.diagnostics().valid;}
    for(unsigned burst:{3,5,7}){evrm::VoiceVowelAnalyser batched;double maximum_error=0;unsigned valid=0;
        for(unsigned n=0;n<audio.size();++n){const auto arrival=1000000+std::uint64_t(n/burst)*burst*10000+(n%burst)*100;
            const auto out=batched.consume({audio[n],1,n+1,arrival},{.71f,27});valid+=batched.diagnostics().valid;
            for(unsigned v=0;v<5;++v)maximum_error=std::max(maximum_error,std::abs(double(out.weights[v])-continuous[n].weights[v]));}
        std::printf("public speech burst=%u valid=%u/%u max-weight-error=%.9g\n",burst,valid,continuous_valid,maximum_error);
        need(valid==continuous_valid&&maximum_error<1e-6,"contiguous game-frame batches reset or weaken voice analysis");
    }
    const auto interrupted=reference.consume({audio.back(),1,std::uint64_t(audio.size()+1),
        continuous.back().timestamp_us+100001},{.71f,27});
    need(!reference.diagnostics().valid,"actual interruption retained the old feature window");
    for(float weight:interrupted.weights)need(weight==0,"actual interruption retained the old mouth pose");
    evrm::VoiceVowelAnalyser analyser;evrm::VoiceProfiles invalid;invalid.count=1;invalid.data[0].mfcc[0]=std::numeric_limits<double>::infinity();
    evrm::VoiceVowelAnalyser fallback(&invalid);evrm::VoicePcmBlock block;block.epoch=1;std::array<float,5> maximum{};unsigned talking=0;
    std::atomic<std::uint64_t> now{1000000};
    const evrm::VoiceTimeSource time{[](void* state)noexcept{return static_cast<std::atomic<std::uint64_t>*>(state)->load();},&now};
    evrm::VoiceAnalysisWorker worker(nullptr,time);need(worker.ready(),"default automatic worker unavailable");worker.set_enabled(true);float worker_peak=0;
    while(input.read(reinterpret_cast<char*>(block.samples.data()),sizeof block.samples)){block.timestamp_us+=10000;++block.sequence;
        const auto output=analyser.consume(block,{}),defaulted=fallback.consume(block,{});float sum=0;
        need(output.weights==defaulted.weights,"invalid custom profile did not fall back to the automatic model");
        for(unsigned n=0;n<5;++n){const auto weight=output.weights[n];need(std::isfinite(weight)&&weight>=0&&weight<=1,"finite bounded automatic mouth weights");sum+=weight;maximum[n]=std::max(maximum[n],weight);}
        need(sum<=1.00001,"combined mouth weights exceed one");talking+=output.activity==evrm::VoiceActivity::Talking;
        if(block.sequence<=300){now=1000000+block.timestamp_us;const auto before=worker.stats().processed;
            need(worker.submit(block.samples,17,now.load()),"default automatic worker rejected fresh public PCM");const auto start=GetTickCount64();
            while(worker.stats().processed==before){need(GetTickCount64()-start<2000,"default automatic worker stalled");Sleep(1);}
            const auto published=worker.snapshot(now.load());for(auto weight:published.weights)worker_peak=std::max(worker_peak,weight);}
    }
    for(float weight:maximum)std::printf("%.3f ",weight);
    std::puts("automatic vowel peak weights");
    for(float weight:maximum)need(weight>.05f,"automatic public speech did not drive all five authored vowels");
    need(talking>200,"automatic speech activation missing");block.samples.fill(0);
    need(worker_peak>.2f&&worker.stats().dropped==0&&worker.stats().discarded==0,"default automatic worker failed real speech publication");
    worker.set_enabled(false);for(auto weight:worker.snapshot(now.load()).weights)need(weight==0,"automatic worker disable retained mouth pose");
    for(unsigned n=0;n<18;++n){block.timestamp_us+=10000;++block.sequence;const auto quiet=analyser.consume(block,{});if(n==17)for(auto weight:quiet.weights)need(weight==0,"automatic silence retained mouth pose");}
    auto output=analyser.consume({block.samples,2,1,block.timestamp_us+10000},{});
    for(auto weight:output.weights)need(weight==0,"source reset retained automatic mouth pose");
    std::puts("PASS default automatic analyser: varied authored vowels, bounded weights, silence and source reset");
}
