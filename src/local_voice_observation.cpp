#include "local_voice_observation.hpp"
#include "local_voice_copy.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace evrm {
namespace {
VoiceFrameSummary inspect_native_frame(void* encoder,const VoiceFloatSpan* input,std::array<float,480>& samples,const VoiceClock* clock,VoiceCopyTimings* timings,bool borrowed=false)noexcept {
    const auto address=reinterpret_cast<std::uintptr_t>(encoder);
    if(!address||address>std::numeric_limits<std::uintptr_t>::max()-8)return {};
    std::uint32_t channels=0;VoiceFloatSpan descriptor;
    const auto copy=borrowed?copy_borrowed_voice_bytes:copy_local_voice_bytes;
    if(!copy(reinterpret_cast<const void*>(address+8),&channels,sizeof channels,clock,timings)||channels!=1||
        !copy(input,&descriptor,sizeof descriptor,clock,timings)||descriptor.count!=480)return {};
    if(!copy(descriptor.data,samples.data(),sizeof samples,clock,timings))return {};
    return summarize_voice_frame(channels,samples.data(),samples.size());
}
std::uint64_t thread_cycles()noexcept {
    ULONG64 cycles=0;return QueryThreadCycleTime(GetCurrentThread(),&cycles)?cycles:0;
}
}
bool local_voice_identity_supported(const VoiceSourceIdentity& identity)noexcept {
    static constexpr std::array<std::uint8_t,32> digest{{
        0xd6,0x49,0x66,0xe2,0x3b,0x1a,0x45,0x8e,0xe6,0xf1,0x47,0xbc,0xbc,0xea,0xc0,0xb5,
        0x77,0xfa,0x7e,0x39,0xa2,0x12,0xf3,0x71,0x28,0x7d,0x8c,0xc1,0x19,0x81,0x78,0x82}};
    static constexpr std::array<std::uint8_t,16> prologue{{
        0x48,0x83,0xec,0x38,0x44,0x8b,0x49,0x08,0x4c,0x8b,0xd2,0x49,0x8b,0x40,0x08,0x33}};
    static constexpr std::array<std::uint8_t,5> call{{0xe8,0x8f,0xe2,0xc6,0xff}};
    return identity.file_sha256==digest&&identity.timestamp==1792507660u&&
        identity.image_bytes==44163072u&&identity.encoder_prologue==prologue&&identity.encoder_call==call;
}
VoiceFrameSummary summarize_voice_frame(std::uint32_t channels,const float* samples,std::size_t count)noexcept {
    if(channels!=1||count!=480||!samples)return {};
    VoiceFrameSummary out;
    double square=0;
    for(std::size_t n=0;n<count;++n){
        const float value=samples[n];
        if(!std::isfinite(value)||std::abs(value)>16)return {};
        out.peak=std::max(out.peak,std::abs(value));
        square+=double(value)*value;
    }
    out.valid=true;out.mean_square=square/count;out.nonzero=out.mean_square>1e-8;
    return out;
}
VoiceObservationSnapshot VoiceObservation::snapshot()const noexcept {
    constexpr auto order=std::memory_order_relaxed;
    return {calls_.load(order),accepted_.load(order),nonzero_.load(order),rejected_.load(order),
        encode_failures_.load(order),inspection_nanoseconds_.load(order),profiled_calls_.load(order),
        sampled_qpc_nanoseconds_.load(order),sampled_thread_cycles_.load(order),validation_ns_.load(order),copy_ns_.load(order),
        query_count_.load(order),copy_count_.load(order),pre_ns_.load(order),post_ns_.load(order),borrowed_calls_.load(order)};
}
std::int64_t observe_voice_encode(VoiceObservation& observation,void* encoder,VoiceByteSpan* output,const VoiceFloatSpan* input,VoiceEncoderFunction original,bool enabled,const VoiceFrameSink* sink,VoiceReadContract contract)noexcept {
    if(!original)return 0;
    if(!enabled||contract==VoiceReadContract::UnrecognizedCaller)return original(encoder,output,input);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"Voice callback counters must not lock");
    const auto thread_error=GetLastError();
    constexpr auto order=std::memory_order_relaxed;
    const auto ticket=observation.calls_.fetch_add(1,order);
    const bool profile=(ticket&63)==0;
    const auto started=observation.clock_.ticks(),cycles_before=profile?thread_cycles():0;
    const auto timestamp=observation.clock_.now_us(),source=reinterpret_cast<std::uint64_t>(encoder);
    // Admission must precede even the checked sample copy: a disabled/re-enabled
    // generation can never adopt PCM already observed by an older callback.
    const auto token=sink&&sink->begin&&sink->accept?sink->begin(sink->context,source,timestamp):0;
    std::array<float,480> samples;
    VoiceCopyTimings timings;
    const bool borrowed=contract==VoiceReadContract::BorrowedThroughReturn;
    VoiceFrameSummary summary;
    if(borrowed)observation.borrowed_calls_.fetch_add(1,order);
    else summary=inspect_native_frame(encoder,input,samples,profile?&observation.clock_:nullptr,profile?&timings:nullptr);
    const auto cycles_after=profile?thread_cycles():0;
    const auto before_ns=counter_duration_ns(started,observation.clock_.ticks(),observation.clock_.frequency());
    SetLastError(thread_error);
    // The original owns encoding and error behavior. Never change its arguments.
    const auto result=original(encoder,output,input);
    const auto original_error=GetLastError();
    const auto post_started=observation.clock_.ticks(),post_cycles_before=profile?thread_cycles():0;
    // The pinned caller owns both span and PCM in its active stack frame. Its
    // original encoder has just read them; the caller cannot reuse the frame
    // until this detour returns. Unknown callers never receive this contract.
    if(borrowed&&result>0)summary=inspect_native_frame(encoder,input,samples,profile?&observation.clock_:nullptr,profile?&timings:nullptr,true);
    if(!summary.valid)observation.rejected_.fetch_add(1,order);
    if(result<0)observation.encode_failures_.fetch_add(1,order);
    if(summary.valid&&result>0){
        observation.accepted_.fetch_add(1,order);
        if(summary.nonzero)observation.nonzero_.fetch_add(1,order);
        if(token)sink->accept(sink->context,samples,source,timestamp,token);
    }
    const auto post_cycles_after=profile?thread_cycles():0;
    const auto after_ns=counter_duration_ns(post_started,observation.clock_.ticks(),observation.clock_.frequency());
    observation.inspection_nanoseconds_.fetch_add(before_ns+after_ns,order);
    if(profile&&cycles_before&&cycles_after>=cycles_before&&post_cycles_before&&post_cycles_after>=post_cycles_before){
        observation.profiled_calls_.fetch_add(1,order);
        observation.sampled_qpc_nanoseconds_.fetch_add(before_ns+after_ns,order);
        observation.sampled_thread_cycles_.fetch_add(cycles_after-cycles_before+post_cycles_after-post_cycles_before,order);
        observation.validation_ns_.fetch_add(timings.validation_ns,order);observation.copy_ns_.fetch_add(timings.copy_ns,order);
        observation.query_count_.fetch_add(timings.queries,order);observation.copy_count_.fetch_add(timings.copies,order);
        observation.pre_ns_.fetch_add(before_ns,order);observation.post_ns_.fetch_add(after_ns,order);
    }
    SetLastError(original_error);
    return result;
}
}
