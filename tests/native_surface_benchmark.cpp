// Reviewed/scanned-before-run, bounded headless material-kernel fixture.
// No game process access, native renderer linkage, DLL injection, network,
// window, surface or swapchain. Never execute via an automatic test discovery.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include "../vendor/tinygltf/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using Json=nlohmann::json;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
static constexpr std::uint64_t fence_timeout_ns=5'000'000'000ULL;
static constexpr std::size_t max_file_bytes=128u*1024u*1024u;
static constexpr std::size_t max_total_texture_bytes=128u*1024u*1024u;
static void require(bool ok,const std::string& text){if(!ok)throw std::runtime_error(text);}
static void checked(VkResult result,const char* operation){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+" VkResult="+std::to_string(result));}
static double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
static std::vector<std::uint8_t> read_bytes(const fs::path& path,std::size_t limit=max_file_bytes){
    std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"cannot open input: "+path.string());
    const auto size=file.tellg();require(size>=0&&std::uint64_t(size)<=limit,"input size out of bounds");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),size);require(bool(file),"short input read");return bytes;
}
static fs::path packet_child(const fs::path& folder,const Json& name){
    require(name.is_string(),"packet filename must be string");const auto value=name.get<std::string>();
    require(!value.empty()&&value.find_first_of("/\\:")==std::string::npos&&value!="."&&value!="..","packet filename must be a single local filename");return folder/value;
}
static std::vector<std::uint32_t> spirv(const fs::path& path){
    const auto bytes=read_bytes(path,4u*1024u*1024u);require(bytes.size()>=20&&bytes.size()%4==0,"invalid SPIR-V byte length");
    std::vector<std::uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());require(words[0]==0x07230203,"invalid SPIR-V magic");return words;
}
// All entry points are resolved from the absolute installed system loader.
#define INSTANCE_FUNCTIONS(X) \
 X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
 X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
 X(GetPhysicalDeviceFormatProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) \
 X(DestroyDevice) X(GetDeviceQueue) X(CreateBuffer) X(DestroyBuffer) \
 X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) \
 X(MapMemory) X(UnmapMemory) X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) \
 X(BindImageMemory) X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(DestroySampler) \
 X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) \
 X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
 X(CreateShaderModule) X(DestroyShaderModule) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
 X(CreateComputePipelines) X(DestroyPipeline) X(CreateCommandPool) X(DestroyCommandPool) \
 X(AllocateCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) \
 X(CmdPipelineBarrier) X(CmdCopyBuffer) X(CmdCopyBufferToImage) X(CmdBindPipeline) X(CmdBindDescriptorSets) \
 X(CmdDispatch) X(CreateFence) X(DestroyFence) X(ResetFences) X(QueueSubmit) X(WaitForFences) \
 X(CreateQueryPool) X(DestroyQueryPool) X(CmdResetQueryPool) X(CmdWriteTimestamp) X(GetQueryPoolResults)
struct Api {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr=nullptr;
    PFN_vkCreateInstance vkCreateInstance=nullptr;
#define DECLARE(name) PFN_vk##name vk##name=nullptr;
    INSTANCE_FUNCTIONS(DECLARE)
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
};
struct Buffer {VkBuffer handle=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void* mapped=nullptr;VkDeviceSize size=0,allocation=0;std::uint32_t memory_type=0,heap=0;VkMemoryPropertyFlags properties=0;};
struct Image {VkImage handle=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;VkSampler sampler=VK_NULL_HANDLE;VkDeviceSize allocation=0;};
struct ProbeControl {std::uint32_t sample_count,width,height,workload;float diffuse[4];};
struct ProbeOutput {float normal[4],lit[4],unlit[4],rgba[4];};
static_assert(sizeof(ProbeControl)==32&&sizeof(ProbeOutput)==64,"probe ABI");

