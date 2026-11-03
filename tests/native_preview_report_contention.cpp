#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "../src/native_preview_trace.hpp"
#include "../src/native_preview_subject.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

// The reporter pauses after acquiring the actual collector lock. Callback
// progress cannot depend on releasing it; this is not a probabilistic stress.
struct GateMutex {
    std::mutex value;
    static inline std::atomic<bool> arm{false},held{false},resume{false};
    void lock(){value.lock();if(arm.exchange(false)){held.store(true,std::memory_order_release);while(!resume.load(std::memory_order_acquire))std::this_thread::yield();}}
    bool try_lock(){return value.try_lock();}void unlock(){value.unlock();}
};
using Handoff=evrm::BasicNativePreviewHandoff<GateMutex>;
using Trace=evrm::BasicNativePreviewTraceHistory<GateMutex>;
static Handoff handoff;static Trace trace;
static unsigned forwarded=0,metadata_retired=0;
static void retire_test_sources(std::uint64_t buffer)noexcept{evrm::retire_native_preview_trace(trace,handoff,buffer);}
struct Memory {void buffer_destroyed(VkBuffer){}};
struct NativeState {void buffer_destroyed(VkBuffer){++metadata_retired;}};
struct Native {
    std::mutex mutex;Memory memory;
    std::unique_lock<std::mutex> native_source_guard(){
        auto lock=std::unique_lock<std::mutex>(mutex);
        // A CPU observer completed between early probe retirement and this
        // same source lock, republishing exactly the allocation being retired.
        evrm::native_preview_subjects().configure({{1,2,3,4,5,6,7,8,9,10},99,1,30,1,4,0});return lock;
    }
    Memory* buffer_memory(){return &memory;}Memory* image_memory(){return &memory;}
};
namespace evrm {using NativeState=::NativeState;struct GpuProbe {void buffer_destroyed(VkBuffer buffer){retire_test_sources(reinterpret_cast<std::uintptr_t>(buffer));native_preview_subjects().retire_buffer(1,reinterpret_cast<std::uintptr_t>(buffer));}};struct CommandTraceRuntime {void buffer_destroyed(VkBuffer){}};}
struct State {std::shared_ptr<Native> native=std::make_shared<Native>();NativeState metadata;evrm::GpuProbe probe;};
static auto state=std::make_shared<State>();
static void VKAPI_CALL original(VkDevice,VkBuffer,const VkAllocationCallbacks*){
    if(metadata_retired!=1||handoff.active()||trace.matches(30)||evrm::native_preview_subjects().active())throw std::runtime_error("forwarding preceded metadata/diagnostic/subject retirement");
    ++forwarded;
}
std::shared_ptr<State> device_state(VkDevice){return state;}
template<class T>T device_proc(const std::shared_ptr<State>&,const char*){return reinterpret_cast<T>(&original);}
template<class F>void observe(const std::shared_ptr<State>& value,F&& f){f(value->probe);}
template<class F>void observe_native(const std::shared_ptr<State>& value,F&& f){f(value->metadata);}
template<class F>void observe_trace(const std::shared_ptr<State>&,F&&){}
#define EVRM_EXPORT
#include "actual_destroy.inc"

template<class F>bool wait_for(F&& predicate,unsigned milliseconds){const auto start=std::chrono::steady_clock::now();while(!predicate()){
    if(std::chrono::steady_clock::now()-start>std::chrono::milliseconds(milliseconds))return false;
    std::this_thread::yield();}return true;}
template<class History,class F>bool while_report_paused(History& history,F&& callback){
    GateMutex::held=false;GateMutex::resume=false;GateMutex::arm=true;
    std::thread reporter([&]{history.report(1000);});
    const bool held=wait_for([]{return GateMutex::held.load(std::memory_order_acquire);},2000);
    std::atomic<bool> entered{false},finished{false};std::exception_ptr error;
    std::thread worker([&]{entered.store(true,std::memory_order_release);try{callback();}catch(...){error=std::current_exception();}finished.store(true,std::memory_order_release);});
    const bool started=wait_for([&]{return entered.load(std::memory_order_acquire);},2000);
    const bool prompt=started&&wait_for([&]{return finished.load(std::memory_order_acquire);},200);
    GateMutex::resume.store(true,std::memory_order_release);reporter.join();worker.join();
    if(!held)throw std::runtime_error("report fixture did not reach the held lock");
    if(error)std::rethrow_exception(error);
    return prompt;
}
int main(){try{
    unsigned blocked=0;const auto check=[&](bool value,const char* what){if(!value){++blocked;std::cerr<<"BLOCKED "<<what<<'\n';}};
    auto source=handoff.configure(1,2,3,30,77);bool retained=false;
    check(while_report_paused(handoff,[&]{retained=handoff.configure(1,2,3,30,77).epoch==source.epoch&&handoff.active();}),"unchanged source publication");
    check(retained,"unchanged source unnecessarily revoked");
    bool revoked=false;check(while_report_paused(handoff,[&]{handoff.configure(1,4,3,30,77);revoked=!handoff.active();}),"changed source publication");
    check(revoked,"contended changed source did not revoke immediately");
    handoff.configure(1,2,3,30,77);std::uint64_t retired=0;
    check(while_report_paused(handoff,[&]{retired=handoff.retire_owner(77);revoked=!handoff.active();}),"backend retirement");
    check(retired==30&&revoked,"backend retirement not immediate");
    handoff.configure(1,2,3,30,77);
    check(while_report_paused(handoff,[&]{retired=handoff.retire_owner(88);retained=handoff.active();}),"unrelated backend retirement");
    check(!retired&&retained,"unrelated backend was revoked");
    trace.configure(0x140000000ULL,44298240,30,99);
    check(while_report_paused(trace,[&]{trace.configure(0x140000000ULL,44298240,31,100);revoked=!trace.matches(30);}),"ancestry source publication");
    check(revoked,"contended ancestry source retained old filter");
    for(bool pause_handoff:{false,true}){
        handoff.configure(1,2,3,30,77);trace.configure(0x140000000ULL,44298240,30,99);forwarded=metadata_retired=0;
        const auto destroy=[] {vkDestroyBuffer(reinterpret_cast<VkDevice>(1),reinterpret_cast<VkBuffer>(30),nullptr);};
        const bool prompt=pause_handoff?while_report_paused(handoff,destroy):while_report_paused(trace,destroy);
        check(prompt,pause_handoff?"actual destruction forwarding behind handoff report":"actual destruction forwarding behind ancestry report");
        check(forwarded==1&&metadata_retired==1&&!handoff.active()&&!trace.matches(30),"actual destruction forwarding/retirement changed");
    }
    if(blocked)throw std::runtime_error("graphics callbacks waited for a suspended report worker or kept stale state");
    std::cout<<"paused real report locks: publication/retirement return immediately, stale cohorts revoked, actual buffer destruction forwarded once\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;}
