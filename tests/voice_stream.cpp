#include "../src/voice_stream.hpp"
#include "../src/voice_clock.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
#include <vector>

namespace {
void need(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
}
int main(){
    need(evrm::counter_duration_ns(100,480100,48000000)==10000000,"literal 48 MHz clock conversion");
    need(evrm::counter_duration_ns(20,1254,10000000)==123400,"literal 10 MHz clock conversion");
    need(evrm::counter_duration_ns(20,10,10000000)==0&&evrm::counter_duration_ns(0,10,0)==0,"invalid clock interval");
    need(evrm::counter_duration_ns(0,UINT64_MAX,1)==UINT64_MAX,"clock conversion overflow");
    evrm::VoiceClock clock;need(clock.valid(),"QPC unavailable in fixture");
    const auto ticks=clock.ticks(),ms=GetTickCount64(),us=clock.now_us();Sleep(25);
    const auto elapsed=evrm::counter_duration_ns(ticks,clock.ticks(),clock.frequency());
    const auto real_ms=GetTickCount64()-ms;
    need(elapsed>=15000000&&std::abs(double(elapsed)/1e6-real_ms)<25,"QPC interval disagrees with independent uptime clock");
    need(clock.now_us()>us,"audio timestamps did not advance");

    evrm::VoiceBlockQueue queue;evrm::VoicePcmBlock block,out;out.sequence=777;
    need(!queue.try_pop(out)&&out.sequence==777,"empty queue changed output");
    for(unsigned n=0;n<16;++n){block.sequence=n;block.epoch=9;block.timestamp_us=n*10000;block.samples.fill(float(n));
        need(queue.try_push(block),"queue rejected a free bounded slot");}
    need(!queue.try_push(block)&&queue.dropped()==1,"full queue did not drop the seventeenth block");
    for(unsigned n=0;n<16;++n){need(queue.try_pop(out),"queue lost accepted block");
        need(out.sequence==n&&out.epoch==9&&out.timestamp_us==n*10000&&out.samples.front()==n&&out.samples.back()==n,"queue reordered/tore a block");}
    need(!queue.try_pop(out),"drained queue was not empty");
    block.sequence=999;need(queue.try_push(block)&&queue.try_pop(out)&&out.sequence==999,"queue slot not reusable");

    constexpr unsigned producers=4,per_producer=1500,total=producers*per_producer;
    evrm::VoiceBlockQueue concurrent;std::array<std::atomic<bool>,total> accepted{};
    std::array<unsigned,total> seen{};std::atomic<unsigned> active{producers};std::vector<std::thread> writers;
    for(unsigned p=0;p<producers;++p)writers.emplace_back([&,p]{
        for(unsigned n=0;n<per_producer;++n){const auto id=p*per_producer+n;
            evrm::VoicePcmBlock value;value.sequence=id;value.epoch=77;value.timestamp_us=id*1000;value.samples.fill(float(id));
            accepted[id].store(concurrent.try_push(value));}
        active.fetch_sub(1);
    });
    unsigned received=0;
    const auto receive=[&](const evrm::VoicePcmBlock& value){need(value.sequence<total,"concurrent block id out of range");
            const auto id=unsigned(value.sequence);need(++seen[id]==1&&value.epoch==77&&value.timestamp_us==id*1000&&
                value.samples.front()==id&&value.samples.back()==id,"concurrent block duplicated/torn");++received;};
    while(active.load()!=0){evrm::VoicePcmBlock value;
        if(concurrent.try_pop(value))receive(value);else std::this_thread::yield();
    }
    for(auto& writer:writers)writer.join();
    evrm::VoicePcmBlock final;while(concurrent.try_pop(final))receive(final);
    unsigned succeeded=0;for(unsigned n=0;n<total;++n){need(seen[n]==unsigned(accepted[n].load()),"accepted concurrent block missing");succeeded+=accepted[n].load();}
    need(received==succeeded&&received+concurrent.dropped()==total,"queue overload accounting wrong");

    evrm::MouthSnapshotExchange exchange;evrm::MouthSnapshot snapshot;
    snapshot.epoch=3;snapshot.timestamp_us=12345;snapshot.activity=evrm::VoiceActivity::Talking;
    snapshot.weights={{.1f,.2f,.3f,.15f,.25f}};exchange.publish(snapshot);
    evrm::MouthSnapshot copy;need(exchange.read(copy),"snapshot publication absent");
    need(copy.epoch==3&&copy.timestamp_us==12345&&copy.activity==evrm::VoiceActivity::Talking,"snapshot metadata changed");
    double sum=0;for(unsigned n=0;n<5;++n){need(std::isfinite(copy.weights[n])&&copy.weights[n]>=0&&
        std::abs(copy.weights[n]-snapshot.weights[n])<=1.f/4095,"snapshot quantization exceeded one step");sum+=copy.weights[n];}
    need(sum<=1.0000001,"snapshot weights exceed one");
    snapshot.weights={{2,-1,.8f,0,0}};exchange.publish(snapshot);need(exchange.read(copy),"bounded publication failed");
    sum=0;for(float w:copy.weights){need(w>=0&&w<=1,"snapshot amplitude unbounded");sum+=w;}need(sum<=1.0000001,"overweight snapshot not normalized");
    snapshot.weights[2]=std::numeric_limits<float>::quiet_NaN();exchange.publish(snapshot);need(exchange.read(copy),"invalid weight publication failed");
    for(float w:copy.weights)need(w==0,"nonfinite snapshot was not neutral");

    std::atomic<bool> done{false};
    std::thread publish([&]{for(std::uint64_t n=1;n<=30000;++n){evrm::MouthSnapshot value;
        value.epoch=n;value.timestamp_us=n*1000;value.activity=evrm::VoiceActivity::Talking;
        value.weights[n%5]=1;exchange.publish(value);}done.store(true);});
    while(!done.load()){evrm::MouthSnapshot value;if(!exchange.read(value)||value.epoch==3)continue;
        need(value.epoch>0&&value.timestamp_us==value.epoch*1000&&value.activity==evrm::VoiceActivity::Talking,
            "concurrent snapshot metadata tore");
        for(unsigned n=0;n<5;++n)need(value.weights[n]==(n==value.epoch%5?1.f:0.f),"concurrent snapshot weights tore");
    }
    publish.join();need(exchange.read(copy)&&copy.epoch==30000,"final snapshot missing");
    std::puts("PASS bounded voice queue, coherent snapshots and monotonic clock");
}
