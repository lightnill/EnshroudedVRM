#pragma once
#include <cstdint>
namespace evrm {
std::uint64_t counter_duration_ns(std::uint64_t start,std::uint64_t end,std::uint64_t frequency)noexcept;
class VoiceClock {
public:
    VoiceClock()noexcept;
    bool valid()const noexcept{return frequency_!=0;}
    std::uint64_t frequency()const noexcept{return frequency_;}
    std::uint64_t ticks()const noexcept;
    std::uint64_t now_us()const noexcept;
private:
    std::uint64_t frequency_=0;
};
}