struct Context {
    Api f;HMODULE loader=nullptr;
    VkInstance instance=VK_NULL_HANDLE;VkPhysicalDevice physical=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;VkQueue queue=VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};VkPhysicalDeviceMemoryProperties memory_properties{};
    std::uint32_t queue_family=0,timestamp_bits=0;VkCommandPool command_pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;VkQueryPool query_pool=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout=VK_NULL_HANDLE;VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;VkDescriptorSet descriptors=VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout=VK_NULL_HANDLE;VkShaderModule shader=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
    // Resources enter these vectors before any fallible allocation or binding,
    // so partial initialization is cleaned up by the same destructor.
    std::vector<std::unique_ptr<Buffer>> buffers;std::vector<std::unique_ptr<Image>> images;
    bool submitted=false;std::uint64_t allocated_bytes=0,peak_allocated_bytes=0;
    ~Context() noexcept {
        if(submitted){
            // No uncertain-completion free and no unbounded DeviceWaitIdle.
            // The bounded outer runner can terminate a driver-stalled process.
            std::cerr<<"GPU completion unknown; abandoning process without resource destruction.\n";std::cerr.flush();std::_Exit(3);
        }
        if(device){
            if(!f.vkDestroyDevice){std::cerr<<"Device cleanup entry unavailable; terminating process.\n";std::cerr.flush();std::_Exit(3);}
            if(pipeline)f.vkDestroyPipeline(device,pipeline,nullptr);
            if(shader)f.vkDestroyShaderModule(device,shader,nullptr);
            if(pipeline_layout)f.vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
            if(descriptor_pool)f.vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
            if(descriptor_layout)f.vkDestroyDescriptorSetLayout(device,descriptor_layout,nullptr);
            if(query_pool)f.vkDestroyQueryPool(device,query_pool,nullptr);
            if(fence)f.vkDestroyFence(device,fence,nullptr);
            if(command_pool)f.vkDestroyCommandPool(device,command_pool,nullptr);
            for(auto& image:images){if(image->sampler)f.vkDestroySampler(device,image->sampler,nullptr);if(image->view)f.vkDestroyImageView(device,image->view,nullptr);if(image->handle)f.vkDestroyImage(device,image->handle,nullptr);if(image->memory)f.vkFreeMemory(device,image->memory,nullptr);}
            for(auto& buffer:buffers)destroy_buffer(*buffer);
            f.vkDestroyDevice(device,nullptr);
        }
        if(instance&&f.vkDestroyInstance)f.vkDestroyInstance(instance,nullptr);
        if(loader)FreeLibrary(loader);
    }
    void destroy_buffer(Buffer& buffer)noexcept{
        if(buffer.mapped){f.vkUnmapMemory(device,buffer.memory);buffer.mapped=nullptr;}
        if(buffer.handle){f.vkDestroyBuffer(device,buffer.handle,nullptr);buffer.handle=VK_NULL_HANDLE;}
        if(buffer.memory){f.vkFreeMemory(device,buffer.memory,nullptr);buffer.memory=VK_NULL_HANDLE;allocated_bytes-=buffer.allocation;}
    }
    void init(unsigned device_index){
        wchar_t directory[MAX_PATH+1]{};const UINT length=GetSystemDirectoryW(directory,MAX_PATH);
        require(length>0&&length<MAX_PATH,"GetSystemDirectoryW failed");
        const std::wstring absolute=std::wstring(directory,length)+L"\\vulkan-1.dll";
        loader=LoadLibraryExW(absolute.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);require(loader!=nullptr,"installed system Vulkan loader unavailable");
        const auto entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
        static_assert(sizeof(entry)==sizeof(f.vkGetInstanceProcAddr),"Windows procedure pointer ABI");
        std::memcpy(&f.vkGetInstanceProcAddr,&entry,sizeof(entry));require(f.vkGetInstanceProcAddr!=nullptr,"vkGetInstanceProcAddr missing");
        f.vkCreateInstance=reinterpret_cast<PFN_vkCreateInstance>(f.vkGetInstanceProcAddr(VK_NULL_HANDLE,"vkCreateInstance"));require(f.vkCreateInstance!=nullptr,"vkCreateInstance missing");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="EVRM generated material kernel";app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;checked(f.vkCreateInstance(&info,nullptr,&instance),"vkCreateInstance");
#define LOAD_INSTANCE(name) f.vk##name=reinterpret_cast<PFN_vk##name>(f.vkGetInstanceProcAddr(instance,"vk" #name));require(f.vk##name!=nullptr,"missing instance entry vk" #name);
        INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
        std::uint32_t count=0;checked(f.vkEnumeratePhysicalDevices(instance,&count,nullptr),"enumerate devices count");require(count>0&&count<=32,"physical device count outside fixture bound");
        std::vector<VkPhysicalDevice> physicals(count);checked(f.vkEnumeratePhysicalDevices(instance,&count,physicals.data()),"enumerate devices");require(device_index<count,"requested device index out of range");physical=physicals[device_index];
        f.vkGetPhysicalDeviceProperties(physical,&properties);require(properties.apiVersion>=VK_API_VERSION_1_1,"Vulkan 1.1 required");
        require(properties.limits.timestampComputeAndGraphics&&std::isfinite(properties.limits.timestampPeriod)&&properties.limits.timestampPeriod>0,"compute timestamps unsupported");
        f.vkGetPhysicalDeviceMemoryProperties(physical,&memory_properties);
        std::uint32_t family_count=0;f.vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,nullptr);require(family_count>0&&family_count<=64,"queue family count outside fixture bound");
        std::vector<VkQueueFamilyProperties> families(family_count);f.vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,families.data());
        bool found=false;for(std::uint32_t i=0;i<family_count;++i)if(families[i].queueCount&&(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT)&&families[i].timestampValidBits>0){queue_family=i;timestamp_bits=families[i].timestampValidBits;found=true;break;}
        require(found&&timestamp_bits<=64,"no timestamp-capable compute queue");
        const long double wrap_ns=std::ldexp(static_cast<long double>(properties.limits.timestampPeriod),int(timestamp_bits));
        require(wrap_ns>static_cast<long double>(fence_timeout_ns),"timestamp wrap period too short for bounded wait");
        const float priority=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=queue_family;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};d.queueCreateInfoCount=1;d.pQueueCreateInfos=&q;checked(f.vkCreateDevice(physical,&d,nullptr,&device),"vkCreateDevice");
