// Actual device_proc forwarding helper; no Vulkan loader, device or GPU.
#include "../src/runtime_hook_timing.hpp"
#include <vulkan/vulkan.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#if __has_include("../src/device_dispatch_cache.hpp")
#include "../src/device_dispatch_cache.hpp"
#else
namespace evrm {
// Existing behavior before the optional table: every query uses the provider.
class DeviceDispatchCache {
public:
    bool seed(VkDevice,PFN_vkGetDeviceProcAddr,const char*const*,std::size_t)noexcept{return false;}
    bool ready()const noexcept{return false;}std::size_t size()const noexcept{return 0;}
    PFN_vkVoidFunction lookup(VkDevice device,PFN_vkGetDeviceProcAddr resolve,const char* name)const{return device&&resolve&&name?resolve(device,name):nullptr;}
};
}
#endif
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
using Proc=int(VKAPI_PTR*)(int);
int VKAPI_CALL a(int value){return value+11;}
int VKAPI_CALL alias_a(int value){return value+111;}
int VKAPI_CALL b(int value){return value+22;}
int VKAPI_CALL alias_b(int value){return value+222;}
std::atomic<unsigned> resolutions{0};bool fail_capture=false;unsigned generation=1;
PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device,const char* name){
    ++resolutions;if(fail_capture)throw std::bad_alloc();
    const bool first=device==reinterpret_cast<VkDevice>(0x100)&&generation==1;
    if(!std::strcmp(name,"vkCmdFakeA"))return reinterpret_cast<PFN_vkVoidFunction>(first?a:b);
    if(!std::strcmp(name,"vkCmdFakeAKHR"))return reinterpret_cast<PFN_vkVoidFunction>(first?alias_a:alias_b);
    if(!std::strcmp(name,"vkUnknown"))return reinterpret_cast<PFN_vkVoidFunction>(b);
    return nullptr;
}
PFN_vkVoidFunction VKAPI_CALL other_resolve(VkDevice,const char*){++resolutions;return reinterpret_cast<PFN_vkVoidFunction>(alias_b);}
struct DeviceState {
    VkDevice handle=reinterpret_cast<VkDevice>(0x100);
    PFN_vkGetDeviceProcAddr get_proc=resolve;
    evrm::RuntimeHookTiming hook_timing;
    evrm::DeviceDispatchCache dispatch_cache;
};
const char* const names[]={"vkCmdFakeA","vkCmdFakeAKHR","vkUnsupported"};
void seed(DeviceState& state){need(state.dispatch_cache.seed(state.handle,state.get_proc,names,3),"per-device table was not initialized");need(state.dispatch_cache.ready()&&state.dispatch_cache.size()==3,"table shape is incomplete");}
#include "device_dispatch_helper.inc"
}
int main(int argc,char** argv){try{
    need(argc==2,"scenario required");const std::string mode=argv[1];auto state=std::make_shared<DeviceState>();
    if(mode=="actual-known"){
        state->dispatch_cache.seed(state->handle,state->get_proc,names,3); // RED must exercise the actual helper, not just this API.
        const auto before=resolutions.load();
        for(unsigned n=0;n<10000;++n){const auto function=device_proc<Proc>(state,"vkCmdFakeA");need(function&&function(5)==16,"cached helper changed forwarding or arguments");}
        need(before==3&&resolutions.load()==before,"actual forwarding helper still resolves the driver function on every call");
    }else if(mode=="uninitialized"){
        need(device_proc<Proc>(state,"vkCmdFakeA")(3)==14&&resolutions==1,"missing optional cache did not preserve old forwarding");
    }else if(mode=="invalid"){
        need(!state->dispatch_cache.seed({},resolve,names,3)&&!state->dispatch_cache.seed(state->handle,nullptr,names,3)&&
            !state->dispatch_cache.seed(state->handle,resolve,nullptr,3)&&!state->dispatch_cache.seed(state->handle,resolve,names,1025),"invalid table initialization accepted");
        need(resolutions==0&&!state->dispatch_cache.ready(),"invalid table queried a provider or became visible");
        need(device_proc<Proc>(state,"vkCmdFakeA")(1)==12,"invalid optional cache stopped original forwarding");
    }else if(mode=="capture-failed"){
        fail_capture=true;need(!state->dispatch_cache.seed(state->handle,resolve,names,3),"failed initialization published a partial table");fail_capture=false;
        need(!state->dispatch_cache.ready()&&state->dispatch_cache.size()==0,"failed table is partly visible");
        const auto before=resolutions.load();need(device_proc<Proc>(state,"vkCmdFakeA")(2)==13&&resolutions==before+1,"failed initialization did not fall back");
    }else if(mode=="owned-names"){
        std::string dynamic="vkCmdFakeA";const char* temporary[]={dynamic.c_str()};
        need(state->dispatch_cache.seed(state->handle,resolve,temporary,1),"owned name initialization failed");dynamic.assign(200,'x');
        const auto before=resolutions.load();need(device_proc<Proc>(state,"vkCmdFakeA")(4)==15&&resolutions==before,"table borrowed an expired or changed key");
    }else if(mode=="duplicates"){
        const char* duplicate[]={names[0],names[0],names[1],names[2]};
        need(state->dispatch_cache.seed(state->handle,resolve,duplicate,4)&&state->dispatch_cache.size()==3&&resolutions==3,"duplicate exact names queried twice or aliased different commands");
    }else if(mode=="benchmark"){
        std::vector<std::string> strings={names[0],names[1],names[2]};
        for(unsigned n=0;n<384;++n){std::ostringstream name;name<<"vkFakeCommand"<<std::setw(3)<<std::setfill('0')<<n;strings.push_back(name.str());}
        std::vector<const char*> pointers;for(const auto& name:strings)pointers.push_back(name.c_str());
        need(state->dispatch_cache.seed(state->handle,resolve,pointers.data(),pointers.size()),"benchmark initialization failed");
        const auto before=resolutions.load();constexpr unsigned count=2000000;std::uint64_t result=0;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned n=0;n<count;++n){const auto function=device_proc<Proc>(state,n&1?names[0]:names[1]);result+=function(1);}
        const auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
        need(result==124ull*(count/2)&&resolutions==before,"benchmark returned wrong aliases or repeated native provider queries");
        std::cout<<"BENCH actual cached device_proc names="<<state->dispatch_cache.size()<<" calls="<<count<<" driver-queries-in-loop="<<resolutions.load()-before<<" ns-per-call="<<elapsed/count<<'\n';
    }else{
        seed(*state);const auto before=resolutions.load();
        if(mode=="aliases"){
            need(device_proc<Proc>(state,names[0])(7)==18&&device_proc<Proc>(state,names[1])(7)==118&&resolutions==before,"equal-signature aliases shared the wrong pointer");
        }else if(mode=="null"){
            need(!device_proc<Proc>(state,names[2])&&!device_proc<Proc>(state,names[2])&&resolutions==before,"unsupported command was retried or acquired a function");
        }else if(mode=="unknown"){
            need(device_proc<Proc>(state,"vkUnknown")(1)==23&&device_proc<Proc>(state,"vkUnknown")(2)==24&&resolutions==before+2,"unlisted command no longer uses the original provider");
        }else if(mode=="device"){
            state->handle=reinterpret_cast<VkDevice>(0x200);need(device_proc<Proc>(state,names[0])(2)==24&&resolutions==before+1,"table used another device's address");
        }else if(mode=="resolver"){
            state->get_proc=other_resolve;need(device_proc<Proc>(state,names[0])(2)==224&&resolutions==before+1,"table used another next-layer resolver's address");
        }else if(mode=="reseed"){
            need(!state->dispatch_cache.seed(state->handle,other_resolve,names,3)&&resolutions==before,"immutable table was overwritten after publication");
            need(device_proc<Proc>(state,names[0])(2)==13,"failed reseed changed the existing device table");
        }else if(mode=="handle-reuse"){
            auto replacement=std::make_shared<DeviceState>();generation=2;seed(*replacement);const auto after=resolutions.load();
            need(device_proc<Proc>(state,names[0])(2)==13&&device_proc<Proc>(replacement,names[0])(2)==24&&resolutions==after,"reused device handle shared a prior state's table");
        }else if(mode=="threads"){
            std::atomic<unsigned> failures{0};std::vector<std::thread> readers;
            for(unsigned t=0;t<4;++t)readers.emplace_back([&]{for(unsigned n=0;n<10000;++n)if(device_proc<Proc>(state,names[n&1])(1)!=(n&1?112:12))++failures;});
            for(auto& reader:readers){reader.join();}
            need(!failures&&resolutions==before,"immutable concurrent lookups changed addresses or called the driver");
        }else throw std::runtime_error("unknown scenario");
    }
    std::cout<<"PASS device dispatch "<<mode<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
