#include "native_avatar_assets.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
namespace evrm {
namespace {
void check(VkResult result,const char* reason){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(reason)+" "+std::to_string(result));}
template<class T>T info(VkStructureType type){T result{};result.sType=type;return result;}
struct AssetContext {
    VkDevice device{};PFN_vkGetDeviceProcAddr get{};VkPhysicalDeviceMemoryProperties memory{};bool* quarantine=nullptr;
    bool uncertain()const noexcept{return quarantine&&*quarantine;}
    void mark_uncertain()noexcept{if(quarantine)*quarantine=true;}
    template<class T>T fn(const char* name)const{return reinterpret_cast<T>(get(device,name));}
    std::uint32_t memory_type(std::uint32_t bits,VkMemoryPropertyFlags flags)const {
        for(unsigned n=0;n<memory.memoryTypeCount;++n)if((bits&(1u<<n))&&(memory.memoryTypes[n].propertyFlags&flags)==flags)return n;
        throw std::runtime_error("Avatar GPU memory unavailable");
    }
};
struct AssetBuffer {
    std::shared_ptr<AssetContext> c;VkBuffer buffer{};VkDeviceMemory memory{};void* mapped=nullptr;
    ~AssetBuffer(){if(c->uncertain())return;if(mapped)c->fn<PFN_vkUnmapMemory>("vkUnmapMemory")(c->device,memory);if(buffer)c->fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(c->device,buffer,nullptr);if(memory)c->fn<PFN_vkFreeMemory>("vkFreeMemory")(c->device,memory,nullptr);}
};
struct AssetImage {
    std::shared_ptr<AssetContext> c;VkImage image{};VkImageView view{};VkDeviceMemory memory{};
    ~AssetImage(){if(c->uncertain())return;if(view)c->fn<PFN_vkDestroyImageView>("vkDestroyImageView")(c->device,view,nullptr);if(image)c->fn<PFN_vkDestroyImage>("vkDestroyImage")(c->device,image,nullptr);if(memory)c->fn<PFN_vkFreeMemory>("vkFreeMemory")(c->device,memory,nullptr);}
};
std::unique_ptr<AssetBuffer> staging(const std::shared_ptr<AssetContext>& c,const std::vector<std::uint8_t>& bytes){
    auto result=std::make_unique<AssetBuffer>();result->c=c;
    auto bi=info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);bi.size=bytes.size();bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check(c->fn<PFN_vkCreateBuffer>("vkCreateBuffer")(c->device,&bi,nullptr,&result->buffer),"Avatar staging buffer");
    VkMemoryRequirements requirements{};c->fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(c->device,result->buffer,&requirements);
    auto ai=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);ai.allocationSize=requirements.size;ai.memoryTypeIndex=c->memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(c->fn<PFN_vkAllocateMemory>("vkAllocateMemory")(c->device,&ai,nullptr,&result->memory),"Avatar staging memory");
    check(c->fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(c->device,result->buffer,result->memory,0),"Avatar staging binding");
    check(c->fn<PFN_vkMapMemory>("vkMapMemory")(c->device,result->memory,0,bytes.size(),0,&result->mapped),"Avatar staging map");std::memcpy(result->mapped,bytes.data(),bytes.size());return result;
}
}
struct NativeAvatarGpuAssets::Impl {
    std::shared_ptr<AssetContext> c;std::shared_ptr<const vrm::Avatar> source;std::shared_ptr<NativeSkinGpuAsset> skin;
    std::vector<std::unique_ptr<AssetImage>> images;std::vector<VkSampler> samplers;std::vector<std::uint32_t> material_images;
    ~Impl(){if(!c->uncertain())for(auto sampler:samplers)if(sampler)c->fn<PFN_vkDestroySampler>("vkDestroySampler")(c->device,sampler,nullptr);}
};
NativeAvatarGpuAssets::NativeAvatarGpuAssets(std::shared_ptr<Impl> value):impl_(std::move(value)){}
NativeAvatarGpuAssets::~NativeAvatarGpuAssets()=default;
const std::shared_ptr<const vrm::Avatar>& NativeAvatarGpuAssets::avatar()const noexcept{return impl_->source;}
const std::shared_ptr<NativeSkinGpuAsset>& NativeAvatarGpuAssets::skin()const noexcept{return impl_->skin;}
VkDescriptorImageInfo NativeAvatarGpuAssets::material(std::size_t index)const noexcept {
    if(impl_->samplers.empty())return {};
    index=std::min(index,impl_->samplers.size()-1);const auto image=impl_->material_images[index];
    return {impl_->samplers[index],impl_->images[image]->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}
struct NativeAvatarAssetStager::Impl {
    struct Job {
        std::shared_ptr<AssetContext> c;std::uint64_t generation=0;VkQueue queue{};std::uint32_t family=0;
        std::shared_ptr<NativeAvatarGpuAssets> assets;std::shared_ptr<const NativeSkinAsset> skin_source;
        VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};bool submitted=false,cancelled=false;
        std::vector<std::unique_ptr<AssetBuffer>> staging;
        ~Job(){if(submitted)c->mark_uncertain();if(c->uncertain())return;if(fence)c->fn<PFN_vkDestroyFence>("vkDestroyFence")(c->device,fence,nullptr);if(pool)c->fn<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(c->device,pool,nullptr);}
    };
    std::shared_ptr<AssetContext> c;PFN_vkSetDeviceLoaderData loader{};std::shared_ptr<NativeSkinningGpu> skinning;
    std::unique_ptr<Job> job;NativeAvatarUploadState state=NativeAvatarUploadState::Idle;std::uint64_t failed_generation=0;std::string error;
    Impl(VkDevice device,PFN_vkGetDeviceProcAddr get,PFN_vkSetDeviceLoaderData set,const VkPhysicalDeviceMemoryProperties& memory,std::shared_ptr<NativeSkinningGpu> gpu,bool* quarantine):
        c(std::make_shared<AssetContext>(AssetContext{device,get,memory,quarantine})),loader(set),skinning(std::move(gpu)){}
    void begin(VkQueue queue,std::uint32_t family,std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& avatar,const std::shared_ptr<const NativeSkinAsset>& skin){
        if(avatar->materials.size()>4096||avatar->images.size()>4096)throw std::runtime_error("Avatar material budget exceeded");
        job=std::make_unique<Job>();auto& out=*job;out.c=c;out.queue=queue;out.family=family;out.generation=generation;out.skin_source=skin;
        auto data=std::make_shared<NativeAvatarGpuAssets::Impl>();data->c=c;data->source=avatar;
        out.assets=std::shared_ptr<NativeAvatarGpuAssets>(new NativeAvatarGpuAssets(data));
        auto pi=info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        check(c->fn<PFN_vkCreateCommandPool>("vkCreateCommandPool")(c->device,&pi,nullptr,&out.pool),"Avatar upload pool");
        auto ai=info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);ai.commandPool=out.pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
        check(c->fn<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(c->device,&ai,&out.command),"Avatar upload command");if(loader)check(loader(c->device,out.command),"Avatar upload loader data");
        auto fi=info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);check(c->fn<PFN_vkCreateFence>("vkCreateFence")(c->device,&fi,nullptr,&out.fence),"Avatar upload fence");
        auto bi=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(c->fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(out.command,&bi),"Avatar upload begin");
        if(skin){if(!skinning)throw std::runtime_error("Avatar GPU skinning unavailable");data->skin=skinning->asset(skin);skinning->upload(out.command,data->skin);}
        const vrm::Image white{1,1,{255,255,255,255}};std::vector<const vrm::Image*> pixels{&white};std::map<int,std::uint32_t> images;
        for(const auto& material:avatar->materials)if(material.base_texture>=0){const auto& texture=avatar->textures.at(material.base_texture);
            if(!images.count(texture.image)){images[texture.image]=std::uint32_t(pixels.size());pixels.push_back(&avatar->images.at(texture.image));}}
        for(const auto* source:pixels){
            if(source->width<=0||source->height<=0||source->rgba.size()!=std::size_t(source->width)*source->height*4)throw std::runtime_error("Avatar texture dimensions invalid");
            auto image=std::make_unique<AssetImage>();image->c=c;
            auto ci=info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_SRGB;ci.extent={std::uint32_t(source->width),std::uint32_t(source->height),1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
            check(c->fn<PFN_vkCreateImage>("vkCreateImage")(c->device,&ci,nullptr,&image->image),"Avatar texture image");
            VkMemoryRequirements requirements{};c->fn<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(c->device,image->image,&requirements);
            auto allocation=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=c->memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(c->fn<PFN_vkAllocateMemory>("vkAllocateMemory")(c->device,&allocation,nullptr,&image->memory),"Avatar texture memory");check(c->fn<PFN_vkBindImageMemory>("vkBindImageMemory")(c->device,image->image,image->memory,0),"Avatar texture binding");
            auto vi=info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);vi.image=image->image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ci.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            check(c->fn<PFN_vkCreateImageView>("vkCreateImageView")(c->device,&vi,nullptr,&image->view),"Avatar texture view");auto buffer=staging(c,source->rgba);
            auto barrier=info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image->image;barrier.subresourceRange=vi.subresourceRange;
            c->fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(out.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent=ci.extent;c->fn<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(out.command,buffer->buffer,image->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            c->fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(out.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            data->images.push_back(std::move(image));out.staging.push_back(std::move(buffer));
        }
        const auto wrap=[](int value){return value==33071?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:value==33648?VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:VK_SAMPLER_ADDRESS_MODE_REPEAT;};
        for(std::size_t n=0;n<=avatar->materials.size();++n){const auto index=n<avatar->materials.size()?avatar->materials[n].base_texture:-1;const auto* texture=index<0?nullptr:&avatar->textures.at(index);
            auto sampler=info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);sampler.magFilter=texture&&texture->mag_filter==9728?VK_FILTER_NEAREST:VK_FILTER_LINEAR;sampler.minFilter=texture&&(texture->min_filter==9728||texture->min_filter==9984||texture->min_filter==9986)?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
            sampler.addressModeU=texture?wrap(texture->wrap_s):VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.addressModeV=texture?wrap(texture->wrap_t):VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            VkSampler handle{};check(c->fn<PFN_vkCreateSampler>("vkCreateSampler")(c->device,&sampler,nullptr,&handle),"Avatar sampler");
            try{data->samplers.push_back(handle);}catch(...){c->fn<PFN_vkDestroySampler>("vkDestroySampler")(c->device,handle,nullptr);throw;}
            data->material_images.push_back(texture?images.at(texture->image):0);
        }
        check(c->fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(out.command),"Avatar upload end");
        auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&out.command;out.submitted=true;
        const auto result=c->fn<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,1,&submit,out.fence);
        if(result!=VK_SUCCESS){if(result==VK_ERROR_DEVICE_LOST)out.submitted=false;else c->mark_uncertain();check(result,"Avatar upload submission");}
    }
    bool poll(){
        if(!job||!job->submitted)return true;
        const auto result=c->fn<PFN_vkGetFenceStatus>("vkGetFenceStatus")(c->device,job->fence);
        if(result==VK_NOT_READY)return false;
        if(result!=VK_SUCCESS){if(result==VK_ERROR_DEVICE_LOST)job->submitted=false;else c->mark_uncertain();check(result,"Avatar upload completion");}
        job->submitted=false;if(job->assets->skin())skinning->finish_upload(job->assets->skin());job->staging.clear();return true;
    }
};
NativeAvatarAssetStager::NativeAvatarAssetStager(VkDevice device,PFN_vkGetDeviceProcAddr get,PFN_vkSetDeviceLoaderData loader,const VkPhysicalDeviceMemoryProperties& memory,std::shared_ptr<NativeSkinningGpu> skinning,bool* quarantine):
    impl_(std::make_unique<Impl>(device,get,loader,memory,std::move(skinning),quarantine)){}
