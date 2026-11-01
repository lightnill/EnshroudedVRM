#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>

namespace evrm {
// Diagnostic scopes can overlap. Their means/maxima are not additive frame time.
enum class RuntimeHookKind:unsigned {StateLookup,ProcLookup,Probe,Graphics,Trace,NativeState,BeforeCommand,NativeWork,NativeSubmit,PresentCpu,PostPresent,Count};
constexpr unsigned runtime_hook_count=static_cast<unsigned>(RuntimeHookKind::Count);
constexpr unsigned runtime_hook_period(RuntimeHookKind kind)noexcept {
    return kind==RuntimeHookKind::NativeSubmit||kind==RuntimeHookKind::PresentCpu||kind==RuntimeHookKind::PostPresent?1u:256u;
}
inline const char* runtime_hook_name(RuntimeHookKind kind)noexcept {
    constexpr const char* names[]={"state","resolve","probe","graphics","trace","native-state","before-command","native-work","native-submit","present-cpu","post-present"};
    const auto index=static_cast<unsigned>(kind);return index<runtime_hook_count?names[index]:"unknown";
}
struct RuntimeHookRow {std::uint64_t samples=0,total_ns=0,max_ns=0;};
class RuntimeHookTiming {
    struct Bucket {std::mutex mutex;RuntimeHookRow row;};
    std::array<Bucket,runtime_hook_count> buckets_;
    static std::uint64_t add(std::uint64_t a,std::uint64_t b)noexcept {
        return b>std::numeric_limits<std::uint64_t>::max()-a?std::numeric_limits<std::uint64_t>::max():a+b;
    }
public:
    // No global counter, lock, allocation or clock read on unsampled hot calls.
    static bool turn(RuntimeHookKind kind)noexcept {
        const auto index=static_cast<unsigned>(kind);if(index>=runtime_hook_count)return false;
        if(runtime_hook_period(kind)==1)return true;
        thread_local std::array<std::uint32_t,runtime_hook_count> cursors{};
        const auto call=cursors[index]++;
        // One sample per block, with an odd rotating offset. Repeating hot
        // call-site patterns must not always land on the same sampled slot.
        const auto offset=((call>>8)*73u)&255u;
        return (call&255u)==offset;
    }
    void record(RuntimeHookKind kind,std::uint64_t nanoseconds)noexcept {
        const auto index=static_cast<unsigned>(kind);if(index>=runtime_hook_count)return;
        try{auto& bucket=buckets_[index];std::lock_guard<std::mutex> lock(bucket.mutex);
            bucket.row.samples=add(bucket.row.samples,1);bucket.row.total_ns=add(bucket.row.total_ns,nanoseconds);
            if(nanoseconds>bucket.row.max_ns)bucket.row.max_ns=nanoseconds;
        }catch(...){} // Profiling cannot change command execution or admission.
    }
    std::array<RuntimeHookRow,runtime_hook_count> take()noexcept {
        std::array<RuntimeHookRow,runtime_hook_count> rows{};
        for(unsigned n=0;n<runtime_hook_count;++n)try{
            auto& bucket=buckets_[n];std::lock_guard<std::mutex> lock(bucket.mutex);rows[n]=bucket.row;bucket.row={};
        }catch(...){}
        return rows;
    }
};
template<class Clock=std::chrono::steady_clock>class BasicRuntimeHookSample {
    RuntimeHookKind kind_;RuntimeHookTiming* target_=nullptr;bool sampled_=false;
    typename Clock::time_point started_{};
public:
    explicit BasicRuntimeHookSample(RuntimeHookKind kind,RuntimeHookTiming* target=nullptr)noexcept
        :kind_(kind),target_(target),sampled_(RuntimeHookTiming::turn(kind)){
        if(sampled_)started_=Clock::now();
    }
    BasicRuntimeHookSample(RuntimeHookKind kind,RuntimeHookTiming* target,typename Clock::time_point started)noexcept
        :kind_(kind),target_(target),sampled_(RuntimeHookTiming::turn(kind)),started_(started){}
    BasicRuntimeHookSample(const BasicRuntimeHookSample&)=delete;
    BasicRuntimeHookSample& operator=(const BasicRuntimeHookSample&)=delete;
    void attach(RuntimeHookTiming* target)noexcept {target_=target;}
    void finish()noexcept {
        if(!sampled_)return;
        sampled_=false;if(!target_)return;
        const auto ended=Clock::now();if(ended<started_)return;
        target_->record(kind_,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(ended-started_).count()));
    }
    ~BasicRuntimeHookSample(){finish();}
};
using RuntimeHookSample=BasicRuntimeHookSample<>;
}
