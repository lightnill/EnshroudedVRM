#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_NO_PROTOTYPES
#include <windows.h>
#include "../src/native_skinning_gpu.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
void checked(VkResult r,const char* s){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(s)+": "+std::to_string(r));}
PFN_vkGetDeviceProcAddr native_get{};PFN_vkCmdPushConstants native_push{};unsigned private_push_writes=0;
void VKAPI_CALL observed_push(VkCommandBuffer cmd,VkPipelineLayout layout,VkShaderStageFlags stages,std::uint32_t offset,std::uint32_t bytes,const void* data){++private_push_writes;native_push(cmd,layout,stages,offset,bytes,data);}
PFN_vkVoidFunction VKAPI_CALL observed_get(VkDevice device,const char* name){
    if(std::strcmp(name,"vkCmdPushConstants")==0)return reinterpret_cast<PFN_vkVoidFunction>(observed_push);
    return native_get(device,name);
}
template<class T>T info(VkStructureType s){T t{};t.sType=s;return t;}
vrm::Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
struct Context {
    HMODULE loader{};PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr get{};VkInstance instance{};VkDevice device{};VkQueue queue{};
    VkPhysicalDeviceMemoryProperties memory{};VkPhysicalDeviceLimits limits{};VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};bool uncertain=false;
    VkPhysicalDevice physical{};std::uint32_t family=0;
    template<class T>T fn(const char* n){return reinterpret_cast<T>(get(device,n));}
    explicit Context(VkQueueFlags required=VK_QUEUE_COMPUTE_BIT,bool wsi=false){
        // These are unattended fixture executables. Failures remain nonzero
        // test exits instead of leaving a desktop dialog after the test times out.
        SetErrorMode(GetErrorMode()|SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
        loader=LoadLibraryW(L"vulkan-1.dll");require(bool(loader),"Vulkan loader missing");gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(reinterpret_cast<void*>(GetProcAddress(loader,"vkGetInstanceProcAddr")));
        auto app=info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);app.apiVersion=VK_API_VERSION_1_1;
        auto ci=info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);ci.pApplicationInfo=&app;
        const char* instance_extensions[]={"VK_KHR_surface","VK_KHR_win32_surface"};if(wsi){ci.enabledExtensionCount=2;ci.ppEnabledExtensionNames=instance_extensions;}
        checked(reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"))(&ci,nullptr,&instance),"instance");
        auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance,"vkEnumeratePhysicalDevices"));std::uint32_t count=0;checked(enumerate(instance,&count,nullptr),"devices");std::vector<VkPhysicalDevice> devices(count);checked(enumerate(instance,&count,devices.data()),"devices");
        for(auto candidate:devices){VkPhysicalDeviceProperties props{};reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance,"vkGetPhysicalDeviceProperties"))(candidate,&props);
            auto queues=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance,"vkGetPhysicalDeviceQueueFamilyProperties"));std::uint32_t qc=0;queues(candidate,&qc,nullptr);std::vector<VkQueueFamilyProperties> qs(qc);queues(candidate,&qc,qs.data());
            for(unsigned i=0;i<qc;++i)if(qs[i].queueCount&&(qs[i].queueFlags&required)==required){physical=candidate;family=i;limits=props.limits;std::cout<<"DEVICE "<<props.deviceName<<'\n';break;}
            if(physical)break;
        }require(bool(physical),"compute queue missing");reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"))(physical,&memory);
        float priority=1;auto qi=info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
        auto di=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
        const char* device_extensions[]={"VK_KHR_swapchain"};if(wsi){di.enabledExtensionCount=1;di.ppEnabledExtensionNames=device_extensions;}
        checked(reinterpret_cast<PFN_vkCreateDevice>(gipa(instance,"vkCreateDevice"))(physical,&di,nullptr,&device),"device");get=reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance,"vkGetDeviceProcAddr"));fn<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device,family,0,&queue);
        auto pi=info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;checked(fn<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device,&pi,nullptr,&pool),"pool");
        auto ai=info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;checked(fn<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device,&ai,&command),"command");
        auto fi=info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);checked(fn<PFN_vkCreateFence>("vkCreateFence")(device,&fi,nullptr,&fence),"fence");
    }
    void begin(){checked(fn<PFN_vkResetCommandPool>("vkResetCommandPool")(device,pool,0),"reset");auto bi=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);checked(fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&bi),"begin");}
    void submit(){checked(fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command),"end");checked(fn<PFN_vkResetFences>("vkResetFences")(device,1,&fence),"reset fence");auto si=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);si.commandBufferCount=1;si.pCommandBuffers=&command;uncertain=true;checked(fn<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,1,&si,fence),"submit");auto r=fn<PFN_vkWaitForFences>("vkWaitForFences")(device,1,&fence,VK_TRUE,5'000'000'000ull);if(r==VK_SUCCESS||r==VK_ERROR_DEVICE_LOST)uncertain=false;checked(r,"completion");}
    ~Context(){if(device&&!uncertain){fn<PFN_vkDestroyFence>("vkDestroyFence")(device,fence,nullptr);fn<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device,pool,nullptr);fn<PFN_vkDestroyDevice>("vkDestroyDevice")(device,nullptr);}if(instance&&!uncertain)reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"))(instance,nullptr);if(loader&&!uncertain)FreeLibrary(loader);}
};
struct HostBuffer {
    Context& c;VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize size;
    HostBuffer(Context& ctx,VkDeviceSize bytes,VkBufferUsageFlags extra=0):c(ctx),size(bytes){auto bi=info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);bi.size=bytes;bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|extra;checked(c.fn<PFN_vkCreateBuffer>("vkCreateBuffer")(c.device,&bi,nullptr,&buffer),"buffer");VkMemoryRequirements req{};c.fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(c.device,buffer,&req);unsigned type=c.memory.memoryTypeCount;for(unsigned i=0;i<c.memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(c.memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}require(type<c.memory.memoryTypeCount,"host memory");auto ai=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);ai.allocationSize=req.size;ai.memoryTypeIndex=type;checked(c.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(c.device,&ai,nullptr,&memory),"allocate");checked(c.fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(c.device,buffer,memory,0),"bind");checked(c.fn<PFN_vkMapMemory>("vkMapMemory")(c.device,memory,0,bytes,0,&mapped),"map");std::memset(mapped,0,bytes);}
    VkDescriptorBufferInfo binding(){return {buffer,0,size};}
    ~HostBuffer(){if(c.uncertain)return;if(mapped)c.fn<PFN_vkUnmapMemory>("vkUnmapMemory")(c.device,memory);if(buffer)c.fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(c.device,buffer,nullptr);if(memory)c.fn<PFN_vkFreeMemory>("vkFreeMemory")(c.device,memory,nullptr);}
};
struct Input {vrm::Avatar avatar;vrm::RetargetResult pose;};
Input fixture(std::size_t count=3){Input f;vrm::Primitive p;p.skinned=true;p.indices={0,1,2};
    for(std::size_t i=0;i<count;++i){vrm::Vertex v;v.position={float(int(i%23)-11)*.03f,float(i%31)*.04f,float(int(i%17)-8)*.02f};v.normal={.3f,.7f,.5f};v.uv={.25f,.75f};v.joints={0,1,0,1};v.weights={.25f,.75f,0,0};p.vertices.push_back(v);}f.avatar.meshes.push_back({{p}});f.avatar.vertex_count=count;f.avatar.triangle_count=1;
    f.avatar.nodes.resize(2);f.pose.node_world.resize(2,identity());for(auto& n:f.avatar.nodes){n.active=true;n.local=n.world=identity();}f.avatar.nodes[0].mesh=0;f.avatar.nodes[0].skin=0;f.avatar.skins.push_back({{0,1},{identity(),identity()}});f.pose.palettes.push_back({0,0,{identity(),identity()}});return f;}
}
