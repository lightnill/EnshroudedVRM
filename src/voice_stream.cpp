#include "voice_stream.hpp"
namespace evrm {
VoiceBlockQueue::VoiceBlockQueue()noexcept {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"Audio queue atomics must not lock");
    static_assert((capacity&(capacity-1))==0,"Audio queue uses a power-of-two capacity");
    for(std::size_t n=0;n<capacity;++n)slots_[n].sequence.store(n,std::memory_order_relaxed);
}
bool VoiceBlockQueue::try_push(const VoicePcmBlock& block)noexcept {
    // Contention or a full queue drops this block; the game's callback never
    // spins waiting for a consumer or another producer to complete.
    for(unsigned attempt=0;attempt<3;++attempt){
        auto position=head_.load(std::memory_order_relaxed);
        auto& slot=slots_[position&(capacity-1)];
        if(slot.sequence.load(std::memory_order_acquire)!=position)continue;
        if(!head_.compare_exchange_strong(position,position+1,std::memory_order_relaxed))continue;
        slot.block=block;
        slot.sequence.store(position+1,std::memory_order_release);
        return true;
    }
    dropped_.fetch_add(1,std::memory_order_relaxed);return false;
}
bool VoiceBlockQueue::try_pop(VoicePcmBlock& block)noexcept {
    auto& slot=slots_[tail_&(capacity-1)];
    if(slot.sequence.load(std::memory_order_acquire)!=tail_+1)return false;
    block=slot.block;
    slot.sequence.store(tail_+capacity,std::memory_order_release);
    ++tail_;return true;
}
void MouthSnapshotExchange::publish(const MouthSnapshot& value)noexcept {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"Mouth snapshots must not lock");
    const auto weights=vrm::bounded_mouth_weights(value.weights);
    auto activity=static_cast<unsigned>(value.activity);
    if(activity>static_cast<unsigned>(VoiceActivity::UnsupportedSource))activity=static_cast<unsigned>(VoiceActivity::UnsupportedSource);
    std::uint64_t packed=std::uint64_t(activity)<<60;unsigned remaining=4095;
    for(unsigned n=0;n<5;++n){
        const auto quantized=std::min(remaining,static_cast<unsigned>(double(weights[n])*4095));
        remaining-=quantized;packed|=std::uint64_t(quantized)<<(n*12);
    }
    // SC atomics give one total order for version and payload. Hardware-atomic
    // payloads avoid an undefined struct data race even when a reader retries.
    sequence_.fetch_add(1);
    packed_.store(packed);epoch_.store(value.epoch);timestamp_.store(value.timestamp_us);
    sequence_.fetch_add(1);
}
bool MouthSnapshotExchange::read(MouthSnapshot& value)const noexcept {
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto before=sequence_.load();if(before&1)continue;
        const auto packed=packed_.load(),epoch=epoch_.load(),timestamp=timestamp_.load();
        if(sequence_.load()!=before)continue;
        MouthSnapshot result;result.epoch=epoch;result.timestamp_us=timestamp;
        result.activity=static_cast<VoiceActivity>(packed>>60);
        for(unsigned n=0;n<5;++n)result.weights[n]=float((packed>>(n*12))&4095)/4095.f;
        value=result;return true;
    }
    return false;
}
}
