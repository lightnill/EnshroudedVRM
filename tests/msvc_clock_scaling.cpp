#include "../src/voice_clock.hpp"
#include <cstdint>
#include <iostream>

int main() {
    evrm::VoiceClock clock;
    if(!clock.valid()||!clock.frequency())return 2;
    const auto before=clock.now_us();
    if(clock.now_us()<before)return 3;
    std::uint64_t start=0,end=0,frequency=0;
    while(std::cin>>start>>end>>frequency)
        std::cout<<evrm::counter_duration_ns(start,end,frequency)<<'\n';
    return std::cin.eof()?0:4;
}
