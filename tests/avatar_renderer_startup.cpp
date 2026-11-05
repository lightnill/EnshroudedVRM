#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
namespace evrm {
using VkInstance=int;using VkPhysicalDevice=int;using VkDevice=int;
using PFN_vkGetInstanceProcAddr=void*(*)(int,const char*);using PFN_vkGetDeviceProcAddr=PFN_vkGetInstanceProcAddr;
using PFN_vkSetDeviceLoaderData=void*;using PFN_vkGetPhysicalDeviceMemoryProperties=void(*)(int,int*);
using PFN_vkGetPhysicalDeviceQueueFamilyProperties=void(*)(int,std::uint32_t*,std::uint32_t*);
std::map<std::wstring,std::wstring> environment;
std::wstring env(const wchar_t* key){return environment[key];}
std::string narrow(const std::wstring& value){return std::filesystem::path(value).u8string();}
struct Asset {unsigned vertex_count=3;std::vector<std::string> warnings;};
struct PreparedAvatar {std::string name="Synthetic";std::shared_ptr<Asset> asset=std::make_shared<Asset>();};
struct Result {std::shared_ptr<const PreparedAvatar> model;std::string error;explicit operator bool()const{return bool(model);}};
unsigned imports=0,initialized=0;bool received_model=false;
Result load_prepared_avatar(const std::string&){++imports;return {std::make_shared<PreparedAvatar>(),{}};}
std::shared_ptr<const PreparedAvatar> restored;
struct Service {bool enabled=true;bool replacement_enabled(){return enabled;}std::shared_ptr<const PreparedAvatar> active(){return restored;}};
Service service;
Service* avatar_runtime_service(){return restored?&service:nullptr;}
void initialize_avatar_runtime(std::shared_ptr<const PreparedAvatar> initial){++initialized;received_model=bool(initial);}
void log_event(const std::string&){}template<class F>void log_event_lazy(F&& fn){log_event(fn());}
struct EnshroudedBridge {bool valid=true;EnshroudedBridge(const std::filesystem::path&,const std::filesystem::path&,const std::string&){}bool ready()const{return valid;}std::string error()const{return {};}};
struct CheckedStartup {bool admitted=false;std::shared_ptr<const EnshroudedBridge> bridge;std::filesystem::path runtime_directory;bool ready()const{return admitted;}};
CheckedStartup startup;
const CheckedStartup& runtime_startup_context(){return startup;}
struct AvatarCpuModel {AvatarCpuModel(std::shared_ptr<const PreparedAvatar>,bool,bool){}};
struct AvatarRenderer {
    struct Impl {int device=0,memory=0;PFN_vkGetDeviceProcAddr gdpa=nullptr;PFN_vkSetDeviceLoaderData set_loader=nullptr;
        std::vector<std::uint32_t> queue_properties;std::unique_ptr<EnshroudedBridge> bridge;std::filesystem::path shader_directory;
        float debug_offset=0;bool native_replacement=false,native_mode=false,gpu_skinning=false,active=false;std::unique_ptr<AvatarCpuModel> model;};
    std::unique_ptr<Impl> impl;
    AvatarRenderer(VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,PFN_vkSetDeviceLoaderData);
};
#include "avatar_renderer_startup_methods.inc"
void memory(int,int* value){*value=1;}
void queues(int,std::uint32_t* count,std::uint32_t* data){*count=1;if(data)*data=1;}
void* get_proc(int,const char* name){return std::string(name)=="vkGetPhysicalDeviceMemoryProperties"?reinterpret_cast<void*>(&memory):reinterpret_cast<void*>(&queues);}
}
void need(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
int main(){try{
    using namespace evrm;
    environment[L"ENSHROUDED_VRM_PROFILE"]=L"synthetic-profile.json";environment[L"ENSHROUDED_VRM_CATALOG"]=L"synthetic-catalog.json";
    environment[L"ENSHROUDED_VRM_NATIVE_REPLACEMENT"]=L"1";
    environment[L"ENSHROUDED_VRM_MODEL"]=L"C:\\fixture\\avatar.vrm";
    {AvatarRenderer dormant(1,2,3,get_proc,get_proc,nullptr);need(!dormant.impl->active&&!initialized&&!imports,"unchecked inherited flags/model initialized the mod");}
    startup.admitted=true;startup.bridge=std::make_shared<EnshroudedBridge>("checked-profile","checked-catalog","checked-build");startup.runtime_directory="checked-runtime";
    {AvatarRenderer empty(1,2,3,get_proc,get_proc,nullptr);need(initialized==1&&!received_model&&!imports&&empty.impl->active&&empty.impl->bridge&&!empty.impl->model,"checked model-free startup did not initialize F8 avatar service and native renderer");need(empty.impl->shader_directory==startup.runtime_directory/"shaders","untrusted shader path used");}
    restored=std::make_shared<PreparedAvatar>();
    {AvatarRenderer saved(1,2,3,get_proc,get_proc,nullptr);need(initialized==2&&received_model&&!imports&&saved.impl->model,"saved service selection was not used without synchronous environment import");}
    service.enabled=false;
    {AvatarRenderer disabled(1,2,3,get_proc,get_proc,nullptr);need(initialized==3&&!received_model&&!imports&&disabled.impl->active&&!disabled.impl->model,"new device loaded cached avatar while replacement disabled");}
    startup.bridge.reset();
    {AvatarRenderer declined(1,2,3,get_proc,get_proc,nullptr);need(initialized==3&&!declined.impl->active,"missing validated bridge initialized services");}
    std::cout<<"PASS actual renderer constructor: unchecked flags declined, checked empty-model F8 startup, restored service model and shader roots\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
