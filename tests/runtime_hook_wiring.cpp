// Actual runtime helper bodies, fake external callbacks, no device or driver.
#include "../src/runtime_hook_timing.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
#include <stdexcept>
#include <string>
#include <iostream>

namespace evrm {
unsigned messages=0;
void log_event(const std::string&){++messages;}
struct GpuProbe {unsigned calls=0;};
struct GraphicsState {unsigned calls=0;};
struct CommandTraceRuntime {unsigned calls=0;};
struct NativeState {unsigned calls=0;};
struct NativeBackend {NativeState value;NativeState& state(){return value;}};
}
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct DeviceState {
    VkDevice handle=reinterpret_cast<VkDevice>(0x100);
    PFN_vkGetDeviceProcAddr get_proc=nullptr;
    evrm::RuntimeHookTiming hook_timing;
    std::shared_ptr<evrm::GpuProbe> probe=std::make_shared<evrm::GpuProbe>();
    std::shared_ptr<evrm::GraphicsState> graphics=std::make_shared<evrm::GraphicsState>();
    std::shared_ptr<evrm::CommandTraceRuntime> trace=std::make_shared<evrm::CommandTraceRuntime>();
    std::shared_ptr<evrm::NativeBackend> native=std::make_shared<evrm::NativeBackend>();
};
std::mutex state_mutex;
std::unordered_map<void*,std::shared_ptr<DeviceState>> devices;
template<class Handle>void* dispatch_key(Handle handle){return handle?*reinterpret_cast<void**>(handle):nullptr;}
unsigned resolutions=0,invocations=0;
void VKAPI_CALL forwarded(){++invocations;}
PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device,const char* name){
    need(device==reinterpret_cast<VkDevice>(0x100)&&std::string(name)=="fake-call","resolver arguments changed");
    ++resolutions;return forwarded;
}
#include "runtime_hook_helpers.inc"
}
int main(){try{
    struct Command {void* dispatch=reinterpret_cast<void*>(0x500);}command;
    auto original=std::make_shared<DeviceState>();original->get_proc=resolve;devices[command.dispatch]=original;
    auto selected=device_state(&command);need(selected==original,"device dispatch lookup changed");
    need(!device_state(static_cast<Command*>(nullptr)),"missing device gained a state");
    auto next=device_proc<PFN_vkVoidFunction>(selected,"fake-call");need(next==forwarded,"resolved callback changed");next();
    need(resolutions==1&&invocations==1,"timing added, skipped or repeated a forwarded call");
    need(!device_proc<PFN_vkVoidFunction>({},"fake-call"),"missing device resolved a function");
    observe(selected,[&](auto& value){++value.calls;});
    observe_graphics(selected,[&](auto& value){++value.calls;});
    observe_trace(selected,[&](auto& value){++value.calls;});
    observe_native(selected,[&](auto& value){++value.calls;});
    need(selected->probe->calls==1&&selected->graphics->calls==1&&selected->trace->calls==1&&selected->native->value.calls==1,"observer callback count changed");
    observe(selected,[&](auto&){throw std::runtime_error("expected probe fault");});
    observe_graphics(selected,[&](auto&){throw std::runtime_error("expected graphics fault");});
    observe_trace(selected,[&](auto&){throw std::runtime_error("expected trace fault");});
    observe_native(selected,[&](auto&){throw std::runtime_error("expected native fault");});
    need(evrm::messages==4,"timing changed observer exception handling");
    const auto result=timed_native(selected,evrm::RuntimeHookKind::NativeSubmit,[&](auto& value){++value.value.calls;return -7;});
    need(result==-7&&selected->native->value.calls==2,"timed native work changed a return value or repeated the callback");
    bool propagated=false;try{timed_native(selected,evrm::RuntimeHookKind::NativeSubmit,[&](auto&){throw std::runtime_error("native exception");});}catch(const std::runtime_error&){propagated=true;}
    need(propagated,"timing swallowed a native callback exception");
    selected->probe.reset();observe(selected,[&](auto&){throw std::runtime_error("absent probe callback invoked");});
    devices.clear();original.reset();
    const auto rows=selected->hook_timing.take();
    need(rows[static_cast<unsigned>(evrm::RuntimeHookKind::StateLookup)].samples==1&&
         rows[static_cast<unsigned>(evrm::RuntimeHookKind::ProcLookup)].samples==1&&
         rows[static_cast<unsigned>(evrm::RuntimeHookKind::NativeSubmit)].samples==2,"actual runtime helper timings missing or state lease lost");
    std::cout<<"PASS actual hook helpers: dispatch, callbacks, return values, exceptions and device lifetime unchanged\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
