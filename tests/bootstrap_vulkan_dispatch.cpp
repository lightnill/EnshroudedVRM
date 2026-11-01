// Owned isolated fixture. Never installed beside the actual game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef EVRM_DIRECT_IMPORT
static auto volatile retained_import = &vkGetInstanceProcAddr;
#endif
#ifdef EVRM_EARLY_DEPENDENCY
extern "C" __declspec(dllimport) unsigned ProbeEarlyState();
#endif

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::wstring environment(const wchar_t* name) {
    wchar_t value[32768]{};
    const DWORD size = GetEnvironmentVariableW(name, value, 32768);
    require(size < 32768, "oversized environment value");
    return {value, size};
}
static std::wstring module_path(HMODULE module) {
    wchar_t value[32768]{};
    const DWORD size = GetModuleFileNameW(module, value, 32768);
    require(size && size < 32768, "module path unavailable");
    return {value, size};
}
static bool same_path(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
template<class T> static T procedure(HMODULE module, const char* name) {
    const FARPROC raw = GetProcAddress(module, name);
    T value{};
    static_assert(sizeof(value) == sizeof(raw));
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}
static void vk_ok(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation)+" result="+std::to_string(result));
}

static void graphics(HMODULE proxy, HMODULE system) {
    const auto gipa = procedure<PFN_vkGetInstanceProcAddr>(proxy, "vkGetInstanceProcAddr");
    const auto real_gipa = procedure<PFN_vkGetInstanceProcAddr>(system, "vkGetInstanceProcAddr");
    require(gipa && gipa == real_gipa, "GIPA forwarding mismatch");
    for (const char* name : {"vkCreateInstance", "vkEnumerateInstanceVersion", "vkEVRM_DoesNotExist"})
        require(gipa(VK_NULL_HANDLE, name) == real_gipa(VK_NULL_HANDLE, name), "global dispatch mismatch");
    auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    require(create != nullptr, "missing create instance");
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application;
    VkInstance instance{};
    vk_ok(create(&instance_info, nullptr, &instance), "create instance");
    for (const char* name : {"vkEnumeratePhysicalDevices", "vkCreateDevice", "vkGetDeviceProcAddr", "vkEVRM_DoesNotExist"})
        require(gipa(instance, name) == real_gipa(instance, name), "instance dispatch mismatch");
#define INSTANCE_PROC(name) const auto name = reinterpret_cast<PFN_##name>(gipa(instance, #name)); require(name != nullptr, #name)
    INSTANCE_PROC(vkEnumeratePhysicalDevices);
    INSTANCE_PROC(vkGetPhysicalDeviceQueueFamilyProperties);
    INSTANCE_PROC(vkGetPhysicalDeviceMemoryProperties);
    INSTANCE_PROC(vkCreateDevice);
    INSTANCE_PROC(vkDestroyInstance);
    INSTANCE_PROC(vkGetDeviceProcAddr);
    std::uint32_t count = 0;
    vk_ok(vkEnumeratePhysicalDevices(instance, &count, nullptr), "count physical devices");
    require(count != 0, "no physical device");
    std::vector<VkPhysicalDevice> physical(count);
    vk_ok(vkEnumeratePhysicalDevices(instance, &count, physical.data()), "enumerate physical devices");
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical.front(), &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical.front(), &family_count, families.data());
    std::uint32_t family = 0;
    while (family < family_count && (!families[family].queueCount || !(families[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT|VK_QUEUE_TRANSFER_BIT)))) ++family;
    require(family < family_count, "no transfer-capable queue");
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    VkDevice device{};
    vk_ok(vkCreateDevice(physical.front(), &device_info, nullptr, &device), "create device");
#ifdef EVRM_CHECK_NATIVE_DORMANT
    const HMODULE native = GetModuleHandleW(L"enshrouded_vrm_runtime.dll");
    require(native != nullptr, "native runtime did not load through implicit discovery");
    const FARPROC native_buffer = GetProcAddress(native, "vkCreateBuffer");
    require(native_buffer != nullptr, "native runtime export missing");
    require(reinterpret_cast<std::uintptr_t>(vkGetDeviceProcAddr(device, "vkCreateBuffer")) != reinterpret_cast<std::uintptr_t>(native_buffer), "dormant native runtime intercepted device commands");
    require(GetModuleHandleW(L"OVRLipSync.dll") == nullptr, "dormant native runtime started optional lip sync");
    std::cout << "native runtime loaded, declined fixture owner, no device interception or optional lip-sync load\n";
