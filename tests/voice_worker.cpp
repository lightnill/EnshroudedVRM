#include "../src/voice_worker.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
template<class F> void until(F predicate,const char* why){const auto start=GetTickCount64();while(!predicate()){
    need(GetTickCount64()-start<2000,why);Sleep(1);}}
struct TestClock {
    std::atomic<std::uint64_t> now{1000000};
    std::atomic<bool> blocked{false},entered{false};
    static std::uint64_t read(void* context)noexcept {
        auto& clock=*static_cast<TestClock*>(context);clock.entered=true;
        while(clock.blocked.load())Sleep(1);
        return clock.now.load();
    }
    evrm::VoiceTimeSource source(){return {read,this};}
};
std::array<float,480> tone(){std::array<float,480> value{};
    for(unsigned n=0;n<480;++n)value[n]=float(.1*std::sin(6.283185307179586*200*n/48000));
    return value;}
double total(const evrm::MouthSnapshot& value){double sum=0;for(auto weight:value.weights){
    need(std::isfinite(weight)&&weight>=0&&weight<=1,"invalid worker weight");sum+=weight;}
    need(sum<=1.000001,"worker weights exceed one");return sum;}
void feed(evrm::VoiceAnalysisWorker& worker,TestClock& clock,unsigned count,std::uint64_t id=7){
    const auto samples=tone();
    for(unsigned n=0;n<count;++n){const auto processed=worker.stats().processed;clock.now.fetch_add(10000);
        need(worker.submit(samples,id,clock.now.load()),"fresh frame rejected");
        until([&]{return worker.stats().processed>processed;},"worker did not consume fresh frame");}
}
}
int main(){
    const auto profiles=evrm::default_voice_profiles();
    const auto samples=tone();
    {
        TestClock clock;evrm::VoiceAnalysisWorker worker(&profiles,clock.source());need(worker.ready(),"worker initialization unavailable");
        need(!worker.submit(samples,7,clock.now)&&worker.stats().submitted==0,"disabled worker enqueued audio");
        need(worker.snapshot(clock.now).activity==evrm::VoiceActivity::Off,"disabled worker status");
        worker.set_enabled(true);
        need(!worker.submit(samples,7,clock.now,0),"explicit invalid capture token was silently replaced");
        feed(worker,clock,12);auto out=worker.snapshot(clock.now);
        need(total(out)>.2&&out.activity==evrm::VoiceActivity::Talking,"worker never published fresh voice");
        const auto measured=worker.stats();need(measured.analysis_nanoseconds>0&&measured.diagnostics.valid,"worker scalar diagnostics missing");
        need(measured.diagnostics.rms_db>-25&&measured.diagnostics.rms_db<-21&&measured.diagnostics.activation>.5f&&
            measured.diagnostics.flatness>=0&&measured.diagnostics.flatness<=1,"worker scalar levels disagree with known input");
        const auto old_epoch=out.epoch;clock.now.fetch_add(10000);
        const auto in_flight=worker.begin_capture(7,clock.now);need(in_flight!=0,"capture token missing");
        worker.set_enabled(false);need(total(worker.snapshot(clock.now))==0&&!worker.stats().diagnostics.valid,"disable retained voice or diagnostics");
        worker.set_enabled(true);need(total(worker.snapshot(clock.now))==0,"re-enable replayed voice");
        need(!worker.submit(samples,7,clock.now,in_flight),"pre-disable in-flight frame became fresh speech");
        feed(worker,clock,6);out=worker.snapshot(clock.now);need(total(out)>.1&&out.epoch>old_epoch,"fresh re-enable did not recover");
        need(total(worker.snapshot(clock.now+180000))==0,"stopped source stayed open after 180 ms");
        const auto before_source=worker.stats().epoch;clock.now.fetch_add(10000);
        const auto changed=worker.begin_capture(8,clock.now);need(changed&&worker.stats().epoch>before_source,"source change did not reset epoch");
        need(total(worker.snapshot(clock.now))==0,"source change retained old voice");
        need(!worker.stats().diagnostics.valid,"source change retained prior scalar levels");
        const auto processed=worker.stats().processed;need(worker.submit(samples,8,clock.now,changed),"new source token rejected");
        until([&]{return worker.stats().processed>processed;},"new source not processed");
        need(total(worker.snapshot(clock.now))==0,"new source reused old feature window");
        const auto before_gap=worker.stats().epoch;clock.now.fetch_add(110000);
        need(worker.begin_capture(8,clock.now)&&worker.stats().epoch>before_gap,"long source gap did not reset epoch");
        const auto before_backward=worker.stats().epoch;
        need(!worker.begin_capture(8,clock.now-10000)&&worker.stats().epoch>before_backward,"backward source timestamp was accepted");
    }
    {
        TestClock clock;clock.blocked=true;clock.now=1070000;
        evrm::VoiceAnalysisWorker worker(&profiles,clock.source());worker.set_enabled(true);
        until([&]{return clock.entered.load();},"worker did not enter controlled clock");
        for(unsigned n=0;n<8;++n)need(worker.submit(samples,7,1000000+n*10000),"bounded burst rejected early");
        clock.blocked=false;
        until([&]{return worker.stats().processed==4;},"burst did not retain exactly newest four usable frames");
        need(worker.snapshot(clock.now).timestamp_us==1070000,"burst published an older frame");
        clock.entered=false;clock.blocked=true;worker.set_enabled(false);worker.set_enabled(true);
        until([&]{return clock.entered.load();},"worker did not pause for overflow test");
        clock.now=1260000;const auto processed=worker.stats().processed;
        for(unsigned n=0;n<16;++n)need(worker.submit(samples,7,1100000+n*10000),"queue capacity smaller than contract");
        const auto prior_epoch=worker.stats().epoch;
        need(!worker.submit(samples,7,1260000),"full queue failed to drop");
        need(worker.stats().epoch>prior_epoch,"overflow left old queued speech valid");clock.blocked=false;
        until([&]{return worker.stats().discarded>=16;},"overflow queue was not discarded");
        need(worker.stats().processed==processed&&total(worker.snapshot(clock.now))==0,"overflow replayed delayed mouth movement");
        feed(worker,clock,6);need(total(worker.snapshot(clock.now))>.1,"worker did not recover from overload");
    }
    {
        TestClock clock;clock.blocked=true;evrm::VoiceAnalysisWorker worker(&profiles,clock.source());worker.set_enabled(true);
        until([&]{return clock.entered.load();},"stale-test worker did not pause");
        need(worker.submit(samples,7,clock.now),"stale-test input rejected early");clock.now.fetch_add(40001);clock.blocked=false;
        until([&]{return worker.stats().discarded!=0;},"older than 40 ms frame was not discarded");
        need(worker.stats().processed==0&&total(worker.snapshot(clock.now))==0,"stale queued audio was analysed");
    }
    std::puts("PASS worker lifetime, capture epochs, burst/overflow and bounded freshness");
}
