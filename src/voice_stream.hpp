#pragma once
#include "mouth_types.hpp"
#include <atomic>

namespace evrm {
struct VoicePcmBlock {
    std::array<float,480> samples{};
    std::uint64_t epoch=0,sequence=0,timestamp_us=0;
};
// Multiple bounded producers, exactly one consumer. No sample pointer is kept.
class VoiceBlockQueue {
public:
    static constexpr std::size_t capacity=16;
    VoiceBlockQueue()noexcept;
    bool try_push(const VoicePcmBlock&)noexcept;
    bool try_pop(VoicePcmBlock&)noexcept;
    std::uint64_t dropped()const noexcept{return dropped_.load(std::memory_order_relaxed);}
private:
    struct Slot {std::atomic<std::uint64_t> sequence{0};VoicePcmBlock block;};
    std::array<Slot,capacity> slots_;
    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::uint64_t tail_=0;
    std::atomic<std::uint64_t> dropped_{0};
};
enum class VoiceActivity:std::uint8_t {Off,Waiting,Talking,Quiet,UnsupportedSource};
struct MouthSnapshot {
    vrm::MouthWeights weights{};
    std::uint64_t epoch=0,timestamp_us=0;
    VoiceActivity activity=VoiceActivity::Off;
};
// One worker writer; any render readers. Every payload field is atomic, avoiding
// the C++ data race of a conventional seqlock over ordinary struct memory.
class MouthSnapshotExchange {
public:
    void publish(const MouthSnapshot&)noexcept;
    bool read(MouthSnapshot&)const noexcept;
private:
    std::atomic<std::uint64_t> sequence_{0},packed_{0},epoch_{0},timestamp_{0};
};
}
