#pragma once
#include "native_owner.hpp"
#include "native_owner_profile.hpp"
#include "native_preview_handoff.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace evrm {
// Diagnostic only. Neither a call path nor a matching buffer authorizes an
// avatar replacement. This history carries no actor IDs, pointers or payloads.
enum class NativePreviewTraceStage:std::uint8_t {Bindings=1,Inline=2,Submit=3};
constexpr std::uint32_t native_preview_code_group_count=72886;
inline unsigned native_preview_trace_path(std::uint32_t group)noexcept {
    if((group>=27451&&group<=27455))return 1;
    if((group>=6484&&group<=6490))return 2;
    if((group>=42953&&group<=42955))return 4;
    if(group==40857||group==41258||(group>=41439&&group<=41445)||(group>=41800&&group<=41803)||group==42010||(group>=42036&&group<=42046))return 8;
    return 0;
}
struct NativePreviewTraceSample {
    std::uint64_t tick=0;
    std::uint32_t stage=0,path_mask=0,stack_depth=0,game_depth=0,elapsed_us=0;
    std::array<std::uint32_t,6> groups{};
};
struct NativePreviewTraceTicket {
    std::uint64_t sequence=0,buffer=0,base=0,size=0,tick=0;
    NativePreviewTraceStage stage=NativePreviewTraceStage::Bindings;
    explicit operator bool()const noexcept{return sequence!=0;}
};
template<class Mutex=std::mutex>class BasicNativePreviewTraceHistory {
    std::atomic<std::uint64_t> sequence_{0},base_{0},size_{0},buffer_{0},context_{0};
    std::array<std::atomic<std::uint64_t>,3> next_{};
    mutable Mutex mutex_;
    std::array<NativePreviewTraceSample,64> rows_{};
    std::size_t cursor_=0,count_=0;
    void source(std::uint64_t base,std::uint64_t size,std::uint64_t buffer,std::uint64_t context)noexcept {
        sequence_.fetch_add(1,std::memory_order_acq_rel);
        base_.store(base,std::memory_order_relaxed);size_.store(size,std::memory_order_relaxed);
        buffer_.store(buffer,std::memory_order_relaxed);context_.store(context,std::memory_order_relaxed);
        for(auto& next:next_)next.store(0,std::memory_order_relaxed);
        sequence_.fetch_add(1,std::memory_order_release);
    }
public:
    bool matches(std::uint64_t buffer)const noexcept{return buffer&&buffer_.load(std::memory_order_acquire)==buffer;}
    void configure(std::uint64_t base,std::uint64_t size,std::uint64_t buffer,std::uint64_t context)noexcept {
        if(base<0x10000||base>=0x800000000000ULL||!size||size>0x800000000000ULL-base||!buffer||!context)base=size=buffer=context=0;
        if(base_.load(std::memory_order_acquire)==base&&size_.load()==size&&buffer_.load()==buffer&&context_.load()==context)return;
        try{std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);
            if(!lock.owns_lock()){buffer_.store(0,std::memory_order_release);return;}
            source(base,size,buffer,context);
        }catch(...){buffer_.store(0,std::memory_order_release);}
    }
    void invalidate(std::uint64_t buffer)noexcept {
        if(buffer)buffer_.compare_exchange_strong(buffer,0,std::memory_order_acq_rel);
    }
    NativePreviewTraceTicket ticket(std::uint64_t buffer,NativePreviewTraceStage stage,std::uint64_t now)noexcept {
        const auto ordinal=unsigned(stage);if(!buffer||ordinal<1||ordinal>3||now>std::numeric_limits<std::uint64_t>::max()-2000)return {};
        const auto sequence=sequence_.load(std::memory_order_acquire);if(!sequence||(sequence&1)||buffer_.load(std::memory_order_relaxed)!=buffer)return {};
        NativePreviewTraceTicket result{sequence,buffer,base_.load(std::memory_order_relaxed),size_.load(std::memory_order_relaxed),now,stage};
        if(!result.base||!result.size||sequence_.load(std::memory_order_acquire)!=sequence)return {};
        auto& next=next_[ordinal-1];auto expected=next.load(std::memory_order_relaxed);
        if(expected&&now<expected)return {};
        if(!next.compare_exchange_strong(expected,now+2000,std::memory_order_relaxed))return {};
        return sequence_.load(std::memory_order_acquire)==sequence?result:NativePreviewTraceTicket{};
    }
    bool record(const NativePreviewTraceTicket& ticket,NativePreviewTraceSample sample)noexcept {
        if(!ticket||ticket.sequence!=sequence_.load(std::memory_order_acquire)||unsigned(ticket.stage)<1||unsigned(ticket.stage)>3||
           sample.path_mask>15||sample.stack_depth>64||sample.game_depth>sample.groups.size()||sample.game_depth>sample.stack_depth||sample.elapsed_us>1000000)return false;
        for(auto group:sample.groups)if(group>native_preview_code_group_count)return false;
        try{std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock()||ticket.sequence!=sequence_.load()||ticket.buffer!=buffer_.load())return false;
            sample.tick=ticket.tick;sample.stage=unsigned(ticket.stage);rows_[cursor_]=sample;cursor_=(cursor_+1)%rows_.size();count_=std::min(count_+1,rows_.size());return true;
        }catch(...){return false;}
    }
    // Called by the report worker. Capture callbacks never format text, allocate
    // a report, write files or wait for this reader's lock.
    std::string report(std::uint64_t now)const noexcept {
        try{std::array<NativePreviewTraceSample,64> rows;std::size_t count=0,cursor=0;
            {std::lock_guard<Mutex> lock(mutex_);rows=rows_;count=count_;cursor=cursor_;}
            std::string result;result.reserve(count*192);const auto first=(cursor+rows.size()-count)%rows.size();
            for(std::size_t i=0;i<count;++i){const auto& row=rows[(first+i)%rows.size()];if(row.tick>now||now-row.tick>120000)continue;
                result+="preview-ownership.sample stage="+std::to_string(row.stage)+" path-mask="+std::to_string(row.path_mask)+" stack-depth="+std::to_string(row.stack_depth)+" game-depth="+std::to_string(row.game_depth);
                for(std::size_t j=0;j<row.groups.size();++j)result+=" group-"+std::to_string(j)+"="+std::to_string(row.groups[j]);
                result+=" elapsed-us="+std::to_string(row.elapsed_us)+" age-ms="+std::to_string(now-row.tick)+'\n';}
            return result;
        }catch(...){return {};}
    }
};
using NativePreviewTraceHistory=BasicNativePreviewTraceHistory<>;
inline NativePreviewTraceHistory& native_preview_trace_history(){static NativePreviewTraceHistory value;return value;}
inline void native_preview_trace_source(const NativeOwnerImage& image,std::uint64_t buffer,std::uint64_t context)noexcept {
    if(image.size!=native_owner_profile::image_size||image.timestamp!=native_owner_profile::image_timestamp||
       !std::equal(image.sha256.begin(),image.sha256.end(),std::begin(native_owner_profile::image_sha256)))return;
    native_preview_trace_history().configure(image.base,image.size,buffer,context);
}
template<class Trace,class Handoff>inline void retire_native_preview_trace(Trace& trace,Handoff& handoff,std::uint64_t buffer)noexcept{
    trace.invalidate(buffer);handoff.retire(buffer);
}
inline void native_preview_trace_retired(std::uint64_t buffer)noexcept{
    retire_native_preview_trace(native_preview_trace_history(),native_preview_handoff_history(),buffer);
}
inline std::string native_preview_trace_report(std::uint64_t tick)noexcept{return native_preview_trace_history().report(tick);}
inline void native_preview_trace_event(std::uint64_t buffer,NativePreviewTraceStage stage)noexcept {
#ifdef _WIN32
    auto& history=native_preview_trace_history();if(!history.matches(buffer))return;
    const auto ticket=history.ticket(buffer,stage,GetTickCount64());if(!ticket)return;
    native_preview_handoff_gpu(buffer,stage!=NativePreviewTraceStage::Bindings,ticket.tick);
    const auto started=std::chrono::steady_clock::now();void* frames[48]{};const auto count=CaptureStackBackTrace(0,48,frames,nullptr);
    NativePreviewTraceSample sample;sample.stack_depth=count;
    constexpr std::uint64_t table_rva=0x285b000,table_bytes=std::uint64_t(native_preview_code_group_count)*sizeof(RUNTIME_FUNCTION);
    static_assert(sizeof(RUNTIME_FUNCTION)==12,"supported Windows code-range metadata");
    for(unsigned i=0;i<count;++i){const auto pc=reinterpret_cast<std::uintptr_t>(frames[i]);if(pc<ticket.base||pc-ticket.base>=ticket.size)continue;
        DWORD64 image_base=0;const auto* entry=RtlLookupFunctionEntry(pc,&image_base,nullptr);if(!entry||image_base!=ticket.base)continue;
        const auto address=reinterpret_cast<std::uintptr_t>(entry),table=ticket.base+table_rva;
        if(address<table||address-table>=table_bytes||(address-table)%sizeof(RUNTIME_FUNCTION))continue;
        const auto group=std::uint32_t((address-table)/sizeof(RUNTIME_FUNCTION)+1);sample.path_mask|=native_preview_trace_path(group);
        if(sample.game_depth<sample.groups.size())sample.groups[sample.game_depth++]=group;
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
    sample.elapsed_us=static_cast<std::uint32_t>(std::max<std::int64_t>(0,std::min<std::int64_t>(elapsed,1000000)));history.record(ticket,sample);
#else
    (void)buffer;(void)stage;
#endif
}
}
