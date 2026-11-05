#include "voice_clock.hpp"
#include <limits>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif
namespace evrm {
namespace {
std::uint64_t scale_counter(std::uint64_t value,std::uint64_t scale,std::uint64_t frequency)noexcept {
    if(!frequency)return 0;
    // Keep the complete product: long-running counters can overflow 64 bits
    // before division even when the final duration is representable.
#if defined(_MSC_VER) && defined(_M_X64)
    std::uint64_t high=0;
    const auto low=_umul128(value,scale,&high);
    // _udiv128 requires a representable quotient. This comparison detects
    // overflow before issuing the division, preserving the saturating API.
    if(high>=frequency)return std::numeric_limits<std::uint64_t>::max();
    std::uint64_t remainder=0;
    return _udiv128(high,low,frequency,&remainder);
#else
    const auto result=static_cast<unsigned __int128>(value)*scale/frequency;
    return result>std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(result);
#endif
}
}
std::uint64_t counter_duration_ns(std::uint64_t start,std::uint64_t end,std::uint64_t frequency)noexcept {
    return end<start?0:scale_counter(end-start,1000000000,frequency);
}
VoiceClock::VoiceClock()noexcept {
    LARGE_INTEGER frequency{};
    if(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0)frequency_=static_cast<std::uint64_t>(frequency.QuadPart);
}
std::uint64_t VoiceClock::ticks()const noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value)&&value.QuadPart>=0?static_cast<std::uint64_t>(value.QuadPart):0;
}
std::uint64_t VoiceClock::now_us()const noexcept{return scale_counter(ticks(),1000000,frequency_);}
}
