#pragma once
#include <vulkan/vulkan.h>
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
}
