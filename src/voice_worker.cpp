#include "voice_worker.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace evrm {
namespace {
struct ProducerRelease {std::atomic_flag& flag;~ProducerRelease(){flag.clear(std::memory_order_release);}};
VoiceAnalysisTuning unpack_tuning(std::uint64_t bits)noexcept {
    VoiceAnalysisTuning value;const auto low=std::uint32_t(bits);std::memcpy(&value.sensitivity,&low,4);
    value.smoothing_ms=std::uint32_t(bits>>32);return value;
}
std::uint64_t pack_diagnostics(const VoiceAnalysisDiagnostics& value,std::uint64_t epoch)noexcept {
    if(!value.valid||!std::isfinite(value.rms_db)||!std::isfinite(value.flatness)||!std::isfinite(value.activation))return 0;
    const auto quantize=[](double x){return std::uint64_t(std::llround(std::clamp(x,0.,1.)*65535));};
    return quantize((value.rms_db+120)/144.)|(quantize(value.flatness)<<16)|(quantize(value.activation)<<32)|(std::uint64_t(1)<<48)|((epoch&32767)<<49);
}
VoiceAnalysisDiagnostics unpack_diagnostics(std::uint64_t word,std::uint64_t epoch)noexcept {
    if(!(word&(std::uint64_t(1)<<48))||(word>>49)!=(epoch&32767))return {};
    return {float(double(word&65535)*144/65535-120),float(double((word>>16)&65535)/65535),float(double((word>>32)&65535)/65535),true};
}
}
VoiceAnalysisWorker::VoiceAnalysisWorker(const VoiceProfiles* p,VoiceTimeSource t)noexcept:analyser_(p),time_(t){
    set_tuning({});
    if(!analyser_.ready())return;
    if(!time_.now_us&&!clock_.valid())return;
    event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event_)return;
    try{thread_=std::thread([this]{run();});ready_=true;}
    catch(...){CloseHandle(event_);event_=nullptr;}
}
VoiceAnalysisWorker::~VoiceAnalysisWorker(){
    stopping_.store(true,std::memory_order_release);if(event_)SetEvent(event_);
    if(thread_.joinable())thread_.join();
    if(event_)CloseHandle(event_);
}
void VoiceAnalysisWorker::set_enabled(bool enabled)noexcept {
    if(!ready_)return;
    auto before=mode_.load(std::memory_order_acquire);
    while(bool(before&1)!=enabled){const auto after=((before+2)&~std::uint64_t(1))|std::uint64_t(enabled);
        if(mode_.compare_exchange_weak(before,after,std::memory_order_acq_rel)){SetEvent(event_);break;}}
}
void VoiceAnalysisWorker::set_tuning(VoiceAnalysisTuning value)noexcept {
    value.sensitivity=std::isfinite(value.sensitivity)?std::clamp(value.sensitivity,0.f,1.f):.5f;
    value.smoothing_ms=std::clamp(value.smoothing_ms,20u,140u);
    std::uint32_t bits=0;std::memcpy(&bits,&value.sensitivity,4);
    tuning_.store((std::uint64_t(value.smoothing_ms)<<32)|bits,std::memory_order_release);
}
void VoiceAnalysisWorker::invalidate()noexcept {
    mode_.fetch_add(2,std::memory_order_acq_rel);if(event_)SetEvent(event_);
}
std::uint64_t VoiceAnalysisWorker::begin_capture(std::uint64_t id,std::uint64_t timestamp)noexcept {
    if(!ready_||!id||!timestamp||!(mode_.load(std::memory_order_acquire)&1))return 0;
    if(producer_.test_and_set(std::memory_order_acquire)){dropped_.fetch_add(1);invalidate();return 0;}
    ProducerRelease release{producer_};auto mode=mode_.load(std::memory_order_acquire);if(!(mode&1))return 0;
    if(source_epoch_!=(mode>>1)){source_epoch_=mode>>1;source_id_=id;capture_timestamp_=0;}
    if(source_id_!=id||(capture_timestamp_&&timestamp<=capture_timestamp_)){
        const bool backward=source_id_==id;invalidate();source_epoch_=0;
        if(backward)return 0;
        mode=mode_.load(std::memory_order_acquire);source_epoch_=mode>>1;source_id_=id;capture_timestamp_=0;
    }else if(capture_timestamp_&&timestamp-capture_timestamp_>100000){
        invalidate();mode=mode_.load(std::memory_order_acquire);source_epoch_=mode>>1;capture_timestamp_=0;
    }
    if(!(mode&1))return 0;
    capture_timestamp_=timestamp;++capture_sequence_;
    return mode_.load(std::memory_order_acquire)==mode?mode>>1:0;
}
bool VoiceAnalysisWorker::submit(const std::array<float,480>& samples,std::uint64_t id,std::uint64_t timestamp)noexcept {
    const auto token=begin_capture(id,timestamp);return token&&submit(samples,id,timestamp,token);
}
bool VoiceAnalysisWorker::submit(const std::array<float,480>& samples,std::uint64_t id,std::uint64_t timestamp,std::uint64_t token)noexcept {
    if(!token)return false;
    if(producer_.test_and_set(std::memory_order_acquire)){dropped_.fetch_add(1);invalidate();return false;}
    ProducerRelease release{producer_};const auto mode=mode_.load(std::memory_order_acquire);
    if(!(mode&1)||(mode>>1)!=token||source_epoch_!=token||source_id_!=id||capture_timestamp_!=timestamp)return false;
    VoicePcmBlock block;block.samples=samples;block.epoch=token;block.sequence=capture_sequence_;block.timestamp_us=timestamp;
    if(!queue_.try_push(block)){dropped_.fetch_add(1);invalidate();return false;}
    submitted_.fetch_add(1,std::memory_order_relaxed);SetEvent(event_);return true;
}
std::uint64_t VoiceAnalysisWorker::now_us()const noexcept {
    return time_.now_us?time_.now_us(time_.context):clock_.now_us();
}
MouthSnapshot VoiceAnalysisWorker::snapshot(std::uint64_t now)const noexcept {
    MouthSnapshot result;const auto mode=mode_.load(std::memory_order_acquire);result.epoch=mode>>1;
    if(!ready_){result.activity=VoiceActivity::UnsupportedSource;return result;}
    if(!(mode&1))return result;
    result.activity=VoiceActivity::Waiting;MouthSnapshot published;
    if(!published_.read(published)||published.epoch!=(mode>>1)||mode_.load(std::memory_order_acquire)!=mode)return result;
    if(!now)now=now_us();
    if(!now||!published.timestamp_us||now<published.timestamp_us||now-published.timestamp_us>=180000)return result;
    const auto age=now-published.timestamp_us;
    if(age>80000){const auto scale=float(180000-age)/100000.f;for(auto& weight:published.weights)weight*=scale;}
    return published;
}
VoiceWorkerStats VoiceAnalysisWorker::stats()const noexcept {
    const auto mode=mode_.load(std::memory_order_acquire);
    VoiceWorkerStats result{submitted_.load(),processed_.load(std::memory_order_acquire),discarded_.load(),dropped_.load(),resets_.load(),mode>>1,bool(mode&1),analysis_nanoseconds_.load(),{}};
    if(mode&1)result.diagnostics=unpack_diagnostics(diagnostics_.load(std::memory_order_acquire),mode>>1);
    return result;
}
void VoiceAnalysisWorker::run()noexcept {
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    std::uint64_t worker_epoch=0,last_sequence=0,last_timestamp=0;
    auto reset=[&]{analyser_.reset();diagnostics_.store(0,std::memory_order_release);last_sequence=last_timestamp=0;resets_.fetch_add(1);};
    while(!stopping_.load(std::memory_order_acquire)){
        const auto wait=WaitForSingleObject(event_,(mode_.load(std::memory_order_acquire)&1)?20:INFINITE);
        if(stopping_.load(std::memory_order_acquire)||wait==WAIT_FAILED)break;
        const auto mode=mode_.load(std::memory_order_acquire),epoch=mode>>1;
        if(worker_epoch!=epoch){reset();worker_epoch=epoch;}
        if(!(mode&1)){
            VoicePcmBlock old;for(unsigned n=0;n<VoiceBlockQueue::capacity&&queue_.try_pop(old);++n)discarded_.fetch_add(1);
            MouthSnapshot off;off.epoch=epoch;published_.publish(off);continue;
        }
        const auto now=now_us();std::array<VoicePcmBlock,4> newest;unsigned count=0;VoicePcmBlock block;
        for(unsigned n=0;n<VoiceBlockQueue::capacity&&queue_.try_pop(block);++n){
            if(block.epoch!=epoch||!now||block.timestamp_us>now||now-block.timestamp_us>40000){discarded_.fetch_add(1);continue;}
            if(count==newest.size()){for(unsigned k=1;k<count;++k)newest[k-1]=newest[k];--count;discarded_.fetch_add(1);}
            newest[count++]=block;
        }
        for(unsigned n=0;n<count;++n){
            if(mode_.load(std::memory_order_acquire)!=mode)break;
            const auto& fresh=newest[n];if(last_sequence&&fresh.sequence!=last_sequence+1)reset();
            const auto started=clock_.ticks();auto out=analyser_.consume(fresh,unpack_tuning(tuning_.load(std::memory_order_acquire)));
            analysis_nanoseconds_.fetch_add(counter_duration_ns(started,clock_.ticks(),clock_.frequency()),std::memory_order_relaxed);
            last_sequence=fresh.sequence;last_timestamp=fresh.timestamp_us;
            if(mode_.load(std::memory_order_acquire)==mode){published_.publish(out);diagnostics_.store(pack_diagnostics(analyser_.diagnostics(),epoch),std::memory_order_release);}
            processed_.fetch_add(1,std::memory_order_release);
        }
        if(!count&&last_timestamp&&now>=last_timestamp&&now-last_timestamp>100000){
            reset();MouthSnapshot quiet;quiet.epoch=epoch;quiet.activity=VoiceActivity::Waiting;published_.publish(quiet);
        }
    }
}
}
