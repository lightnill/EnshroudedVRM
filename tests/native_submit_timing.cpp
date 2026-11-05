#include "native_fixture_compat.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#if __has_include("../src/native_submit_timing.hpp")
#include "../src/native_submit_timing.hpp"
#else
// The retained build has no correlated per-submit breakdown.
namespace evrm {
enum class NativeSubmitStage:unsigned {MutexWait,Retry,Views,History,Gates,OwnerVerify,Refresh,Capture,RefreshVerify,FinalChecks,Finalize,Count};
constexpr unsigned native_submit_stage_count=static_cast<unsigned>(NativeSubmitStage::Count);
struct NativeSubmitSample {std::uint64_t tick=0;bool legacy=false;std::uint64_t commands=0,bundles=0,menus=0,refreshes=0;double total_ms=0;std::array<double,native_submit_stage_count> phase_ms{};};
struct NativeSubmitReport {std::uint64_t samples=0,commands=0,bundles=0,menus=0,refreshes=0;double total_ms=0;std::array<double,native_submit_stage_count> phase_ms{};NativeSubmitSample worst;};
class NativeSubmitTiming {
public:bool begin(std::uint64_t,bool,double)noexcept{return false;}bool active()const noexcept{return false;}
    void add(NativeSubmitStage,double)noexcept{}void command()noexcept{}void bundle(bool)noexcept{}void refresh()noexcept{}void finish(double)noexcept{}
    NativeSubmitReport take_if_ready(std::uint64_t=300)noexcept{return {};}
};
template<class Clock=std::chrono::steady_clock>class BasicNativeSubmitStageScope {public:BasicNativeSubmitStageScope(NativeSubmitTiming&,NativeSubmitStage)noexcept{}void finish()noexcept{}};
template<class Clock=std::chrono::steady_clock>class BasicNativeSubmitCall {public:BasicNativeSubmitCall(NativeSubmitTiming&,typename Clock::time_point,std::uint64_t,bool)noexcept{}void finish()noexcept{}};
using NativeSubmitStageScope=BasicNativeSubmitStageScope<>;using NativeSubmitCall=BasicNativeSubmitCall<>;
}
#endif
static std::atomic<std::size_t> allocated{0};
EVRM_FIXTURE_NOINLINE void* operator new(std::size_t n){if(void* p=std::malloc(n?n:1)){++allocated;return p;}throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
EVRM_FIXTURE_NOINLINE void operator delete(void* p)noexcept{std::free(p);}
EVRM_FIXTURE_NOINLINE void operator delete[](void* p)noexcept{std::free(p);}
EVRM_FIXTURE_NOINLINE void operator delete(void* p,std::size_t)noexcept{std::free(p);}
EVRM_FIXTURE_NOINLINE void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
namespace {
using namespace evrm;using Stage=NativeSubmitStage;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
constexpr unsigned index(Stage value){return static_cast<unsigned>(value);}
struct Clock {
    using duration=std::chrono::microseconds;using time_point=std::chrono::time_point<Clock>;
    static std::int64_t ticks;static unsigned calls;
    static time_point now()noexcept{++calls;return time_point(duration(ticks));}
};
std::int64_t Clock::ticks=0;unsigned Clock::calls=0;
using Scope=BasicNativeSubmitStageScope<Clock>;using Call=BasicNativeSubmitCall<Clock>;
}
int main(int argc,char** argv){try{
    need(argc==2,"scenario required");const std::string mode=argv[1];NativeSubmitTiming timing;
    if(mode=="correlated-worst"){
        timing.begin(100,true,.5);timing.add(Stage::Capture,15);timing.add(Stage::RefreshVerify,3);timing.finish(23);
        timing.begin(200,false,.2);timing.add(Stage::Capture,18);timing.add(Stage::RefreshVerify,1);timing.finish(20);
        const auto report=timing.take_if_ready(2);
        need(report.samples==2&&report.total_ms==43&&report.worst.tick==100&&report.worst.total_ms==23&&report.worst.legacy,"worst total submit was not retained");
        need(report.worst.phase_ms[index(Stage::Capture)]==15&&report.worst.phase_ms[index(Stage::RefreshVerify)]==3&&report.phase_ms[index(Stage::Capture)]==33,"independent phase maxima were incorrectly combined into a fictional slow call");
    }else if(mode=="threshold"){
        for(unsigned n=0;n<299;++n){timing.begin(n,true,0);timing.finish(1);}
        need(!timing.take_if_ready().samples,"report emitted before bounded interval");timing.begin(299,true,0);timing.finish(2);
        const auto report=timing.take_if_ready();need(report.samples==300&&report.total_ms==301&&report.worst.tick==299,"ready report lost interval or worst call");
        need(!timing.take_if_ready(1).samples,"report snapshot did not reset counters");
    }else if(mode=="nested"){
        timing.begin(1,true,0);timing.add(Stage::Views,20);timing.add(Stage::Refresh,15);timing.add(Stage::Capture,10);timing.finish(21);
        const auto row=timing.take_if_ready(1);need(row.worst.phase_ms[index(Stage::Views)]==20&&row.worst.phase_ms[index(Stage::Refresh)]==15&&row.worst.phase_ms[index(Stage::Capture)]==10,"inclusive nested phases were normalized or summed as frame time");
    }else if(mode=="scope"){
        timing.begin(1,true,0);{Scope scope(timing,Stage::Gates);Clock::ticks=2500;scope.finish();Clock::ticks=9000;scope.finish();}timing.finish(10);
        need(timing.take_if_ready(1).worst.phase_ms[index(Stage::Gates)]==2.5&&Clock::calls==2,"explicit stage finish recorded twice or failed to time scope");
    }else if(mode=="inactive"){
        {Scope scope(timing,Stage::Capture);Clock::ticks=100;}
        timing.add(Stage::Capture,10);timing.command();timing.bundle(false);timing.refresh();timing.finish(20);
        need(!Clock::calls&&!timing.take_if_ready(1).samples,"inactive timing read a clock or contaminated another submission");
    }else if(mode=="early-return"||mode=="exception"){
        const auto original=Clock::now();Clock::ticks=2000;int returned=0;bool caught=false;
        const auto operation=[&]()->int{Call call(timing,original,77,true);Scope scope(timing,Stage::OwnerVerify);Clock::ticks=7000;
            if(mode=="exception"){throw std::runtime_error("original failure");}
            return -7;};
        try{returned=operation();}catch(const std::runtime_error&){caught=true;}
        const auto row=timing.take_if_ready(1);
        need((mode=="exception"?caught:returned==-7)&&!timing.active()&&row.samples==1&&row.worst.total_ms==7&&row.worst.phase_ms[index(Stage::MutexWait)]==2&&row.worst.phase_ms[index(Stage::OwnerVerify)]==5,"unwinding/early return changed result or lost timing before lock release");
    }else if(mode=="invalid"){
        timing.begin(1,true,-1);timing.add(Stage::Capture,std::numeric_limits<double>::quiet_NaN());timing.add(Stage::Gates,-10);timing.finish(std::numeric_limits<double>::infinity());
        need(!timing.active()&&!timing.take_if_ready(1).samples,"invalid or regressed duration made a false spike");
        timing.begin(2,true,0);timing.add(static_cast<Stage>(999),4);timing.finish(1);
        need(timing.take_if_ready(1).samples==1,"unknown timing stage affected submission record");
    }else if(mode=="double-begin"){
        need(timing.begin(1,true,0)&&!timing.begin(2,false,0),"nested begin replaced an in-progress submission");timing.finish(4);
        need(timing.take_if_ready(1).worst.tick==1,"nested begin changed the original sample identity");
    }else if(mode=="counters"){
        timing.begin(1,true,0);timing.command();timing.command();timing.bundle(false);timing.bundle(true);timing.refresh();timing.finish(2);
        const auto row=timing.take_if_ready(1);need(row.commands==2&&row.bundles==2&&row.menus==1&&row.refreshes==1&&row.worst.commands==2,"submit context counters were not correlated with their call");
    }else if(mode=="bounded"){
        const auto before=allocated.load();
        for(unsigned n=0;n<10000;++n){timing.begin(n,true,0);timing.add(Stage::Capture,1);timing.finish(2);(void)timing.take_if_ready();}
        need(allocated==before,"timing accumulation allocated per submission or guard");
        need(timing.take_if_ready(1).samples==100,"bounded report consumption lost its trailing window");
    }else if(mode=="benchmark"){
        constexpr unsigned count=100000;const auto start=std::chrono::steady_clock::now();
        for(unsigned n=0;n<count;++n){const auto entered=std::chrono::steady_clock::now();NativeSubmitCall call(timing,entered,n,true);
            {NativeSubmitStageScope gates(timing,Stage::Gates);std::atomic_signal_fence(std::memory_order_seq_cst);}
            {NativeSubmitStageScope verify(timing,Stage::OwnerVerify);std::atomic_signal_fence(std::memory_order_seq_cst);}}
        const auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
        need(timing.take_if_ready(1).samples==count,"benchmark did not run complete production timing scopes");
        std::cout<<"BENCH correlated-submit-timing calls="<<count<<" ns-per-call="<<elapsed/count<<'\n';
    }else throw std::runtime_error("unknown scenario");
    std::cout<<"PASS submit timing "<<mode<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
