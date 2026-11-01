#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>
namespace evrm {
using VkInstance=int;using VkPhysicalDevice=int;using VkDevice=int;using VkDeviceSize=std::uint64_t;
using PFN_vkGetInstanceProcAddr=void*(*)(int,const char*);using PFN_vkGetDeviceProcAddr=PFN_vkGetInstanceProcAddr;
using PFN_vkSetDeviceLoaderData=void*;using PFN_vkGetPhysicalDeviceMemoryProperties=void(*)(int,int*);
using PFN_vkGetPhysicalDeviceQueueFamilyProperties=void(*)(int,std::uint32_t*,std::uint32_t*);
struct VkPhysicalDeviceProperties{struct {std::uint64_t minUniformBufferOffsetAlignment=256;}limits;};
using PFN_vkGetPhysicalDeviceProperties=void(*)(int,VkPhysicalDeviceProperties*);
struct Startup {bool admitted=false;std::filesystem::path capture_directory;bool ready()const{return admitted;}} startup;
const Startup& runtime_startup_context(){return startup;}
std::wstring poison;
std::wstring environment(const wchar_t*){return poison;}
std::set<std::uint64_t> probe_capture_frames(const std::wstring&){return {};}
void log_event(const char*){}
struct GpuProbe {
    struct Impl {int device=0,memory_properties=0;PFN_vkGetDeviceProcAddr gdpa=nullptr;PFN_vkSetDeviceLoaderData set_loader_data=nullptr;
        bool active=false,anchor_enabled=false;std::uint64_t uniform_alignment=0;std::filesystem::path directory;
        std::set<std::uint64_t> requested_frames;std::vector<std::uint32_t> queue_properties;};
    std::unique_ptr<Impl> impl;
    GpuProbe(VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,PFN_vkSetDeviceLoaderData);
    bool enabled()const;
};
#include "standalone_probe_startup_methods.inc"
unsigned memory_queries=0;
void memory(int,int* value){++memory_queries;*value=1;}
void queues(int,std::uint32_t* count,std::uint32_t* data){*count=1;if(data)*data=1;}
void* get_proc(int,const char* name){return std::string(name)=="vkGetPhysicalDeviceMemoryProperties"?reinterpret_cast<void*>(&memory):reinterpret_cast<void*>(&queues);}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)return 2;
        using namespace evrm;
        const auto blocker=std::filesystem::path(argv[1])/"blocker";std::ofstream(blocker)<<"unchanged";poison=blocker.wstring();
        {GpuProbe dormant(1,2,3,get_proc,get_proc,nullptr);if(dormant.enabled() || memory_queries)return 10;}
        startup.admitted=true;
        {GpuProbe ready(1,2,3,get_proc,get_proc,nullptr);if(!ready.enabled() || memory_queries!=1 || ready.impl->anchor_enabled || !ready.impl->requested_frames.empty())return 11;}
        std::cout<<"storage-independent GPU readback startup; untrusted capture environment ignored\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 12;}
}
