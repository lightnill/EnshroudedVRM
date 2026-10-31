#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <cstddef>
#include <limits>
namespace evrm {
enum class ModInputEventKind:std::uint8_t {Reset,Key,Button,Wheel};
struct ModInputEvent {ModInputEventKind kind=ModInputEventKind::Reset;std::uint16_t code=0;bool down=false;float x=0,y=0;bool positioned=false;};
struct ModInputBatch {std::array<ModInputEvent,256> events{};std::size_t count=0;};
struct ModInputTransition {
    bool suppress=false,opened=false,closed=false;
    std::bitset<256> release_keys;
    std::bitset<5> release_buttons;
};
// One window-thread owner. Its adapter copies/drains events under a short lock;
// no GUI or OS work runs in this deterministic ownership model.
class ModInputState {
public:
    bool open()const noexcept{return open_;}
    ModInputTransition key(unsigned,bool,std::uint32_t time,unsigned hotkey)noexcept;
    ModInputTransition button(unsigned,bool,std::uint32_t time,float x=std::numeric_limits<float>::quiet_NaN(),float y=std::numeric_limits<float>::quiet_NaN())noexcept;
    ModInputTransition wheel(float horizontal,float vertical,std::uint32_t time)noexcept;
    ModInputTransition motion(std::uint32_t time)const noexcept;
    ModInputTransition close(std::uint32_t time)noexcept;
    ModInputTransition focus_lost(std::uint32_t time)noexcept;
    ModInputBatch drain()noexcept;
private:
    bool stale(std::uint32_t time)const noexcept;
    bool push(ModInputEvent,std::uint32_t time)noexcept;
    ModInputTransition show(std::uint32_t time)noexcept;
    std::bitset<256> physical_keys_,forwarded_keys_,latched_keys_;
    std::bitset<5> physical_buttons_,forwarded_buttons_,latched_buttons_;
    ModInputBatch queue_;
    std::uint32_t cutoff_=0;
    bool open_=false,has_cutoff_=false;
};
}