#endif
    const auto real_gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(real_gipa(instance, "vkGetDeviceProcAddr"));
    for (const char* name : {"vkCreateBuffer", "vkCmdFillBuffer", "vkQueueSubmit", "vkEVRM_DoesNotExist"})
        require(vkGetDeviceProcAddr(device, name) == real_gdpa(device, name), "device dispatch mismatch");
#define DEVICE_PROC(name) const auto name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)); require(name != nullptr, #name)
    DEVICE_PROC(vkDestroyDevice);
    DEVICE_PROC(vkCreateBuffer); DEVICE_PROC(vkDestroyBuffer);
    DEVICE_PROC(vkGetBufferMemoryRequirements);
    DEVICE_PROC(vkAllocateMemory); DEVICE_PROC(vkFreeMemory); DEVICE_PROC(vkBindBufferMemory);
    DEVICE_PROC(vkMapMemory); DEVICE_PROC(vkUnmapMemory);
    DEVICE_PROC(vkCreateCommandPool); DEVICE_PROC(vkDestroyCommandPool);
    DEVICE_PROC(vkAllocateCommandBuffers);
    DEVICE_PROC(vkBeginCommandBuffer); DEVICE_PROC(vkEndCommandBuffer); DEVICE_PROC(vkCmdFillBuffer);
    DEVICE_PROC(vkCmdPipelineBarrier);
    DEVICE_PROC(vkGetDeviceQueue); DEVICE_PROC(vkQueueSubmit);
    DEVICE_PROC(vkCreateFence); DEVICE_PROC(vkDestroyFence); DEVICE_PROC(vkWaitForFences);
    constexpr VkDeviceSize bytes = 4096;
    constexpr std::uint32_t fill = 0x7142ABCD;
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = bytes;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer{};
    vk_ok(vkCreateBuffer(device, &buffer_info, nullptr, &buffer), "create buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical.front(), &properties);
    std::uint32_t type = 0;
    const VkMemoryPropertyFlags needed = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (type < properties.memoryTypeCount && (!(requirements.memoryTypeBits & (1u << type)) || (properties.memoryTypes[type].propertyFlags & needed) != needed)) ++type;
    require(type < properties.memoryTypeCount, "no coherent host-visible memory");
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    VkDeviceMemory memory{};
    vk_ok(vkAllocateMemory(device, &allocation, nullptr, &memory), "allocate buffer memory");
    vk_ok(vkBindBufferMemory(device, buffer, memory, 0), "bind buffer memory");
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = family;
    VkCommandPool pool{};
    vk_ok(vkCreateCommandPool(device, &pool_info, nullptr, &pool), "create command pool");
    VkCommandBufferAllocateInfo command_allocation{};
    command_allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_allocation.commandPool = pool;
    command_allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocation.commandBufferCount = 1;
    VkCommandBuffer command{};
    vk_ok(vkAllocateCommandBuffers(device, &command_allocation, &command), "allocate command buffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_ok(vkBeginCommandBuffer(command, &begin), "begin command buffer");
    vkCmdFillBuffer(command, buffer, 0, bytes, fill);
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.size = bytes;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    vk_ok(vkEndCommandBuffer(command), "end command buffer");
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence{};
    vk_ok(vkCreateFence(device, &fence_info, nullptr, &fence), "create fence");
    VkQueue queue{};
    vkGetDeviceQueue(device, family, 0, &queue);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    vk_ok(vkQueueSubmit(queue, 1, &submit, fence), "submit GPU fill");
    vk_ok(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ULL), "wait for GPU fill");
    void* mapped{};
    vk_ok(vkMapMemory(device, memory, 0, bytes, 0, &mapped), "map readback");
    const auto words = static_cast<const std::uint32_t*>(mapped);
    for (std::size_t n = 0; n < static_cast<std::size_t>(bytes)/sizeof(fill); ++n) require(words[n] == fill, "GPU readback mismatch");
    vkUnmapMemory(device, memory);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    std::cout << "instance/device dispatch and 4096-byte GPU fill/readback passed\n";
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(GetErrorMode() | 0x8003);
    try {
#ifdef EVRM_DIRECT_IMPORT
        require(retained_import != nullptr, "ordinary import removed");
        require(environment(L"VK_ADD_IMPLICIT_LAYER_PATH") == environment(L"EVRM_EXPECT_LAYER_PATH"), "setup did not precede main");
#else
        require(environment(L"VK_ADD_IMPLICIT_LAYER_PATH") == environment(L"EVRM_INITIAL_LAYER_PATH"), "unexpected setup before bare-name load");
#endif
#ifdef EVRM_EARLY_DEPENDENCY
        require(ProbeEarlyState() == 3, "setup or Vulkan availability did not precede dependent attach");
        std::cout << "environment and global Vulkan dispatch available in dependency attach\n";
#endif
        const std::wstring mode = argc > 1 ? argv[1] : L"resolve";
        if (mode == L"preloaded") {
            wchar_t preloaded_directory[32768]{};
            const UINT preloaded_size = GetSystemDirectoryW(preloaded_directory, 32768);
            require(preloaded_size && preloaded_size < 32700, "preload directory unavailable");
            require(LoadLibraryExW((std::wstring(preloaded_directory, preloaded_size)+L"\\vulkan-1.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) != nullptr, "system preload failed");
        }
        const HMODULE proxy = LoadLibraryW(L"vulkan-1.dll");
        if (!proxy) throw std::runtime_error("bare-name Vulkan load failed error="+std::to_string(GetLastError()));
        require(same_path(module_path(proxy), environment(L"EVRM_EXPECT_PROXY_PATH")), "bare-name load bypassed proxy");
        require(environment(L"VK_ADD_IMPLICIT_LAYER_PATH") == environment(L"EVRM_EXPECT_LAYER_PATH"), "layer setup mismatch after load");
        wchar_t directory[32768]{};
        const UINT length = GetSystemDirectoryW(directory, 32768);
        require(length && length < 32700, "system directory unavailable");
        const std::wstring system_path = std::wstring(directory, length)+L"\\vulkan-1.dll";
        const HMODULE system = LoadLibraryExW(system_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(system && system != proxy && same_path(module_path(system), system_path), "provider did not reach distinct system Vulkan loader");
        std::string name;
        std::size_t checked = 0;
        while (std::getline(std::cin, name)) {
            if (name == "--EVRM-END--") break;
            const char* symbol = name.c_str();
            if (!name.empty() && name.front() == '#') symbol = MAKEINTRESOURCEA(std::stoul(name.substr(1)));
            const FARPROC expected = GetProcAddress(system, symbol);
            const FARPROC got = GetProcAddress(proxy, symbol);
            require(expected && got == expected, ("export forwarding mismatch: "+name).c_str());
            ++checked;
        }
        require(checked == 530, "expected 265 named plus 265 ordinal resolutions");
        std::cout << "forwarding matched " << checked << " names/ordinals\n";
        const auto gipa = procedure<PFN_vkGetInstanceProcAddr>(proxy, "vkGetInstanceProcAddr");
        require(gipa != nullptr, "missing forwarded GIPA");
        std::atomic<unsigned> failures{0};
        std::vector<std::thread> workers;
        for (unsigned n=0; n<8; ++n) workers.emplace_back([&] {
            for (unsigned i=0; i<256; ++i)
                if (procedure<PFN_vkGetInstanceProcAddr>(proxy, "vkGetInstanceProcAddr") != gipa || !gipa(VK_NULL_HANDLE, "vkCreateInstance")) ++failures;
        });
        for (auto& worker : workers) worker.join();
        require(failures == 0, "concurrent forwarding failed");
        std::cout << "2048 concurrent resolution/global-dispatch checks passed\n";
        if (mode == L"gpu") graphics(proxy, system);
        if (mode == L"hold") { std::cout << "holding checked manifest" << std::endl; std::cin.clear(); std::cin.get(); }
        FreeLibrary(system);
        FreeLibrary(proxy);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
