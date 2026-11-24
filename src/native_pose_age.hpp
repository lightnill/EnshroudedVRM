#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
namespace evrm {
// Diagnostic only: never participates in freshness or ownership decisions.
class NativePoseAgeHistogram {
public:
    struct Window {
        std::array<std::uint64_t,501> ages{};
        std::uint64_t samples=0,invalid=0,dropped_old=0,sum=0,minimum=0,maximum=0,first_tick=0,last_tick=0;
    };
    bool observe(std::uint64_t presentation,std::uint64_t captured,std::uint64_t now)noexcept{
        if(!presentation||presentation==presentation_)return false;
        if(presentation<presentation_){++window_.dropped_old;return false;}
        presentation_=presentation;
        if(!captured||now<captured||now-captured>500){++window_.invalid;return false;}
        const auto age=now-captured;
        if(!window_.samples){window_.minimum=age;window_.first_tick=now;}
        window_.minimum=std::min(window_.minimum,age);window_.maximum=std::max(window_.maximum,age);
        ++window_.ages[age];++window_.samples;window_.sum+=age;window_.last_tick=now;
        return window_.samples>=300;
    }
    const Window& window()const noexcept{return window_;}
    static std::uint64_t quantile(const Window& w,unsigned percent)noexcept{
        if(!w.samples)return 0;
        const auto target=std::max<std::uint64_t>(1,(w.samples*std::min(percent,100u)+99)/100);
        std::uint64_t total=0;
        for(std::size_t n=0;n<w.ages.size();++n){total+=w.ages[n];if(total>=target)return n;}
        return 500;
    }
    void clear()noexcept{window_={};}
private:
    Window window_{};
    std::uint64_t presentation_=0;
};
}
