#pragma once
#include "voice_clock.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace evrm {
struct VoiceSourceIdentity {
    std::array<std::uint8_t,32> file_sha256{};
    std::uint32_t timestamp=0,image_bytes=0;
    std::array<std::uint8_t,16> encoder_prologue{};
    std::array<std::uint8_t,5> encoder_call{};
};
bool local_voice_identity_supported(const VoiceSourceIdentity&)noexcept;
struct VoiceFloatSpan { const float* data=nullptr; std::uint64_t count=0; };
struct VoiceByteSpan { std::uint8_t* data=nullptr; std::uint64_t capacity=0; };
static_assert(sizeof(VoiceFloatSpan)==16&&sizeof(VoiceByteSpan)==16,"Native voice span ABI");
struct VoiceFrameSummary {
    bool valid=false,nonzero=false;
    float peak=0;
    double mean_square=0;
};
struct VoiceObservationSnapshot {
    std::uint64_t calls=0,accepted=0,nonzero=0,rejected=0,encode_failures=0,inspection_nanoseconds=0;
    std::uint64_t profiled_calls=0,sampled_qpc_nanoseconds=0,sampled_thread_cycles=0;
    std::uint64_t sampled_validation_ns=0,sampled_copy_ns=0,sampled_queries=0,sampled_copies=0,sampled_pre_ns=0,sampled_post_ns=0;
    std::uint64_t borrowed_calls=0;
};
struct VoiceFrameSink {
    void* context=nullptr;
    // Admission occurs before reading PCM and may be followed by invalid input
    // or unsuccessful encoding. Only accept delivers validated sample data.
    std::uint64_t (*begin)(void*,std::uint64_t source_id,std::uint64_t timestamp_us)noexcept=nullptr;
    void (*accept)(void*,const std::array<float,480>&,std::uint64_t source_id,std::uint64_t timestamp_us,std::uint64_t token)noexcept=nullptr;
};
using VoiceEncoderFunction=std::int64_t (*)(void*,VoiceByteSpan*,const VoiceFloatSpan*);
enum class VoiceReadContract:std::uint8_t {DefensiveBeforeEncode,BorrowedThroughReturn,UnrecognizedCaller};
class VoiceObservation {
public:
    VoiceObservationSnapshot snapshot()const noexcept;
private:
    std::atomic<std::uint64_t> calls_{0},accepted_{0},nonzero_{0},rejected_{0},encode_failures_{0},inspection_nanoseconds_{0};
    std::atomic<std::uint64_t> profiled_calls_{0},sampled_qpc_nanoseconds_{0},sampled_thread_cycles_{0};
    std::atomic<std::uint64_t> validation_ns_{0},copy_ns_{0},query_count_{0},copy_count_{0},pre_ns_{0},post_ns_{0};
    std::atomic<std::uint64_t> borrowed_calls_{0};
    VoiceClock clock_;
    friend std::int64_t observe_voice_encode(VoiceObservation&,void*,VoiceByteSpan*,const VoiceFloatSpan*,VoiceEncoderFunction,bool,const VoiceFrameSink*,VoiceReadContract)noexcept;
};
VoiceFrameSummary summarize_voice_frame(std::uint32_t,const float*,std::size_t)noexcept;
// Called only with the proven native ABI. No pointer or sample is retained.
std::int64_t observe_voice_encode(VoiceObservation&,void*,VoiceByteSpan*,const VoiceFloatSpan*,VoiceEncoderFunction,bool,const VoiceFrameSink* sink=nullptr,VoiceReadContract contract=VoiceReadContract::DefensiveBeforeEncode)noexcept;
}
