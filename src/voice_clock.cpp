#include "voice_clock.hpp"
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
std::uint64_t scale_counter(std::uint64_t value,std::uint64_t scale,std::uint64_t frequency)noexcept {
    if(!frequency)return 0;
    // The supported native toolchain is GCC x64; wide integer arithmetic keeps
    // long-running timestamps exact without overflowing an intermediate product.
    const auto result=static_cast<unsigned __int128>(value)*scale/frequency;
    return result>std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(result);
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
