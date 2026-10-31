#include "mod_input_state.hpp"
#include <cmath>
#include <algorithm>
namespace evrm {
bool ModInputState::stale(std::uint32_t time)const noexcept{return has_cutoff_&&std::int32_t(time-cutoff_)<=0;}
bool ModInputState::push(ModInputEvent event,std::uint32_t time)noexcept {
    if(queue_.count==queue_.events.size()){
        open_=false;latched_keys_|=physical_keys_;latched_buttons_|=physical_buttons_;
        forwarded_keys_.reset();forwarded_buttons_.reset();cutoff_=time;has_cutoff_=true;
        queue_.count=1;queue_.events[0]={ModInputEventKind::Reset};return false;
    }
    queue_.events[queue_.count++]=event;return true;
}
ModInputTransition ModInputState::show(std::uint32_t)noexcept {
    ModInputTransition result;result.opened=true;result.suppress=true;open_=true;
    result.release_keys=forwarded_keys_;result.release_buttons=forwarded_buttons_;
    forwarded_keys_.reset();forwarded_buttons_.reset();
    queue_.count=1;queue_.events[0]={ModInputEventKind::Reset};return result;
}
ModInputTransition ModInputState::close(std::uint32_t time)noexcept {
    ModInputTransition result;result.closed=open_;result.suppress=open_;open_=false;
    latched_keys_|=physical_keys_;latched_buttons_|=physical_buttons_;
    cutoff_=time;has_cutoff_=true;queue_.count=1;queue_.events[0]={ModInputEventKind::Reset};return result;
}
ModInputTransition ModInputState::focus_lost(std::uint32_t time)noexcept {
    auto result=close(time);result.release_keys=forwarded_keys_;result.release_buttons=forwarded_buttons_;
    forwarded_keys_.reset();forwarded_buttons_.reset();physical_keys_.reset();physical_buttons_.reset();return result;
}
ModInputTransition ModInputState::key(unsigned code,bool down,std::uint32_t time,unsigned hotkey)noexcept {
    ModInputTransition result;if(code>=256)return result;
    const bool was=physical_keys_[code];physical_keys_[code]=down;
    const bool old=stale(time);const bool latched=latched_keys_[code];
    result.suppress=open_||old||latched;
    if(old&&down)latched_keys_.set(code);
    if(!old&&!latched&&code==hotkey&&down&&!was){
        result=open_?close(time):show(time);latched_keys_.set(code);return result;
    }
    if(!old&&!latched&&open_&&code==27&&down&&!was){result=close(time);latched_keys_.set(code);return result;}
    if(!down)latched_keys_.reset(code);
    if(open_&&down!=was&&!push({ModInputEventKind::Key,std::uint16_t(code),down},time)){result.closed=true;result.suppress=true;}
    if(!result.suppress)forwarded_keys_[code]=down;
    return result;
}
ModInputTransition ModInputState::button(unsigned code,bool down,std::uint32_t time,float x,float y)noexcept {
    ModInputTransition result;if(code>=5)return result;
    const bool was=physical_buttons_[code];physical_buttons_[code]=down;
    const auto old=stale(time);result.suppress=open_||old||latched_buttons_[code];
    if(old&&down)latched_buttons_.set(code);
    if(!down)latched_buttons_.reset(code);
    const bool position=std::isfinite(x)&&std::isfinite(y);
    if(open_&&down!=was&&!push({ModInputEventKind::Button,std::uint16_t(code),down,position?x:0,position?y:0,position},time)){result.closed=true;result.suppress=true;}
    if(!result.suppress)forwarded_buttons_[code]=down;
    return result;
}
ModInputTransition ModInputState::wheel(float horizontal,float vertical,std::uint32_t time)noexcept {
    ModInputTransition result;result.suppress=open_||stale(time);
    if(open_&&std::isfinite(horizontal)&&std::isfinite(vertical)&&
        !push({ModInputEventKind::Wheel,0,false,std::clamp(horizontal,-16.f,16.f),std::clamp(vertical,-16.f,16.f)},time))result.closed=true;
    return result;
}
ModInputTransition ModInputState::motion(std::uint32_t time)const noexcept {ModInputTransition result;result.suppress=open_||stale(time);return result;}
ModInputBatch ModInputState::drain()noexcept {auto result=queue_;queue_.count=0;return result;}
}
