// Execute the actual destruction hook with an in-progress CPU source observation.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "../src/native_preview_trace.hpp"
#include "../src/native_preview_subject.hpp"
#include <memory>
#include <mutex>
#include <cstdio>
#include <stdexcept>
template<class T>T handle(std::uintptr_t value){return reinterpret_cast<T>(value);}
static unsigned forwarded=0,metadata_retired=0;static bool late_registration=true;
struct Memory {void buffer_destroyed(VkBuffer){}};
struct NativeState {void buffer_destroyed(VkBuffer){++metadata_retired;}};
struct Native {
    std::mutex mutex;Memory memory;
    std::unique_lock<std::mutex> native_source_guard(){
        // An existing observation completes immediately before the destroyer
        // acquires its source lock. This must not undo final retirement.
        if(late_registration){evrm::native_preview_trace_history().configure(0x140000000ULL,44298240,30,99);late_registration=false;}
        return std::unique_lock<std::mutex>(mutex);
    }
    Memory* buffer_memory(){return &memory;}Memory* image_memory(){return &memory;}
};
namespace evrm {using NativeState=::NativeState;struct GpuProbe {void buffer_destroyed(VkBuffer buffer){native_preview_trace_retired(reinterpret_cast<std::uintptr_t>(buffer));}};struct CommandTraceRuntime {void buffer_destroyed(VkBuffer){}};}
struct State {std::shared_ptr<Native> native=std::make_shared<Native>();NativeState metadata;evrm::GpuProbe probe;};
static auto state=std::make_shared<State>();
static void VKAPI_CALL original(VkDevice,VkBuffer,const VkAllocationCallbacks*){if(metadata_retired!=1)throw std::runtime_error("native metadata retired after physical destruction");++forwarded;}
std::shared_ptr<State> device_state(VkDevice){return state;}
template<class T>T device_proc(const std::shared_ptr<State>&,const char*){return reinterpret_cast<T>(&original);}
template<class F>void observe(const std::shared_ptr<State>& value,F&& f){f(value->probe);}
template<class F>void observe_native(const std::shared_ptr<State>& value,F&& f){f(value->metadata);}
template<class F>void observe_trace(const std::shared_ptr<State>&,F&&){}
#define EVRM_EXPORT
#include "actual_destroy.inc"
int main(){try{
    auto& trace=evrm::native_preview_trace_history();trace.configure(0x140000000ULL,44298240,30,99);
    vkDestroyBuffer(handle<VkDevice>(1),handle<VkBuffer>(30),nullptr);
    if(forwarded!=1||metadata_retired!=1)throw std::runtime_error("original destruction/metadata forwarding changed");
    if(trace.ticket(30,evrm::NativePreviewTraceStage::Bindings,1000))throw std::runtime_error("retirement was undone by a delayed source registration; reused handle remains traceable");
    trace.configure(0x140000000ULL,44298240,30,100);
    if(!trace.ticket(30,evrm::NativePreviewTraceStage::Bindings,2000))throw std::runtime_error("freshly validated replacement allocation could not be traced");
    std::puts("PASS actual destruction: delayed registration cannot rearm retired/reused trace source");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL preview retirement: %s\n",e.what());return 1;}}
