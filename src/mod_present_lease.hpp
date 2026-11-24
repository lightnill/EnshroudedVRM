#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
namespace evrm {
enum class ModPresentAction {Original,Replace,Stop};
class ModPresentLease {
public:
    void acquired(VkResult result)noexcept {if(!retired_&&(result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR))acquired_=true;}
    bool can_draw(bool frame_complete)const noexcept {return !retired_&&acquired_&&state_==State::Available&&frame_complete;}
    ModPresentAction submit_result(VkResult result)noexcept {
        if(result==VK_SUCCESS){state_=State::Submitted;return ModPresentAction::Replace;}
        if(result==VK_ERROR_OUT_OF_HOST_MEMORY||result==VK_ERROR_OUT_OF_DEVICE_MEMORY)return ModPresentAction::Original;
        state_=result==VK_ERROR_DEVICE_LOST?State::Lost:State::Uncertain;retired_=true;return ModPresentAction::Stop;
    }
    void present_result(VkResult result)noexcept {
        if(state_!=State::Submitted)return;
        if(result==VK_ERROR_OUT_OF_HOST_MEMORY||result==VK_ERROR_OUT_OF_DEVICE_MEMORY)return;
        acquired_=false;
        if(result==VK_ERROR_DEVICE_LOST){state_=State::Lost;retired_=true;return;}
        if(result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR||result==VK_ERROR_OUT_OF_DATE_KHR||
           result==VK_ERROR_SURFACE_LOST_KHR||result==VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT||result==VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT)
            state_=State::PresentPending;
        else{state_=State::Uncertain;retired_=true;}
    }
    void fence_complete()noexcept {if(state_==State::PresentPending)state_=State::Available;}
    void drained()noexcept {if(state_==State::Submitted)state_=State::Available;}
    // Draw-fence route only: the menu's submission is known finished and nothing
    // of the menu's goes to the presentation engine, so no present is awaited.
    // The image is not drawn on again before its next acquisition.
    void draw_complete()noexcept {if(state_==State::Submitted){state_=State::Available;acquired_=false;}}
    void original_presented()noexcept {acquired_=false;}
    void device_lost()noexcept {state_=State::Lost;retired_=true;}
    void uncertain()noexcept {state_=State::Uncertain;retired_=true;}
    void retire()noexcept {retired_=true;}
    bool retry_present()const noexcept {return state_==State::Submitted;}
    bool waiting_present()const noexcept {return state_==State::PresentPending;}
    bool safe_to_release()const noexcept {return state_==State::Available||state_==State::Lost;}
private:
    enum class State {Available,Submitted,PresentPending,Lost,Uncertain};
    State state_=State::Available;bool acquired_=false,retired_=false;
};
// How long an open menu may hold one present for a completion that is a moment
// late, instead of presenting that frame without the panel. Bookkeeping only:
// the caller performs the wait with this budget. A wait that runs out is never
// taken for completion; the frame is then presented without the panel, as it
// always was. Nothing here depends on a driver or a vendor: a driver whose
// completions arrive later than the budget costs a bounded stall, after which
// waiting rests for a growing number of frames and the old behaviour applies.
class ModPresentWait {
public:
    static constexpr std::uint64_t frame_ns=20'000'000;   // all waits of one present together
    static constexpr unsigned patience=2;                 // waits in a row that may run out
    static constexpr unsigned first_rest=240,longest_rest=15360;
    // One present with the menu open begins.
    void frame()noexcept {
        if(resting_){--resting_;left_=0;}else left_=frame_ns;
    }
    // Nanoseconds the next wait of this present may take; zero means do not wait.
    std::uint64_t budget()const noexcept {return left_;}
    bool resting()const noexcept {return resting_!=0;}
    // A wait returned after `spent` nanoseconds, completed or run out.
    void waited(bool completed,std::uint64_t spent)noexcept {
        if(completed){left_=spent<left_?left_-spent:0;misses_=0;next_rest_=first_rest;return;}
        left_=0;
        if(++misses_<patience)return;
        misses_=0;resting_=next_rest_;next_rest_=next_rest_<longest_rest/2?next_rest_*2:longest_rest;
    }
private:
    std::uint64_t left_=0;unsigned misses_=0,resting_=0,next_rest_=first_rest;
};
// What happened to the frames presented while the menu was open. Counts and
// durations only; written to the log when the menu closes and every few
// seconds while it stays open.
struct ModPresentCounts {
    std::uint64_t frames=0,drawn=0,image_waits=0,image_timeouts=0,slot_waits=0,slot_timeouts=0;
    std::uint64_t skipped_image=0,skipped_slot=0,skipped_other=0,rests=0,wait_ns=0,longest_wait_ns=0;
    void waited(bool image,bool completed,std::uint64_t spent)noexcept {
        ++(image?image_waits:slot_waits);if(!completed)++(image?image_timeouts:slot_timeouts);
        wait_ns+=spent;if(spent>longest_wait_ns)longest_wait_ns=spent;
    }
    std::string line()const {
        const auto n=[](std::uint64_t value){return std::to_string(value);};
        return "mod-menu.presentation frames="+n(frames)+" drawn="+n(drawn)+" image-waits="+n(image_waits)+" image-timeouts="+n(image_timeouts)+
            " slot-waits="+n(slot_waits)+" slot-timeouts="+n(slot_timeouts)+" skipped-image="+n(skipped_image)+" skipped-slot="+n(skipped_slot)+
            " skipped-other="+n(skipped_other)+" rests="+n(rests)+" wait-us="+n(wait_ns/1000)+" longest-wait-us="+n(longest_wait_ns/1000);
    }
};
}
