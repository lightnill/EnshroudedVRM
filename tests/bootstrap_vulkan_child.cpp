#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef EVRM_NO_BOOTSTRAP
// Vulkan header supplies the ordinary import declaration.
// Retain a real ordinary import without invoking any Windows power API.
static auto volatile retained_import = &vkGetInstanceProcAddr;
#endif
template<class T> T procedure(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name); T value{};
    static_assert(sizeof(value) == sizeof(address)); std::memcpy(&value,&address,sizeof(value)); return value;
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(GetErrorMode() | 0x8003);
#ifndef EVRM_NO_BOOTSTRAP
    if (!retained_import) return 2;
#endif
    if (argc == 2 && std::wstring(argv[1]) == L"startup") {
        std::cout << "startup reached" << std::endl; std::cin.get(); return 0;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"resolve") {
        wchar_t directory[32768]; const auto length = GetSystemDirectoryW(directory,32768);
        if (!length || length > 32700) return 20;
        const auto system = LoadLibraryExW((std::wstring(directory,length)+L"\\vulkan-1.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto proxy = GetModuleHandleW(L"vulkan-1.dll");
        if (!system || !proxy || system == proxy) return 21;
        std::string name; std::size_t count=0;
        while (std::getline(std::cin,name)) {
            const char* symbol = name.c_str();
            if (!name.empty() && name[0] == '#') symbol = MAKEINTRESOURCEA(std::stoul(name.substr(1)));
            const auto expected = GetProcAddress(system,symbol), got = GetProcAddress(proxy,symbol);
            if (!expected || got != expected) { std::cerr << "resolution mismatch " << name << " error=" << GetLastError() << '\n'; return 22; }
            ++count;
        }
        std::cout << "system forwarding addresses matched: " << count << '\n';
        return count ? 0 : 23;
    }
    wchar_t system_directory[32768]{};
    const auto system_length = GetSystemDirectoryW(system_directory,32768);
    if (!system_length || system_length > 32700) return 3;
    const auto loader = LoadLibraryExW((std::wstring(system_directory,system_length)+L"\\vulkan-1.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loader) return 3;
    const auto create = procedure<PFN_vkCreateInstance>(loader,"vkCreateInstance");
    const auto destroy = procedure<PFN_vkDestroyInstance>(loader,"vkDestroyInstance");
    const auto enumerate = procedure<PFN_vkEnumeratePhysicalDevices>(loader,"vkEnumeratePhysicalDevices");
    const auto families = procedure<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(loader,"vkGetPhysicalDeviceQueueFamilyProperties");
    const auto create_device = procedure<PFN_vkCreateDevice>(loader,"vkCreateDevice");
    const auto destroy_device = procedure<PFN_vkDestroyDevice>(loader,"vkDestroyDevice");
    if (!create || !destroy || !enumerate || !families || !create_device || !destroy_device) return 4;
    VkApplicationInfo application{}; application.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; application.apiVersion=VK_API_VERSION_1_0;
    VkInstanceCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; info.pApplicationInfo=&application;
    VkInstance instance{}; const auto result=create(&info,nullptr,&instance);
    if (result != VK_SUCCESS) { std::cerr << "vkCreateInstance=" << result << '\n'; return 5; }
    std::uint32_t count=0; if (enumerate(instance,&count,nullptr) != VK_SUCCESS || !count) { destroy(instance,nullptr); return 6; }
    std::vector<VkPhysicalDevice> devices(count); if (enumerate(instance,&count,devices.data()) != VK_SUCCESS) return 7;
    std::uint32_t family_count=0; families(devices.front(),&family_count,nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(family_count); families(devices.front(),&family_count,queue_families.data());
    std::uint32_t family=0; while (family < family_count && !queue_families[family].queueCount) ++family;
    if (family == family_count) return 8;
    const float priority=1; VkDeviceQueueCreateInfo queue{}; queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; queue.queueFamilyIndex=family; queue.queueCount=1; queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo device_info{}; device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; device_info.queueCreateInfoCount=1; device_info.pQueueCreateInfos=&queue;
    VkDevice device{}; const auto device_result=create_device(devices.front(),&device_info,nullptr,&device);
    if (device_result != VK_SUCCESS) { std::cerr << "vkCreateDevice=" << device_result << '\n'; return 9; }
    if (argc==2 && std::wstring(argv[1])==L"declined") {
        const auto mod=GetModuleHandleW(L"enshrouded_vrm_runtime.dll");
        const auto gdpa=procedure<PFN_vkGetDeviceProcAddr>(loader,"vkGetDeviceProcAddr");
        const auto tracked=mod?GetProcAddress(mod,"vkCreateBuffer"):nullptr;
        if(!mod || !gdpa || !tracked)return 30;
        if(reinterpret_cast<std::uintptr_t>(gdpa(device,"vkCreateBuffer"))==reinterpret_cast<std::uintptr_t>(tracked))return 31;
    }
    destroy_device(device,nullptr); destroy(instance,nullptr);
    std::cout << "system Vulkan instance/device created and destroyed\n";
    return 0;
}
