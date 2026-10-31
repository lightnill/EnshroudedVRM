#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace evrm {
struct MicrophonePacket {
    const float* samples=nullptr;
    std::size_t frames=0;
    std::uint64_t timestamp_us=0; // First sample, in QPC microseconds.
    bool silent=false,discontinuity=false,timestamp_uncertain=false;
};
struct MicrophoneAudioSink {
    void* context=nullptr;
    void (*reset)(void*)noexcept=nullptr;
    void (*block)(void*,const std::array<float,480>&,std::uint64_t)noexcept=nullptr;
};
class MicrophoneAudioAssembler {
public:
    void clear()noexcept{count_=0;last_end_=last_emitted_=0;}
    std::uint64_t discarded_blocks()const noexcept{return discarded_;}
    void push(const MicrophonePacket& packet,std::uint64_t now,const MicrophoneAudioSink& sink)noexcept {
        const auto reset=[&]{clear();if(sink.reset)sink.reset(sink.context);};
        if(!packet.frames)return;
        if(!now||!packet.timestamp_us||packet.frames>48000||packet.timestamp_us>UINT64_MAX-1000000||
            (!packet.silent&&!packet.samples)){reset();return;}
        if(!packet.silent)for(std::size_t n=0;n<packet.frames;++n)
            if(!std::isfinite(packet.samples[n])||std::abs(packet.samples[n])>16){reset();return;}
        if(packet.discontinuity||(last_end_&&!packet.timestamp_uncertain&&
            (packet.timestamp_us>last_end_+2000||last_end_>packet.timestamp_us+2000)))reset();
        bool dropped=false;
        for(std::size_t n=0;n<packet.frames;++n){
            pending_[count_++]=packet.silent?0:packet.samples[n];
            if(count_!=pending_.size())continue;
            count_=0;auto timestamp=packet.timestamp_us+(n+1)*1000000/48000;
            if(packet.timestamp_uncertain&&last_emitted_&&timestamp<=last_emitted_)timestamp=last_emitted_+1;
            if(timestamp>now||now-timestamp>40000){++discarded_;dropped=true;continue;}
            if(dropped){if(sink.reset)sink.reset(sink.context);dropped=false;}
            if(sink.block)sink.block(sink.context,pending_,timestamp);
            last_emitted_=timestamp;
        }
        last_end_=packet.timestamp_us+packet.frames*1000000/48000;
        if(dropped)reset();
    }
private:
    std::array<float,480> pending_{};
    std::size_t count_=0;
    std::uint64_t last_end_=0,last_emitted_=0,discarded_=0;
};
}
