#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#if __has_include("../src/runtime_hook_timing.hpp")
#include "../src/runtime_hook_timing.hpp"
#else
// The old runtime collected none of these timings. Make that behavior
// executable for the RED check without inventing another implementation.
namespace evrm {
enum class RuntimeHookKind:unsigned {StateLookup,ProcLookup,Probe,Graphics,Trace,NativeState,BeforeCommand,NativeWork,NativeSubmit,PresentCpu,Count};
constexpr unsigned runtime_hook_count=static_cast<unsigned>(RuntimeHookKind::Count);
struct RuntimeHookRow {std::uint64_t samples=0,total_ns=0,max_ns=0;};
class RuntimeHookTiming {
public:void record(RuntimeHookKind,std::uint64_t)noexcept{}
    auto take()noexcept{return std::array<RuntimeHookRow,runtime_hook_count>{};}
};
template<class Clock=std::chrono::steady_clock>class BasicRuntimeHookSample {
public:BasicRuntimeHookSample(RuntimeHookKind,RuntimeHookTiming* =nullptr)noexcept{}
    void attach(RuntimeHookTiming*)noexcept{}void finish()noexcept{}
};
using RuntimeHookSample=BasicRuntimeHookSample<>;
}
#endif
namespace {
using namespace evrm;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
constexpr unsigned index(RuntimeHookKind kind){return static_cast<unsigned>(kind);}
struct Clock {
    using duration=std::chrono::nanoseconds;using rep=duration::rep;using period=duration::period;
    using time_point=std::chrono::time_point<Clock>;
    static constexpr bool is_steady=true;
    static std::int64_t ticks;static unsigned calls;
    static time_point now()noexcept{++calls;return time_point(duration(ticks));}
};
std::int64_t Clock::ticks=0;unsigned Clock::calls=0;
using Sample=BasicRuntimeHookSample<Clock>;
}
int main(int argc,char** argv){try{
    need(argc==2,"scenario required");const std::string mode=argv[1];RuntimeHookTiming timing;
    if(mode=="aggregate"){
        timing.record(RuntimeHookKind::Probe,20);timing.record(RuntimeHookKind::Probe,80);timing.record(RuntimeHookKind::Probe,40);
        const auto first=timing.take()[index(RuntimeHookKind::Probe)];
        need(first.samples==3&&first.total_ns==140&&first.max_ns==80,"profile did not aggregate the observed timings");
        const auto second=timing.take()[index(RuntimeHookKind::Probe)];need(!second.samples&&!second.total_ns&&!second.max_ns,"profile snapshot did not reset the interval");
    }else if(mode=="sampling"){
        for(unsigned n=0;n<2048;++n){Sample sample(RuntimeHookKind::Probe,&timing);Clock::ticks+=10;}
        const auto row=timing.take()[index(RuntimeHookKind::Probe)];
        need(row.samples==8&&row.total_ns==80&&Clock::calls==16,"hot hook timing did not sample exactly once per 256 calls");
    }else if(mode=="phase-coverage"){
        for(unsigned n=0;n<65536;++n){Sample sample(RuntimeHookKind::NativeWork,&timing);Clock::ticks+=(n&255u)+1;}
        const auto row=timing.take()[index(RuntimeHookKind::NativeWork)];
        need(row.samples==256&&row.total_ns==32896&&row.max_ns==256,"sampling phase locked to one operation in a repeating 256-call pattern");
    }else if(mode=="present"||mode=="post-present"){
        const auto kind=mode=="present"?RuntimeHookKind::PresentCpu:static_cast<RuntimeHookKind>(10);
        need(index(kind)<runtime_hook_count,"work after driver Present is not measured");
        for(unsigned n=0;n<100;++n){Sample sample(kind,&timing);Clock::ticks+=100;}
        const auto row=timing.take()[index(kind)];
        need(row.samples==100&&row.total_ns==10000&&Clock::calls==200,"low-frequency Present work was not measured each call");
    }else if(mode=="attach"){
        {Sample sample(RuntimeHookKind::StateLookup);Clock::ticks=25;sample.attach(&timing);sample.finish();Clock::ticks=100;sample.finish();}
        const auto row=timing.take()[index(RuntimeHookKind::StateLookup)];
        need(row.samples==1&&row.total_ns==25&&Clock::calls==2,"device-lookup timing could not attach later or was recorded twice");
    }else if(mode=="nested"){
        {Sample outer(RuntimeHookKind::PresentCpu,&timing);Clock::ticks=10;
            {Sample inner(RuntimeHookKind::NativeSubmit,&timing);Clock::ticks=20;}Clock::ticks=30;}
        const auto rows=timing.take();need(rows[index(RuntimeHookKind::PresentCpu)].total_ns==30&&
            rows[index(RuntimeHookKind::NativeSubmit)].total_ns==10,"inclusive timing scopes were mixed into one category");
    }else if(mode=="regressed"){
        Clock::ticks=100;{Sample sample(RuntimeHookKind::NativeSubmit,&timing);Clock::ticks=99;}
        const auto row=timing.take()[index(RuntimeHookKind::NativeSubmit)];need(!row.samples&&!row.total_ns&&!row.max_ns,"clock regression produced a giant false hitch");
    }else if(mode=="no-target"){
        {Sample sample(RuntimeHookKind::StateLookup);Clock::ticks=10;sample.finish();}
        need(Clock::calls==1,"unresolved device timing took another clock sample or required a target");
        for(const auto& row:timing.take())need(!row.samples,"unresolved device wrote another device's timings");
    }else if(mode=="threads"){
        std::atomic<unsigned> done{0};std::vector<std::thread> writers;
        for(unsigned t=0;t<4;++t)writers.emplace_back([&]{for(unsigned n=0;n<5000;++n)timing.record(RuntimeHookKind::Graphics,1);++done;});
        std::uint64_t count=0,total=0;
        const auto collect=[&]{const auto row=timing.take()[index(RuntimeHookKind::Graphics)];need(row.samples==row.total_ns&&row.max_ns==(row.samples?1u:0u),"concurrent interval snapshot was incoherent");count+=row.samples;total+=row.total_ns;};
        while(done.load()!=4){collect();std::this_thread::yield();}
        for(auto& thread:writers){thread.join();}
        collect();need(count==20000&&total==20000,"concurrent profile lost or double-counted timings");
    }else if(mode=="per-device"){
        RuntimeHookTiming other;
        {Sample sample(RuntimeHookKind::PresentCpu,&timing);Clock::ticks+=30;}
        {Sample sample(RuntimeHookKind::PresentCpu,&other);Clock::ticks+=70;}
        need(timing.take()[index(RuntimeHookKind::PresentCpu)].total_ns==30&&other.take()[index(RuntimeHookKind::PresentCpu)].total_ns==70,"profiles leaked between device lifetimes");
    }else if(mode=="benchmark"){
        constexpr unsigned count=2000000;const auto start=std::chrono::steady_clock::now();
        for(unsigned n=0;n<count;++n){RuntimeHookSample sample(RuntimeHookKind::Probe,&timing);std::atomic_signal_fence(std::memory_order_seq_cst);}
        const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
        const auto row=timing.take()[index(RuntimeHookKind::Probe)];need(row.samples>=count/256&&row.samples<=(count+255)/256,"benchmark did not exercise the production sparse timing path");
        std::cout<<"BENCH sampled-hook-timer calls="<<count<<" measured="<<row.samples<<" ns-per-call="<<ns/count<<'\n';
    }else throw std::runtime_error("unknown scenario");
    std::cout<<"PASS hook timing "<<mode<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
