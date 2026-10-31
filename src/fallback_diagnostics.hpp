#pragma once
#include <cstdint>
#include <array>
#include <cstring>
#include <string>
#include <stdexcept>
#include <utility>
namespace evrm {
inline void fallback_copy_reason(std::array<char,160>& out,const char* text)noexcept {
    out.fill(0);if(!text)return;
    for(unsigned n=0;n+1<out.size()&&text[n];++n)out[n]=static_cast<unsigned char>(text[n])<32?' ':text[n];
}
struct FallbackReasonSamples {
    std::array<std::array<char,160>,4> values{};
    unsigned count=0;bool overflow=false;
    void add(const char* text)noexcept {
        if(!text||!*text)return;
        std::array<char,160> bounded{};fallback_copy_reason(bounded,text);
        for(unsigned n=0;n<count;++n)if(values[n]==bounded)return;
        if(count<values.size())values[count++]=bounded;else overflow=true;
    }
};
struct FallbackEvent {
    bool emit=false;
    std::uint64_t episode=0,failures=0,elapsed_ms=0,suppressed_episodes=0;
    std::uint64_t total_failures=0,suppressed_failures=0,reason_mask=0;
    FallbackReasonSamples details;
};
// The caller already owns its renderer/capture lock. At most one failure line
// per second plus its single recovery; rapid alternating episodes are counted.
class FallbackEpisodes {
public:
    FallbackEvent failure(std::uint64_t now,std::uint64_t reason_mask=0,const char* reason=nullptr)noexcept {
        if(!active_){active_=true;logged_=false;first_=now;failures_=0;++episodes_;}
        ++failures_;++total_failures_;reasons_|=reason_mask;details_.add(reason);
        const bool emit=!emitted_||now<last_emit_||now-last_emit_>=1000;
        if(emit){emitted_=logged_=true;last_emit_=now;}
        else{++suppressed_failures_;if(failures_==1)++suppressed_;}
        return event(emit,now);
    }
    FallbackEvent recovery(std::uint64_t now)noexcept {
        if(!active_)return {};
        active_=false;auto result=event(logged_,now);
        if(logged_){reasons_=0;details_={};}
        return result;
    }
    std::uint64_t episodes()const noexcept{return episodes_;}
private:
    bool active_=false,logged_=false,emitted_=false;
    std::uint64_t first_=0,last_emit_=0,episodes_=0,failures_=0,suppressed_=0;
    std::uint64_t total_failures_=0,suppressed_failures_=0,reasons_=0;
    FallbackReasonSamples details_;
    FallbackEvent event(bool emit,std::uint64_t now)const noexcept {
        return {emit,episodes_,failures_,now>=first_?now-first_:0,suppressed_,total_failures_,suppressed_failures_,reasons_,details_};
    }
};
inline std::string fallback_event_summary(const FallbackEvent& event){
    std::string result=" total-failures="+std::to_string(event.total_failures)+" suppressed-failures="+std::to_string(event.suppressed_failures)+
        " reason-mask="+std::to_string(event.reason_mask)+" reason-overflow="+std::to_string(event.details.overflow)+" observed-reasons={";
    for(unsigned n=0;n<event.details.count;++n){if(n)result+=';';result+=event.details.values[n].data();}
    return result+'}';
}
enum class FallbackGate:std::uint64_t {
    Shape=1ull<<0,Eligible=1ull<<1,Untainted=1ull<<2,ValidBundle=1ull<<3,Recorded=1ull<<4,Ended=1ull<<5,
    VisibilityPass=1ull<<6,GbufferPass=1ull<<7,VisibilityDraw=1ull<<8,GbufferDraw=1ull<<9,
    Camera=1ull<<10,Owner=1ull<<11,Parts=1ull<<12,Arena=1ull<<13,Offsets=1ull<<14,Latest=1ull<<15,PoseAge=1ull<<16,
    Sources=1ull<<17,CameraSource=1ull<<18,OwnerSource=1ull<<19,Shadows=1ull<<20,Mixed=1ull<<21,
    Clips=1ull<<22,Mask=1ull<<23,Memory=1ull<<24,AffectedPass=1ull<<25,OwnershipWindow=1ull<<26,
    CompetingOwnership=1ull<<27,ExcludedTransforms=1ull<<28,OwnerFreshness=1ull<<29,
    BodyBuild=1ull<<30,QualifiedParts=1ull<<31,BodyWords=1ull<<32,AgeAfterTool=1ull<<33,AgeAfterSeed=1ull<<34
};
struct FallbackDecision {
    std::uint64_t failed=0;
    // Wrap the condition where it is already evaluated; preserve && ordering.
    bool check(bool condition,FallbackGate gate)noexcept{if(!condition)failed|=std::uint64_t(gate);return condition;}
};
inline std::string fallback_gate_names(std::uint64_t mask){
    const char* names[]={"shape","eligible","untainted","valid-bundle","recorded","ended","visibility-pass","gbuffer-pass","visibility-draw","gbuffer-draw",
        "camera","owner","parts","arena","offsets","latest","pose-age","sources","camera-source","owner-source","shadows","mixed",
        "clips","mask","memory","affected-pass","ownership-window","competing-ownership","excluded-transforms","owner-freshness","body-build",
        "qualified-parts","body-words","age-after-tool","age-after-seed"};
    std::string result;for(unsigned n=0;n<35;++n)if(mask&(1ull<<n)){if(!result.empty())result+=',';result+=names[n];}
    return result.empty()?"none":result;
}
struct FallbackCaptureMetadata {
    unsigned roles=0,view_index=0;bool asynchronous=false;
    std::uint64_t source_epoch=0,captured_milliseconds=0;
    template<class Snapshot>void observe(const Snapshot& snapshot,unsigned index=0)noexcept {
        roles=0;view_index=index;asynchronous=snapshot.asynchronous;source_epoch=snapshot.source_epoch;captured_milliseconds=snapshot.captured_milliseconds;
        // Compare existing keys directly; no temporary strings or allocation.
        for(const auto& entry:snapshot.buffers){const auto& name=entry.first;
            if(name=="instances")roles|=1;else if(name=="camera")roles|=2;else if(name=="skin")roles|=4;
            else if(name=="mesh-draw")roles|=8;else if(name=="culled-instances")roles|=16;}
    }
};
struct FallbackPoseDiagnostics {
    struct Failure {FallbackCaptureMetadata capture;const char* phase="begin";std::array<char,160> reason{};};
    std::array<Failure,4> failures{};unsigned count=0,retained_views=0;
    bool failed()const noexcept{return count!=0;}
    void record(const FallbackCaptureMetadata& capture,const char* phase,const char* reason)noexcept {
        if(count==failures.size())return;
        auto& failure=failures[count++];failure.capture=capture;failure.phase=phase;fallback_copy_reason(failure.reason,reason);
    }
};
template<class Snapshot> std::uint64_t native_view_instance_source(const Snapshot& view){
    const auto found=view.buffers.find("instances");
    if(found==view.buffers.end())throw std::runtime_error("Native view missing instances capture");
    return reinterpret_cast<std::uintptr_t>(found->second.binding.buffer);
}
template<class Callback> struct FallbackOutcomeScope {
    Callback callback;
    bool succeeded=false;
    const char* phase="begin";
    ~FallbackOutcomeScope()noexcept{try{callback(succeeded,phase);}catch(...) {}}
};
template<class Callback> auto track_fallback_outcome(Callback&& callback){
    return FallbackOutcomeScope<Callback>{std::forward<Callback>(callback)};
}
}