#define LOAD_DEVICE(name) f.vk##name=reinterpret_cast<PFN_vk##name>(f.vkGetDeviceProcAddr(device,"vk" #name));require(f.vk##name!=nullptr,"missing device entry vk" #name);
        DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
        f.vkGetDeviceQueue(device,queue_family,0,&queue);require(queue!=VK_NULL_HANDLE,"null compute queue");
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pool.queueFamilyIndex=queue_family;checked(f.vkCreateCommandPool(device,&pool,nullptr,&command_pool),"create command pool");
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};alloc.commandPool=command_pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;checked(f.vkAllocateCommandBuffers(device,&alloc,&command),"allocate command buffer");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};checked(f.vkCreateFence(device,&fence_info,nullptr,&fence),"create fence");
        VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};queries.queryType=VK_QUERY_TYPE_TIMESTAMP;queries.queryCount=2;checked(f.vkCreateQueryPool(device,&queries,nullptr,&query_pool),"create timestamp query pool");
    }
    std::uint32_t memory_type(std::uint32_t bits,VkMemoryPropertyFlags flags){
        // Prefer discrete device memory when available; unified DEVICE_LOCAL
        // types remain valid and are disclosed in the report.
        if(flags==VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)for(std::uint32_t i=0;i<memory_properties.memoryTypeCount;++i)
            if((bits&(1u<<i))&&(memory_properties.memoryTypes[i].propertyFlags&flags)==flags&&!(memory_properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))return i;
        for(std::uint32_t i=0;i<memory_properties.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory_properties.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw std::runtime_error("required device/host coherent memory type unavailable");
    }
    void allocate(VkMemoryRequirements requirements,VkMemoryPropertyFlags flags,VkDeviceMemory& memory,VkDeviceSize& allocation){
        require(requirements.size>0&&requirements.size<=256u*1024u*1024u,"single device allocation outside bound");
        require(allocated_bytes+requirements.size<=512u*1024u*1024u,"total owned device allocations exceed 512MiB bound");
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=requirements.size;a.memoryTypeIndex=memory_type(requirements.memoryTypeBits,flags);
        checked(f.vkAllocateMemory(device,&a,nullptr,&memory),"allocate memory");allocation=requirements.size;allocated_bytes+=allocation;peak_allocated_bytes=std::max(allocated_bytes,peak_allocated_bytes);
    }
    Buffer& buffer(VkDeviceSize size,VkBufferUsageFlags usage,bool local=false){
        require(size>0&&size<=max_file_bytes,"buffer size outside bound");buffers.push_back(std::make_unique<Buffer>());auto& b=*buffers.back();b.size=size;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;info.usage=usage;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;checked(f.vkCreateBuffer(device,&info,nullptr,&b.handle),"create buffer");
        VkMemoryRequirements requirements;f.vkGetBufferMemoryRequirements(device,b.handle,&requirements);
        const VkMemoryPropertyFlags flags=local?VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT:VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        b.memory_type=memory_type(requirements.memoryTypeBits,flags);b.properties=memory_properties.memoryTypes[b.memory_type].propertyFlags;b.heap=memory_properties.memoryTypes[b.memory_type].heapIndex;
        allocate(requirements,flags,b.memory,b.allocation);
        checked(f.vkBindBufferMemory(device,b.handle,b.memory,0),"bind buffer memory");
        if(!local)checked(f.vkMapMemory(device,b.memory,0,b.allocation,0,&b.mapped),"map coherent buffer");
        return b;
    }
    void begin(){checked(f.vkResetCommandBuffer(command,0),"reset command buffer");VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;checked(f.vkBeginCommandBuffer(command,&info),"begin command buffer");}
    void submit(){
        checked(f.vkEndCommandBuffer(command),"end command buffer");checked(f.vkResetFences(device,1,&fence),"reset fence");
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};info.commandBufferCount=1;info.pCommandBuffers=&command;
        submitted=true;const auto submission=f.vkQueueSubmit(queue,1,&info,fence);
        if(submission!=VK_SUCCESS){std::cerr<<"queue submission failed VkResult="<<submission<<'\n';std::cerr.flush();checked(submission,"submit compute");}
        const auto result=f.vkWaitForFences(device,1,&fence,VK_TRUE,fence_timeout_ns);
        if(result!=VK_SUCCESS){std::cerr<<"bounded fence wait failed VkResult="<<result<<'\n';std::cerr.flush();checked(result,"wait fence (5s maximum)");}
        submitted=false;
    }
    static VkSamplerAddressMode wrap(int mode){if(mode==33071)return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;if(mode==33648)return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;require(mode==10497,"unsupported fixture sampler wrap");return VK_SAMPLER_ADDRESS_MODE_REPEAT;}
    Image& texture(const Json& description,const std::vector<std::uint8_t>& pixels){
        const auto width=description.at("width").get<std::uint32_t>(),height=description.at("height").get<std::uint32_t>();
        require(width>0&&height>0&&width<=4096&&height<=4096&&std::uint64_t(width)*height*4==pixels.size(),"texture dimensions/bytes outside bound");
        require(width<=properties.limits.maxImageDimension2D&&height<=properties.limits.maxImageDimension2D,"texture dimensions exceed device limit");
        const auto format=description.at("srgb").get<bool>()?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;
        VkFormatProperties supported;f.vkGetPhysicalDeviceFormatProperties(physical,format,&supported);
        require((supported.optimalTilingFeatures&(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT))==(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT),"required sampled texture format unsupported");
        const auto mag=description.at("mag_filter").get<int>(),min=description.at("min_filter").get<int>();
        require((mag==9728||mag==9729)&&(min==9728||min==9729||min==9984||min==9985||min==9986||min==9987),"unsupported fixture texture filter");
        const bool linear_mag=mag==9729,linear_min=min==9729||min==9985||min==9987;
        if(linear_mag||linear_min)require((supported.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)!=0,"linear sampled format unsupported");
        images.push_back(std::make_unique<Image>());auto& image=*images.back();
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={width,height,1};info.mipLevels=1;info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        checked(f.vkCreateImage(device,&info,nullptr,&image.handle),"create texture image");VkMemoryRequirements requirements;f.vkGetImageMemoryRequirements(device,image.handle,&requirements);allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,image.memory,image.allocation);checked(f.vkBindImageMemory(device,image.handle,image.memory,0),"bind image memory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image.handle;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};checked(f.vkCreateImageView(device,&view,nullptr,&image.view),"create texture view");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=linear_mag?VK_FILTER_LINEAR:VK_FILTER_NEAREST;sampler.minFilter=linear_min?VK_FILTER_LINEAR:VK_FILTER_NEAREST;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sampler.addressModeU=wrap(description.at("wrap_s").get<int>());sampler.addressModeV=wrap(description.at("wrap_t").get<int>());sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.maxLod=0;checked(f.vkCreateSampler(device,&sampler,nullptr,&image.sampler),"create sampler");
        auto& staging=buffer(pixels.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);std::memcpy(staging.mapped,pixels.data(),pixels.size());
        begin();VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;host.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=staging.handle;host.size=VK_WHOLE_SIZE;
        f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&host,0,nullptr);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image.handle;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};f.vkCmdCopyBufferToImage(command,staging.handle,image.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);submit();destroy_buffer(staging);return image;
    }
    void make_pipeline(const std::vector<std::uint32_t>& words,bool enhanced,Buffer* material,Buffer* surface,Buffer& input,Buffer& output,Buffer& control,bool camera_only=false){
        require(properties.limits.maxPerStageDescriptorUniformBuffers>=1&&properties.limits.maxPerStageDescriptorStorageBuffers>=2,"descriptor limits too small");
        if(!camera_only)require(properties.limits.maxPerStageDescriptorSamplers>=4&&properties.limits.maxPerStageDescriptorSampledImages>=4&&properties.limits.maxPerStageDescriptorUniformBuffers>=3,"material descriptor limits too small");
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        const auto bind=[&](std::uint32_t number,VkDescriptorType type){VkDescriptorSetLayoutBinding value{};value.binding=number;value.descriptorType=type;value.descriptorCount=1;value.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;bindings.push_back(value);};
        if(!camera_only){bind(1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);bind(2,VK_DESCRIPTOR_TYPE_SAMPLER);bind(3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);}
        if(enhanced){bind(12,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);for(unsigned i=0;i<3;++i){bind(13+i*2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);bind(14+i*2,VK_DESCRIPTOR_TYPE_SAMPLER);}}
        bind(19,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);bind(20,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);bind(21,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layout.bindingCount=std::uint32_t(bindings.size());layout.pBindings=bindings.data();checked(f.vkCreateDescriptorSetLayout(device,&layout,nullptr,&descriptor_layout),"create descriptor layout");
        const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4},{VK_DESCRIPTOR_TYPE_SAMPLER,4},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=4;pool.pPoolSizes=sizes;checked(f.vkCreateDescriptorPool(device,&pool,nullptr,&descriptor_pool),"create descriptor pool");
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};alloc.descriptorPool=descriptor_pool;alloc.descriptorSetCount=1;alloc.pSetLayouts=&descriptor_layout;checked(f.vkAllocateDescriptorSets(device,&alloc,&descriptors),"allocate descriptors");
        const auto buffer_descriptor=[&](unsigned binding,VkDescriptorType type,Buffer& b){VkDescriptorBufferInfo value{b.handle,0,b.size};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=descriptors;write.dstBinding=binding;write.descriptorCount=1;write.descriptorType=type;write.pBufferInfo=&value;f.vkUpdateDescriptorSets(device,1,&write,0,nullptr);};
        if(!camera_only)buffer_descriptor(3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,*material);
        if(enhanced)buffer_descriptor(12,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,*surface);
        buffer_descriptor(19,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,input);buffer_descriptor(20,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,output);buffer_descriptor(21,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,control);
        for(unsigned slot=0;slot<images.size();++slot){
            const unsigned binding=slot==0?1:11+slot*2;
            VkDescriptorImageInfo image_info{VK_NULL_HANDLE,images[slot]->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet image_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};image_write.dstSet=descriptors;image_write.dstBinding=binding;image_write.descriptorCount=1;image_write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;image_write.pImageInfo=&image_info;f.vkUpdateDescriptorSets(device,1,&image_write,0,nullptr);
            VkDescriptorImageInfo sampler_info{images[slot]->sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};VkWriteDescriptorSet sampler_write=image_write;sampler_write.dstBinding=binding+1;sampler_write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;sampler_write.pImageInfo=&sampler_info;f.vkUpdateDescriptorSets(device,1,&sampler_write,0,nullptr);
        }
        VkPipelineLayoutCreateInfo pipeline_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pipeline_info.setLayoutCount=1;pipeline_info.pSetLayouts=&descriptor_layout;checked(f.vkCreatePipelineLayout(device,&pipeline_info,nullptr,&pipeline_layout),"create pipeline layout");
        VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shader_info.codeSize=words.size()*4;shader_info.pCode=words.data();checked(f.vkCreateShaderModule(device,&shader_info,nullptr,&shader),"create shader module");
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};compute.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;compute.stage.module=shader;compute.stage.pName="main";compute.layout=pipeline_layout;checked(f.vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline),"create compute pipeline");
    }
    double dispatch(Buffer& output,std::uint32_t count,Buffer* sample=nullptr){
        const auto groups=(count+63)/64;require(groups<=properties.limits.maxComputeWorkGroupCount[0],"dispatch group count exceeds device limit");
        begin();VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT;host.dstAccessMask=VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&host,0,nullptr,0,nullptr);
        f.vkCmdResetQueryPool(command,query_pool,0,2);f.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);f.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&descriptors,0,nullptr);
        f.vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,query_pool,0);f.vkCmdDispatch(command,groups,1,1);f.vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,query_pool,1);
        // Only the tiny correctness sample is copied, after the end timestamp.
        // Timed warmup/measurement dispatches pass no staging buffer.
        if(sample){
            const VkDeviceSize bytes=VkDeviceSize(count)*sizeof(ProbeOutput);require(bytes==sample->size&&bytes<=output.size,"correctness readback bounds");
            VkBufferMemoryBarrier transfer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};transfer.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;transfer.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            transfer.srcQueueFamilyIndex=transfer.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;transfer.buffer=output.handle;transfer.size=bytes;
            f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&transfer,0,nullptr);
            VkBufferCopy copy{0,0,bytes};f.vkCmdCopyBuffer(command,output.handle,sample->handle,1,&copy);
            VkBufferMemoryBarrier host_read{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host_read.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host_read.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            host_read.srcQueueFamilyIndex=host_read.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host_read.buffer=sample->handle;host_read.size=bytes;
            f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host_read,0,nullptr);
        }
        submit(); // Completion established before timestamp or staging reads.

        std::uint64_t ticks[2]{};checked(f.vkGetQueryPoolResults(device,query_pool,0,2,sizeof(ticks),ticks,sizeof(std::uint64_t),VK_QUERY_RESULT_64_BIT),"read timestamp queries after fence");
        const auto mask=timestamp_bits==64?std::numeric_limits<std::uint64_t>::max():((std::uint64_t(1)<<timestamp_bits)-1);
        const auto delta=(ticks[1]-ticks[0])&mask;const double duration=double(delta)*properties.limits.timestampPeriod/1e6;
        // A 5s bounded submission cannot identify multiple timestamp wraps when
        // the device's counter period is shorter. Reject that device outright.
        const long double wrap_ns=std::ldexp(static_cast<long double>(properties.limits.timestampPeriod),int(timestamp_bits));
        require(wrap_ns>static_cast<long double>(fence_timeout_ns),"timestamp wrap period too short for bounded wait");
        require(std::isfinite(duration)&&duration>=0&&duration<=5000,"invalid/wrapped timestamp duration");return duration;
    }
};

