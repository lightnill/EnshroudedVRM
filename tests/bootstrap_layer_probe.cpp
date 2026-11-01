// Minimal owned fixture, not the product renderer. Observe actual loader chains.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <cstring>

static PFN_vkGetInstanceProcAddr next_instance = nullptr;
static PFN_vkGetDeviceProcAddr next_device = nullptr;
static VkInstance active_instance = VK_NULL_HANDLE;
static void marker(char kind) {
    wchar_t path[32768]; const auto length=GetEnvironmentVariableW(L"EVRM_PROBE_MARKER",path,32768);
    if (!length || length >= 32768) return;
    const auto file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written=0; WriteFile(file,&kind,1,&written,nullptr); CloseHandle(file);
}
static VKAPI_ATTR VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* allocator, VkInstance* instance) {
    auto chain=static_cast<const VkLayerInstanceCreateInfo*>(info->pNext);
    while (chain && (chain->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || chain->function != VK_LAYER_LINK_INFO)) chain=static_cast<const VkLayerInstanceCreateInfo*>(chain->pNext);
    if (!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    next_instance=chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const auto next=reinterpret_cast<PFN_vkCreateInstance>(next_instance(VK_NULL_HANDLE,"vkCreateInstance"));
    const_cast<VkLayerInstanceCreateInfo*>(chain)->u.pLayerInfo=chain->u.pLayerInfo->pNext;
    const auto result=next(info,allocator,instance);
    if (result == VK_SUCCESS) { active_instance=*instance; marker('I'); }
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_device(VkPhysicalDevice physical, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator, VkDevice* device) {
    auto chain=static_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
    while (chain && (chain->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || chain->function != VK_LAYER_LINK_INFO)) chain=static_cast<const VkLayerDeviceCreateInfo*>(chain->pNext);
    if (!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    const auto next_gipa=chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    next_device=chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    const auto next=reinterpret_cast<PFN_vkCreateDevice>(next_gipa(active_instance,"vkCreateDevice"));
    const_cast<VkLayerDeviceCreateInfo*>(chain)->u.pLayerInfo=chain->u.pLayerInfo->pNext;
    const auto result=next(physical,info,allocator,device); if (result == VK_SUCCESS) marker('D'); return result;
}
extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) return nullptr;
    if (!std::strcmp(name,"vkGetInstanceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(&vkGetInstanceProcAddr);
    if (!std::strcmp(name,"vkCreateInstance")) return reinterpret_cast<PFN_vkVoidFunction>(&create_instance);
    if (!std::strcmp(name,"vkCreateDevice")) return reinterpret_cast<PFN_vkVoidFunction>(&create_device);
    return next_instance ? next_instance(instance,name) : nullptr;
}
extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    return next_device ? next_device(device,name) : nullptr;
}
extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* info) {
    if (!info || info->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || info->loaderLayerInterfaceVersion < 2) return VK_ERROR_INITIALIZATION_FAILED;
    info->loaderLayerInterfaceVersion=2; info->pfnGetInstanceProcAddr=&vkGetInstanceProcAddr; info->pfnGetDeviceProcAddr=&vkGetDeviceProcAddr; info->pfnGetPhysicalDeviceProcAddr=nullptr; return VK_SUCCESS;
}