NativeAvatarAssetStager::~NativeAvatarAssetStager()=default;
NativeAvatarUploadState NativeAvatarAssetStager::update(VkQueue queue,std::uint32_t family,std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& avatar,const std::shared_ptr<const NativeSkinAsset>& skin)noexcept {
    try{
        if(impl_->c->uncertain()){impl_->state=NativeAvatarUploadState::Failed;return impl_->state;}
        if(impl_->job){auto& job=*impl_->job;
            if(!generation||job.generation!=generation||job.assets->avatar()!=avatar||job.skin_source!=skin||job.queue!=queue||job.family!=family)job.cancelled=true;
            if(!impl_->poll()){impl_->state=NativeAvatarUploadState::Pending;return impl_->state;}
            if(job.cancelled){impl_->job.reset();impl_->state=NativeAvatarUploadState::Idle;}
            else{impl_->state=NativeAvatarUploadState::Ready;return impl_->state;}}
        if(!generation||!avatar){impl_->state=NativeAvatarUploadState::Idle;return impl_->state;}
        if(impl_->state==NativeAvatarUploadState::Failed&&impl_->failed_generation==generation)return impl_->state;
        impl_->error.clear();impl_->begin(queue,family,generation,avatar,skin);impl_->state=NativeAvatarUploadState::Pending;return impl_->state;
    }catch(const std::exception& error){impl_->error=error.what();}catch(...){impl_->error="Avatar GPU upload failed";}
    impl_->failed_generation=generation;impl_->state=NativeAvatarUploadState::Failed;
    if(impl_->job&&!impl_->job->submitted)impl_->job.reset();
    return impl_->state;
}
std::shared_ptr<NativeAvatarGpuAssets> NativeAvatarAssetStager::ready(std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& asset)const noexcept {
    return impl_->state==NativeAvatarUploadState::Ready&&impl_->job&&!impl_->job->cancelled&&impl_->job->generation==generation&&impl_->job->assets->avatar()==asset?impl_->job->assets:nullptr;
}
std::shared_ptr<NativeAvatarGpuAssets> NativeAvatarAssetStager::take_ready(std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& asset)noexcept {
    auto result=ready(generation,asset);if(result){impl_->job.reset();impl_->state=NativeAvatarUploadState::Idle;}return result;
}
void NativeAvatarAssetStager::cancel()noexcept{if(impl_->job)impl_->job->cancelled=true;}
const std::string& NativeAvatarAssetStager::error()const noexcept{return impl_->error;}
void NativeAvatarAssetStager::device_idle_complete()noexcept{if(impl_->job)impl_->job->submitted=false;impl_->job.reset();impl_->state=NativeAvatarUploadState::Idle;}
}