static Json run_mode(const fs::path& packet_path,const Json& packet,const std::vector<std::uint8_t>& constants,const std::vector<std::uint8_t>& inputs,const std::vector<std::uint32_t>& basic,const std::vector<std::uint32_t>& enhanced,const std::string& mode,unsigned device_index,unsigned width,unsigned height,unsigned sample_count){
    const bool use_enhanced=mode!="Basic"&&packet.at("features").get<unsigned>()!=0;
    const auto start=Clock::now();Context context;context.init(device_index);const double device_ms=ms(start);
    const auto& p=context.properties;
    const std::uint64_t count=std::uint64_t(width)*height,output_bytes=count*sizeof(ProbeOutput);
    require(output_bytes<=p.limits.maxStorageBufferRange&&inputs.size()<=p.limits.maxStorageBufferRange,"storage buffer range exceeds device limit");
    require(p.limits.maxComputeWorkGroupInvocations>=64&&p.limits.maxComputeWorkGroupSize[0]>=64,"compute local size unsupported");
    const auto setup=Clock::now();auto& material=context.buffer(80,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);std::memcpy(material.mapped,constants.data(),80);
    Buffer* surface=nullptr;if(use_enhanced){surface=&context.buffer(112,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);std::memcpy(surface->mapped,constants.data()+80,112);}
    auto& input=context.buffer(inputs.size(),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);std::memcpy(input.mapped,inputs.data(),inputs.size());
    auto& output=context.buffer(output_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);
    const auto readback_bytes=std::uint64_t(packet.at("sample_count").get<unsigned>())*sizeof(ProbeOutput);
    auto& readback=context.buffer(readback_bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    auto& control=context.buffer(sizeof(ProbeControl),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    ProbeControl settings{};settings.sample_count=packet.at("sample_count").get<std::uint32_t>();settings.width=settings.sample_count;settings.height=1;settings.workload=0;
    for(unsigned i=0;i<3;++i){settings.diffuse[i]=packet.at("diffuse").at(i).get<float>();}
    std::memcpy(control.mapped,&settings,sizeof(settings));
    std::uint64_t texel_bytes=0,authored_texel_bytes=0;const auto upload=Clock::now();
    for(unsigned slot=0;slot<(use_enhanced?4u:1u);++slot){const auto& description=packet.at("textures").at(slot);const auto pixels=read_bytes(packet_child(packet_path.parent_path(),description.at("file")));texel_bytes+=pixels.size();require(texel_bytes<=max_total_texture_bytes,"total texture bytes exceed fixture bound");if(description.at("authored_active").get<bool>())authored_texel_bytes+=pixels.size();context.texture(description,pixels);}
    const double upload_ms=ms(upload);context.make_pipeline(use_enhanced?enhanced:basic,use_enhanced,&material,surface,input,output,control);const double setup_ms=ms(setup);
    const double pixel_gpu_ms=context.dispatch(output,settings.sample_count,&readback);
    const auto* pixels=static_cast<const ProbeOutput*>(readback.mapped);Json pixel_values=Json::array();double max_error=0;bool pixel_pass=true;
    for(unsigned i=0;i<settings.sample_count;++i){const auto& expected=packet.at("reference").at("samples").at(i);Json value;
        const float* observed[]={pixels[i].normal,pixels[i].lit,pixels[i].unlit,pixels[i].rgba};const char* names[]={"normal","lit","unlit","rgba"};
        for(unsigned field=0;field<4;++field){value[names[field]]=Json::array();const unsigned channels=field==3?4:3;
            for(unsigned channel=0;channel<channels;++channel){const double actual=observed[field][channel];double target=expected.at(names[field]).at(channel).get<double>();
                if(!use_enhanced){if(field==0)target=channel==2?1:0;else if(field==1||field==2)target=0;else if(channel<3){const auto& e=expected;const double lit=e.at("lit").at(channel).get<double>(),unlit=e.at("unlit").at(channel).get<double>();target=e.at("rgba").at(channel).get<double>()-lit*settings.diffuse[channel]-unlit;}}
                const double error=std::abs(actual-target);if(!std::isfinite(actual)||error>.003)pixel_pass=false;if(std::isfinite(error))max_error=std::max(max_error,error);value[names[field]].push_back(actual);
            }
        }pixel_values.push_back(value);
    }
    settings.width=width;settings.height=height;settings.workload=1;std::memcpy(control.mapped,&settings,sizeof(settings));
    for(unsigned i=0;i<4;++i)context.dispatch(output,std::uint32_t(count));
    std::vector<double> times;times.reserve(sample_count);for(unsigned i=0;i<sample_count;++i)times.push_back(context.dispatch(output,std::uint32_t(count)));
    auto sorted=times;std::sort(sorted.begin(),sorted.end());const double median=sorted.size()%2?sorted[sorted.size()/2]:(sorted[sorted.size()/2-1]+sorted[sorted.size()/2])*.5;
    const double p95=sorted[std::size_t(std::ceil(.95*sorted.size()))-1];
    Json report={{"mode",mode},{"pipeline",use_enhanced?"Enhanced":"Basic"},{"pixels_pass",pixel_pass},{"pixel_absolute_tolerance",.003},{"pixel_max_absolute_error",max_error},{"pixels",pixel_values},{"pixel_dispatch_gpu_ms",pixel_gpu_ms},
        {"samples",sample_count},{"warmup_dispatches",4},{"resolution",{width,height}},{"geometry","equal-count compute invocations; no raster geometry"},{"gpu_ms",times},{"median_gpu_ms",median},{"p95_gpu_ms",p95},
        {"cold_device_setup_ms",device_ms},{"cold_resources_upload_pipeline_ms",setup_ms},{"cold_texture_upload_ms",upload_ms},{"owned_texture_texel_bytes",texel_bytes},{"owned_authored_texture_texel_bytes",authored_texel_bytes},
        {"source_surface_stream_bytes",use_enhanced?packet.at("surface_vertex_bytes").get<std::uint64_t>():0},{"resident_device_allocation_bytes",context.allocated_bytes},{"peak_device_allocation_bytes",context.peak_allocated_bytes},{"probe_input_bytes",inputs.size()},{"probe_output_bytes",output_bytes},{"correctness_readback_bytes",readback_bytes},{"timed_readback_bytes",0},{"output_memory",{{"type_index",output.memory_type},{"property_flags",output.properties},{"heap_index",output.heap},{"heap_flags",context.memory_properties.memoryHeaps[output.heap].flags},{"heap_bytes",context.memory_properties.memoryHeaps[output.heap].size},{"mapped",output.mapped!=nullptr}}},{"readback_memory",{{"type_index",readback.memory_type},{"property_flags",readback.properties},{"heap_index",readback.heap}}},
        {"device",{{"name",p.deviceName},{"type",p.deviceType},{"vendor_id",p.vendorID},{"device_id",p.deviceID},{"driver_version",p.driverVersion},{"api_version",p.apiVersion},{"physical_device_index",device_index},{"queue_family",context.queue_family},{"timestamp_valid_bits",context.timestamp_bits},{"timestamp_period_ns",p.limits.timestampPeriod}}}};
    return report;
}

static Json run_camera(const Json& packet,const std::vector<std::uint8_t>& inputs,const std::vector<std::uint32_t>& shader,unsigned device_index,unsigned width,unsigned height,unsigned sample_count){
    const auto start=Clock::now();Context context;context.init(device_index);const double device_ms=ms(start);
    const std::uint64_t count=std::uint64_t(width)*height,output_bytes=count*sizeof(ProbeOutput);
    require(count>=packet.at("sample_count").get<unsigned>()&&output_bytes<=context.properties.limits.maxStorageBufferRange&&inputs.size()<=context.properties.limits.maxStorageBufferRange,"camera storage/count exceeds device limit");
    require(context.properties.limits.maxComputeWorkGroupInvocations>=64&&context.properties.limits.maxComputeWorkGroupSize[0]>=64,"camera compute local size unsupported");
    const auto setup=Clock::now();auto& input=context.buffer(inputs.size(),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);std::memcpy(input.mapped,inputs.data(),inputs.size());
    auto& output=context.buffer(output_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);
    const auto readback_bytes=std::uint64_t(packet.at("sample_count").get<unsigned>())*sizeof(ProbeOutput);
    auto& readback=context.buffer(readback_bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    auto& control=context.buffer(sizeof(ProbeControl),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    ProbeControl settings{};settings.sample_count=packet.at("sample_count").get<std::uint32_t>();settings.width=settings.sample_count;settings.height=1;std::memcpy(control.mapped,&settings,sizeof(settings));
    context.make_pipeline(shader,false,nullptr,nullptr,input,output,control,true);const double setup_ms=ms(setup);
    const double pixel_ms=context.dispatch(output,settings.sample_count,&readback);const auto* pixels=static_cast<const ProbeOutput*>(readback.mapped);
    bool pass=true;double max_error=0;Json observed=Json::array();
    for(unsigned i=0;i<settings.sample_count;++i){const auto& expected=packet.at("reference").at("samples").at(i);Json sample;
        const float* values[]={pixels[i].normal,pixels[i].lit,pixels[i].unlit,pixels[i].rgba};const char* names[]={"normal","lit","unlit","rgba"};
        for(unsigned field=0;field<4;++field){sample[names[field]]=Json::array();for(unsigned channel=0;channel<4;++channel){
            const double actual=values[field][channel],target=channel==3?0:expected.at(names[field]).at(channel).get<double>();
            const double error=std::abs(actual-target),tolerance=.003;
            if(!std::isfinite(actual)||error>tolerance)pass=false;
            if(std::isfinite(error))max_error=std::max(max_error,error);
            sample[names[field]].push_back(actual);
        }}observed.push_back(sample);
    }
    settings.width=width;settings.height=height;settings.workload=1;std::memcpy(control.mapped,&settings,sizeof(settings));
    for(unsigned i=0;i<4;++i)context.dispatch(output,std::uint32_t(count));
    std::vector<double> times;for(unsigned i=0;i<sample_count;++i)times.push_back(context.dispatch(output,std::uint32_t(count)));
    auto sorted=times;std::sort(sorted.begin(),sorted.end());const double median=sorted.size()%2?sorted[sorted.size()/2]:(sorted[sorted.size()/2-1]+sorted[sorted.size()/2])*.5;
    const auto& p=context.properties;
    return {{"mode","Camera"},{"pipeline","Actual pure camera helpers"},{"pixels_pass",pass},{"pixels",observed},{"pixel_absolute_tolerance",.003},{"pixel_max_absolute_error",max_error},{"pixel_dispatch_gpu_ms",pixel_ms},
        {"case_names",packet.at("reference").at("case_names")},{"resolution",{width,height}},{"warmup_dispatches",4},{"samples",sample_count},{"gpu_ms",times},{"median_gpu_ms",median},{"p95_gpu_ms",sorted[std::size_t(std::ceil(.95*sorted.size()))-1]},
        {"cold_device_setup_ms",device_ms},{"cold_resources_pipeline_ms",setup_ms},{"owned_texture_texel_bytes",0},{"probe_input_bytes",inputs.size()},{"probe_output_bytes",output_bytes},{"correctness_readback_bytes",readback_bytes},{"timed_readback_bytes",0},{"output_memory",{{"type_index",output.memory_type},{"property_flags",output.properties},{"heap_index",output.heap},{"heap_flags",context.memory_properties.memoryHeaps[output.heap].flags},{"heap_bytes",context.memory_properties.memoryHeaps[output.heap].size},{"mapped",output.mapped!=nullptr}}},{"readback_memory",{{"type_index",readback.memory_type},{"property_flags",readback.properties},{"heap_index",readback.heap}}},{"resident_device_allocation_bytes",context.allocated_bytes},{"peak_device_allocation_bytes",context.peak_allocated_bytes},
        {"device",{{"name",p.deviceName},{"type",p.deviceType},{"vendor_id",p.vendorID},{"device_id",p.deviceID},{"driver_version",p.driverVersion},{"api_version",p.apiVersion},{"physical_device_index",device_index},{"queue_family",context.queue_family},{"timestamp_valid_bits",context.timestamp_bits},{"timestamp_period_ns",p.limits.timestampPeriod}}}};
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    std::set_terminate([]()noexcept{std::fputs("FAIL unexpected terminate in GPU surface helper\n",stderr);std::fflush(stderr);std::_Exit(3);});
    // Six mandatory paths/description make accidental double-click or generic
    // test enumeration a non-GPU failure. The reviewed runner supplies timeout.
    try{
        require(argc==8||argc==11,"usage: native_surface_benchmark packet.json basic.spv enhanced.spv report.json gpu-load-context device-index samples [width height mode]; mode=all|rotate1|rotate2|Basic|Auto|Enhanced");
        const fs::path packet_path=argv[1],output_path=argv[4];const std::string load_context=argv[5];require(load_context.size()>=8&&load_context.size()<=2048,"explicit observed GPU/game load context required");
        const auto device_index=std::stoul(argv[6]),samples=std::stoul(argv[7]);require(device_index<32&&samples>=32&&samples<=256,"device/sample count outside bound");
        unsigned width=512,height=512;std::string mode="all";if(argc==11){width=unsigned(std::stoul(argv[8]));height=unsigned(std::stoul(argv[9]));mode=argv[10];}
        require(width>=16&&width<=1024&&height>=1&&height<=1024,"benchmark resolution outside bound");require(mode=="all"||mode=="rotate1"||mode=="rotate2"||mode=="Basic"||mode=="Auto"||mode=="Enhanced","invalid benchmark mode");
        const auto packet_bytes=read_bytes(packet_path,2u*1024u*1024u);const auto packet=Json::parse(packet_bytes);const bool camera_only=packet.at("format")=="EVRM camera packet v1";
        require(camera_only||packet.at("format")=="EVRM surface packet v1","unknown packet format");
        std::vector<std::uint8_t> constants;if(!camera_only)constants=read_bytes(packet_child(packet_path.parent_path(),packet.at("constants")),192);
        const auto inputs=read_bytes(packet_child(packet_path.parent_path(),packet.at("inputs")),176u*64u);
        const auto count=packet.at("sample_count").get<unsigned>();
        if(camera_only)require(count>0&&count<=64&&inputs.size()==count*176,"invalid camera packet shape");
        else require(constants.size()==192&&count==16&&inputs.size()==count*96&&packet.at("textures").size()==4,"invalid material packet shapes");
        const auto basic=spirv(argv[2]),enhanced=spirv(argv[3]);
        SYSTEM_INFO system{};GetNativeSystemInfo(&system);MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);require(GlobalMemoryStatusEx(&memory)!=0,"system memory context unavailable");
        Json report={{"format","EVRM material-kernel benchmark v1"},{"packet",packet_path.filename().string()},{"fixture",packet.at("fixture")},{"load_context",load_context},{"operating_system","Windows; system Vulkan loader; no instance layers requested"},
            {"system",{{"native_processor_architecture",system.wProcessorArchitecture},{"logical_processors",system.dwNumberOfProcessors},{"physical_memory_bytes",memory.ullTotalPhys},{"available_physical_memory_bytes",memory.ullAvailPhys},{"memory_load_percent",memory.dwMemoryLoad}}},
            {"limits","Headless material compute kernel only. No native-game memory, rasterization, whole-game FPS, compositing/alpha-silhouette, GBuffer, presentation or in-game acceptance. External GPU/game load is supplied by caller and can contaminate timings; never assumes idle hardware. Mode runs are sequential separate device/resource setups; warm pipeline/disk caches may affect cold timings."},{"modes",Json::array()}};
        if(camera_only){
            auto result=run_camera(packet,inputs,basic,unsigned(device_index),width,height,unsigned(samples));const bool pass=result.at("pixels_pass").get<bool>();report["format"]="EVRM camera-kernel benchmark v1";report["modes"].push_back(result);
            std::ofstream output(output_path);output<<report.dump(2)<<'\n';require(bool(output),"camera report write failed");std::cout<<(pass?"PASS":"FAIL")<<" actual camera helper pixel vectors\n";return pass?0:2;
        }
        std::array<std::string,3> order{{"Basic","Auto","Enhanced"}};
        if(mode=="rotate1")std::rotate(order.begin(),order.begin()+1,order.end());
        if(mode=="rotate2")std::rotate(order.begin(),order.begin()+2,order.end());
        report["mode_order"]=order;
        bool pass=true;
        for(const auto& candidate:order)if(mode=="all"||mode=="rotate1"||mode=="rotate2"||mode==candidate){
            auto result=run_mode(packet_path,packet,constants,inputs,basic,enhanced,candidate,unsigned(device_index),width,height,unsigned(samples));pass=pass&&result.at("pixels_pass").get<bool>();report["modes"].push_back(result);
            // Preserve completed modes even if a later mode fails/device hangs.
            std::ofstream output(output_path);output<<report.dump(2)<<'\n';require(bool(output),"report write failed");
        }
        std::cout<<(pass?"PASS":"FAIL")<<" material-kernel pixels; recorded "<<samples<<" timestamp samples per mode\n";return pass?0:2;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
    catch(...){std::cerr<<"FAIL nonstandard exception\n";return 1;}
}
