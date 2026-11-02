#pragma once
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace evrm {
enum class NativeSubmitStage:unsigned {MutexWait,Retry,Views,History,Gates,OwnerVerify,Refresh,Capture,RefreshVerify,FinalChecks,Finalize,Count};
constexpr unsigned native_submit_stage_count=static_cast<unsigned>(NativeSubmitStage::Count);
inline const char* native_submit_stage_name(NativeSubmitStage stage)noexcept {
    constexpr const char* names[]={"wait","retry","views","history","gates","owner-verify","refresh","capture","refresh-verify","final-checks","finalize"};
    const auto index=static_cast<unsigned>(stage);return index<native_submit_stage_count?names[index]:"unknown";
}
struct NativeSubmitSample {
    std::uint64_t tick=0;bool legacy=false;
    std::uint64_t commands=0,bundles=0,menus=0,refreshes=0;
    double total_ms=0;std::array<double,native_submit_stage_count> phase_ms{};
};
struct NativeSubmitReport {
    std::uint64_t samples=0,commands=0,bundles=0,menus=0,refreshes=0;
    double total_ms=0;std::array<double,native_submit_stage_count> phase_ms{};
    NativeSubmitSample worst;
};
// Externally serialized by the backend mutex. Fixed storage, no file I/O,
// allocations, resource leases or admission decisions. Nested phases overlap.
class NativeSubmitTiming {
    bool active_=false;
    NativeSubmitSample current_;
    NativeSubmitReport window_;
public:
    bool active()const noexcept{return active_;}
    bool begin(std::uint64_t tick,bool legacy,double wait_ms)noexcept {
        if(active_)return false;
        active_=true;current_={};current_.tick=tick;current_.legacy=legacy;add(NativeSubmitStage::MutexWait,wait_ms);return true;
    }
    void add(NativeSubmitStage stage,double milliseconds)noexcept {
        const auto index=static_cast<unsigned>(stage);
        if(active_&&index<native_submit_stage_count&&std::isfinite(milliseconds)&&milliseconds>=0)current_.phase_ms[index]+=milliseconds;
    }
    void command()noexcept{if(active_)++current_.commands;}
    void bundle(bool menu)noexcept{if(active_){++current_.bundles;if(menu)++current_.menus;}}
    void refresh()noexcept{if(active_)++current_.refreshes;}
    void finish(double milliseconds)noexcept {
        if(!active_)return;
        active_=false;if(!std::isfinite(milliseconds)||milliseconds<0)return;
        current_.total_ms=milliseconds;
        if(!window_.samples||milliseconds>window_.worst.total_ms)window_.worst=current_;
        ++window_.samples;window_.total_ms+=milliseconds;window_.commands+=current_.commands;window_.bundles+=current_.bundles;
        window_.menus+=current_.menus;window_.refreshes+=current_.refreshes;
        for(unsigned n=0;n<native_submit_stage_count;++n)window_.phase_ms[n]+=current_.phase_ms[n];
    }
    NativeSubmitReport take_if_ready(std::uint64_t minimum=300)noexcept {
        if(!minimum||window_.samples<minimum)return {};
        const auto result=window_;window_={};return result;
    }
};
template<class Clock=std::chrono::steady_clock>class BasicNativeSubmitStageScope {
    NativeSubmitTiming& timing_;NativeSubmitStage stage_;bool active_=false;typename Clock::time_point started_{};
public:
    BasicNativeSubmitStageScope(NativeSubmitTiming& timing,NativeSubmitStage stage)noexcept
        :timing_(timing),stage_(stage),active_(timing.active()){if(active_)started_=Clock::now();}
    BasicNativeSubmitStageScope(const BasicNativeSubmitStageScope&)=delete;
    BasicNativeSubmitStageScope& operator=(const BasicNativeSubmitStageScope&)=delete;
    void finish()noexcept {
        if(!active_)return;
        active_=false;timing_.add(stage_,std::chrono::duration<double,std::milli>(Clock::now()-started_).count());
    }
    ~BasicNativeSubmitStageScope(){finish();}
};
template<class Clock=std::chrono::steady_clock>class BasicNativeSubmitCall {
    NativeSubmitTiming& timing_;typename Clock::time_point started_;bool active_=false;
public:
    BasicNativeSubmitCall(NativeSubmitTiming& timing,typename Clock::time_point started,std::uint64_t tick,bool legacy)noexcept
        :timing_(timing),started_(started){active_=timing_.begin(tick,legacy,std::chrono::duration<double,std::milli>(Clock::now()-started_).count());}
    BasicNativeSubmitCall(const BasicNativeSubmitCall&)=delete;
    BasicNativeSubmitCall& operator=(const BasicNativeSubmitCall&)=delete;
    void finish()noexcept {
        if(!active_)return;
        active_=false;timing_.finish(std::chrono::duration<double,std::milli>(Clock::now()-started_).count());
    }
    ~BasicNativeSubmitCall(){finish();}
};
using NativeSubmitStageScope=BasicNativeSubmitStageScope<>;
using NativeSubmitCall=BasicNativeSubmitCall<>;
}
