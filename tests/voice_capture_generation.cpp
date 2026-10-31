#include "../src/local_voice_observation.hpp"
#include "../src/voice_worker.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

namespace {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
using QueryMemory=SIZE_T(WINAPI*)(LPCVOID,PMEMORY_BASIC_INFORMATION,SIZE_T);
QueryMemory real_query=nullptr;
evrm::VoiceAnalysisWorker* worker=nullptr;
const void* watched_samples=nullptr;
bool transition_on_copy=false,transition_observed=false;
bool transition_in_encoder=false;
unsigned forwarded=0,accepted=0;
SIZE_T WINAPI query_with_transition(LPCVOID source,PMEMORY_BASIC_INFORMATION output,SIZE_T size){
    const auto result=real_query(source,output,size);
    if(result&&transition_on_copy&&source==watched_samples){
        transition_on_copy=false;transition_observed=true;
        // Deterministic scheduling point after validation but before the native
        // copy. The token must already precede every checked read.
        worker->set_enabled(false);worker->set_enabled(true);
    }
    return result;
}
std::int64_t encode(void*,evrm::VoiceByteSpan* output,const evrm::VoiceFloatSpan*){
    need(GetLastError()==111,"capture admission changed encoder incoming error");
    if(transition_in_encoder){transition_in_encoder=false;worker->set_enabled(false);worker->set_enabled(true);}
    ++forwarded;output->data[0]=42;SetLastError(222);return 7;
}
std::uint64_t begin(void*,std::uint64_t source,std::uint64_t timestamp)noexcept{return worker->begin_capture(source,timestamp);}
void accept(void*,const std::array<float,480>& samples,std::uint64_t source,std::uint64_t timestamp,std::uint64_t token)noexcept {
    if(worker->submit(samples,source,timestamp,token))++accepted;
}
}
extern "C" {QueryMemory __wrap___imp_VirtualQuery=&query_with_transition;}
int main(){
    const auto address=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualQuery");
    static_assert(sizeof(address)==sizeof(real_query),"Win64 function pointer ABI");std::memcpy(&real_query,&address,sizeof real_query);
    need(real_query!=nullptr,"real memory validator missing");
    evrm::VoiceAnalysisWorker owned;worker=&owned;need(worker->ready(),"fixture worker unavailable");worker->set_enabled(true);
    std::array<float,480> samples{};for(unsigned n=0;n<480;++n)samples[n]=float(.1*std::sin(6.283185307179586*200*n/48000));
    const auto unchanged=samples;watched_samples=samples.data();transition_on_copy=true;
    std::array<std::uint64_t,3> encoder{0,1,0};std::uint8_t byte=0;
    evrm::VoiceByteSpan output{&byte,1};evrm::VoiceFloatSpan input{samples.data(),samples.size()};
    evrm::VoiceObservation observation;const evrm::VoiceFrameSink sink{nullptr,begin,accept};
    SetLastError(111);const auto result=evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true,&sink);
    need(GetLastError()==222&&result==7&&byte==42&&forwarded==1&&samples==unchanged,"generation transition changed original behavior");
    need(transition_observed,"copy-boundary scheduling point did not execute");
    need(accepted==0&&worker->stats().submitted==0,"pre-disable copied PCM acquired the re-enabled generation");
    for(unsigned n=0;n<5;++n){Sleep(10);SetLastError(111);
        evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true,&sink);}
    need(accepted==5&&forwarded==6,"fresh audio failed to recover after generation reset");
    transition_in_encoder=true;SetLastError(111);
    evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true,&sink,evrm::VoiceReadContract::BorrowedThroughReturn);
    need(accepted==5&&forwarded==7,"post-encode borrowed PCM adopted a re-enabled generation");
    for(unsigned n=0;n<5;++n){Sleep(10);SetLastError(111);
        evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true,&sink,evrm::VoiceReadContract::BorrowedThroughReturn);}
    need(accepted==10&&forwarded==12&&observation.snapshot().borrowed_calls==6,"borrowed audio failed to recover after generation reset");
    const auto before_unknown=worker->stats();const auto before_observation=observation.snapshot();
    std::array<std::uint64_t,3> other_encoder{0,1,0};Sleep(10);SetLastError(111);
    const auto unknown=evrm::observe_voice_encode(observation,other_encoder.data(),&output,&input,encode,true,&sink,evrm::VoiceReadContract::UnrecognizedCaller);
    need(unknown==7&&GetLastError()==222&&forwarded==13&&byte==42&&samples==unchanged,"unrecognized caller changed original encoding");
    need(accepted==10&&worker->stats().submitted==before_unknown.submitted&&worker->stats().epoch==before_unknown.epoch,
        "unrecognized caller entered the mouth worker or invalidated its legitimate generation");
    need(observation.snapshot().calls==before_observation.calls,"unrecognized caller observed unproven PCM");
    std::puts("PASS capture generation precedes checked copy; exact forwarding and fresh recovery");
}
