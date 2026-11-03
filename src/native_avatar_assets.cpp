#include "native_avatar_assets.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <limits>
#include <stdexcept>
namespace evrm {
namespace {
void check(VkResult result,const char* reason){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(reason)+" "+std::to_string(result));}
template<class T>T info(VkStructureType type){T result{};result.sType=type;return result;}
struct AssetContext {
    VkDevice device{};PFN_vkGetDeviceProcAddr get{};VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t image_dimension_limit=0;bool* quarantine=nullptr;
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
    using ImageKey=std::pair<int,VkFormat>;
    std::shared_ptr<AssetContext> c;std::shared_ptr<const vrm::Avatar> source;std::shared_ptr<NativeSkinGpuAsset> skin;
    std::shared_ptr<const NativeSkinAsset> skin_source;
    std::shared_ptr<NativeSkinningGpu> skinning_owner;
    NativeSurfaceSelection selection;
    // Only independently owned leaves are shared; a new view never retains an old asset bank.
    std::map<ImageKey,std::shared_ptr<AssetImage>> images;
    std::vector<VkSampler> samplers;
    std::vector<VkDescriptorImageInfo> materials;
    std::vector<std::array<VkDescriptorImageInfo,3>> surfaces;
    ~Impl(){if(!c->uncertain())for(auto sampler:samplers)if(sampler)c->fn<PFN_vkDestroySampler>("vkDestroySampler")(c->device,sampler,nullptr);}
};
NativeAvatarGpuAssets::NativeAvatarGpuAssets(std::shared_ptr<Impl> value):impl_(std::move(value)){}
NativeAvatarGpuAssets::~NativeAvatarGpuAssets()=default;
const std::shared_ptr<const vrm::Avatar>& NativeAvatarGpuAssets::avatar()const noexcept{return impl_->source;}
const std::shared_ptr<NativeSkinGpuAsset>& NativeAvatarGpuAssets::skin()const noexcept{return impl_->skin;}
const NativeSurfaceSelection& NativeAvatarGpuAssets::surface_selection()const noexcept{return impl_->selection;}
VkDescriptorBufferInfo NativeAvatarGpuAssets::surface_vertices()const noexcept{return impl_->skin?impl_->skin->surface():VkDescriptorBufferInfo{};}
VkDescriptorImageInfo NativeAvatarGpuAssets::material(std::size_t index)const noexcept {
    return impl_->materials.empty()?VkDescriptorImageInfo{}:impl_->materials[std::min(index,impl_->materials.size()-1)];
}
VkDescriptorImageInfo NativeAvatarGpuAssets::surface_material(std::size_t index,NativeSurfaceTexture role)const noexcept {
    const auto slot=static_cast<unsigned>(role);
    if(impl_->surfaces.empty()||slot>=3)return {};
    return impl_->surfaces[std::min(index,impl_->surfaces.size()-1)][slot];
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
    std::shared_ptr<const vrm::Avatar> failed_avatar;std::shared_ptr<const NativeSkinAsset> failed_skin;
    NativeSurfaceSelection failed_selection;
    void clear_failure()noexcept{
        failed_generation=0;failed_avatar.reset();failed_skin.reset();
        failed_selection.features.clear();failed_selection.combined=0;
    }
    Impl(VkDevice device,PFN_vkGetDeviceProcAddr get,PFN_vkSetDeviceLoaderData set,const VkPhysicalDeviceMemoryProperties& memory,std::uint32_t image_limit,std::shared_ptr<NativeSkinningGpu> gpu,bool* quarantine):
        c(std::make_shared<AssetContext>(AssetContext{device,get,memory,image_limit,quarantine})),loader(set),skinning(std::move(gpu)){
        if(!image_limit)throw std::invalid_argument("Avatar device image dimension limit unavailable");
    }
    void begin(VkQueue queue,std::uint32_t family,std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& avatar,const std::shared_ptr<const NativeSkinAsset>& skin,const NativeSurfaceSelection& selection,const std::shared_ptr<NativeAvatarGpuAssets>& reuse){
        if(!native_surface_valid_selection(*avatar,selection))throw std::runtime_error("Avatar surface selection invalid");
        if(avatar->materials.size()>4096||avatar->images.size()>4096)throw std::runtime_error("Avatar material budget exceeded");
        const auto validate_image=[&](const vrm::Image& image){
            if(image.width<=0||image.height<=0)throw std::runtime_error("Avatar texture dimensions invalid");
            if(std::uint32_t(image.width)>c->image_dimension_limit||std::uint32_t(image.height)>c->image_dimension_limit)
                throw std::runtime_error("Avatar texture dimensions exceed device limit");
            const auto width=std::size_t(image.width),height=std::size_t(image.height);
            if(width>std::numeric_limits<std::size_t>::max()/4/height||image.rgba.size()!=width*height*4)
                throw std::runtime_error("Avatar texture dimensions invalid");
        };
        const auto validate_texture=[&](int index){
            if(index<0)return;
            const auto& texture=avatar->textures.at(std::size_t(index));
            if(texture.image<0)throw std::runtime_error("Avatar texture source invalid");
            validate_image(avatar->images.at(std::size_t(texture.image)));
        };
        // Validate only the images this selection will upload, before any
        // VkImage creation. Fast may leave an oversized optional map unused.
        for(std::size_t n=0;n<avatar->materials.size();++n){
            const auto& material=avatar->materials[n];validate_texture(material.base_texture);
            const auto flags=selection.features[n];if(!flags)continue;
            const auto& surface=*material.surface;
            if(flags&surface_normal)validate_texture(surface.normal.texture);
            if(flags&surface_matcap)validate_texture(surface.matcap.texture);
            if(flags&surface_mask)validate_texture(surface.rim.texture);
        }
        job=std::make_unique<Job>();auto& out=*job;out.c=c;out.queue=queue;out.family=family;out.generation=generation;out.skin_source=skin;
        auto data=std::make_shared<NativeAvatarGpuAssets::Impl>();data->c=c;data->source=avatar;data->selection=selection;data->skin_source=skin;data->skinning_owner=skin?skinning:nullptr;
        const auto old=reuse&&reuse->avatar()==avatar&&reuse->impl_->c->device==c->device&&reuse->impl_->c->get==c->get&&reuse->impl_->c->quarantine==c->quarantine?reuse->impl_:nullptr;
        out.assets=std::shared_ptr<NativeAvatarGpuAssets>(new NativeAvatarGpuAssets(data));
        auto pi=info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        check(c->fn<PFN_vkCreateCommandPool>("vkCreateCommandPool")(c->device,&pi,nullptr,&out.pool),"Avatar upload pool");
        auto ai=info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);ai.commandPool=out.pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
        check(c->fn<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(c->device,&ai,&out.command),"Avatar upload command");if(loader)check(loader(c->device,out.command),"Avatar upload loader data");
        auto fi=info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);check(c->fn<PFN_vkCreateFence>("vkCreateFence")(c->device,&fi,nullptr,&out.fence),"Avatar upload fence");
        auto bi=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(c->fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(out.command,&bi),"Avatar upload begin");
        if(skin){if(!skinning)throw std::runtime_error("Avatar GPU skinning unavailable");
            if(selection.needs_tangents())skinning->prepare_surface();
            const auto skin_reuse=old&&old->skin_source==skin&&old->skinning_owner==skinning?old->skin:nullptr;
            data->skin=skinning->asset(skin,selection.needs_vertices(),skin_reuse);skinning->upload(out.command,data->skin);}
        const vrm::Image white{1,1,{255,255,255,255}};
        const auto add_image=[&](int source_index,VkFormat format){
            const NativeAvatarGpuAssets::Impl::ImageKey key{source_index,format};
            if(data->images.count(key))return;
            if(old){const auto found=old->images.find(key);if(found!=old->images.end()){data->images.emplace(key,found->second);return;}}
            const auto* source=source_index<0?&white:&avatar->images.at(source_index);
            validate_image(*source);
            auto image=std::make_shared<AssetImage>();image->c=c;
            auto ci=info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={std::uint32_t(source->width),std::uint32_t(source->height),1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
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
            data->images.emplace(key,std::move(image));out.staging.push_back(std::move(buffer));
        };
        add_image(-1,VK_FORMAT_R8G8B8A8_SRGB);
        const auto wrap=[](int value){return value==33071?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:value==33648?VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:VK_SAMPLER_ADDRESS_MODE_REPEAT;};
        const auto descriptor=[&](int index,VkFormat format){
            const auto* texture=index<0?nullptr:&avatar->textures.at(index);
            if(texture&&texture->image<0)throw std::runtime_error("Avatar texture source invalid");
            const auto source=texture?texture->image:-1;add_image(source,format);
            auto sampler=info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);sampler.magFilter=texture&&texture->mag_filter==9728?VK_FILTER_NEAREST:VK_FILTER_LINEAR;sampler.minFilter=texture&&(texture->min_filter==9728||texture->min_filter==9984||texture->min_filter==9986)?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
            sampler.addressModeU=texture?wrap(texture->wrap_s):VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.addressModeV=texture?wrap(texture->wrap_t):VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            VkSampler handle{};check(c->fn<PFN_vkCreateSampler>("vkCreateSampler")(c->device,&sampler,nullptr,&handle),"Avatar sampler");
            try{data->samplers.push_back(handle);}catch(...){c->fn<PFN_vkDestroySampler>("vkDestroySampler")(c->device,handle,nullptr);throw;}
            return VkDescriptorImageInfo{handle,data->images.at({source,format})->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        };
        for(std::size_t n=0;n<=avatar->materials.size();++n){
            data->materials.push_back(descriptor(n<avatar->materials.size()?avatar->materials[n].base_texture:-1,VK_FORMAT_R8G8B8A8_SRGB));
            data->surfaces.emplace_back();
        }
        const auto fallback=data->materials.back();
        for(auto& maps:data->surfaces)maps.fill(fallback);
        for(std::size_t n=0;n<avatar->materials.size();++n){
            const auto flags=selection.features[n];if(!flags)continue;
            const auto& surface=*avatar->materials[n].surface;auto& maps=data->surfaces[n];
            if(flags&surface_normal)maps[0]=descriptor(surface.normal.texture,VK_FORMAT_R8G8B8A8_UNORM);
            if(flags&surface_matcap)maps[1]=descriptor(surface.matcap.texture,VK_FORMAT_R8G8B8A8_SRGB);
            if(flags&surface_mask)maps[2]=descriptor(surface.rim.texture,VK_FORMAT_R8G8B8A8_SRGB);
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
NativeAvatarAssetStager::NativeAvatarAssetStager(VkDevice device,PFN_vkGetDeviceProcAddr get,PFN_vkSetDeviceLoaderData loader,const VkPhysicalDeviceMemoryProperties& memory,std::uint32_t image_dimension_limit,std::shared_ptr<NativeSkinningGpu> skinning,bool* quarantine):
    impl_(std::make_unique<Impl>(device,get,loader,memory,image_dimension_limit,std::move(skinning),quarantine)){}
NativeAvatarAssetStager::~NativeAvatarAssetStager()=default;
NativeAvatarUploadState NativeAvatarAssetStager::update(VkQueue queue,std::uint32_t family,std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& avatar,const std::shared_ptr<const NativeSkinAsset>& skin)noexcept {
    try{NativeSurfaceSelection basic;if(avatar)basic.features.resize(avatar->materials.size()+1);
        return update(queue,family,generation,avatar,skin,basic);
    }catch(...){impl_->error="Avatar GPU selection allocation failed";impl_->state=NativeAvatarUploadState::Failed;return impl_->state;}
}
NativeAvatarUploadState NativeAvatarAssetStager::update(VkQueue queue,std::uint32_t family,std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& avatar,const std::shared_ptr<const NativeSkinAsset>& skin,const NativeSurfaceSelection& selection,const std::shared_ptr<NativeAvatarGpuAssets>& reuse)noexcept {
    try{
        if(impl_->c->uncertain()){impl_->state=NativeAvatarUploadState::Failed;return impl_->state;}
        if(impl_->job){auto& job=*impl_->job;
            if(!generation||job.assets->avatar()!=avatar||job.skin_source!=skin||job.assets->surface_selection()!=selection||job.queue!=queue||job.family!=family)job.cancelled=true;
            // A generation-only refresh can adopt identical immutable work.
            if(!job.cancelled)job.generation=generation;
            if(!impl_->poll()){impl_->state=NativeAvatarUploadState::Pending;return impl_->state;}
            if(job.cancelled){impl_->job.reset();impl_->state=NativeAvatarUploadState::Idle;}
            else{impl_->state=NativeAvatarUploadState::Ready;return impl_->state;}}
        if(!generation||!avatar){impl_->clear_failure();impl_->state=NativeAvatarUploadState::Idle;return impl_->state;}
        if(impl_->state==NativeAvatarUploadState::Failed&&impl_->failed_generation==generation&&impl_->failed_avatar==avatar&&impl_->failed_skin==skin&&impl_->failed_selection==selection)return impl_->state;
        impl_->clear_failure();impl_->error.clear();impl_->begin(queue,family,generation,avatar,skin,selection,reuse);impl_->state=NativeAvatarUploadState::Pending;return impl_->state;
    }catch(const std::exception& error){impl_->error=error.what();}catch(...){impl_->error="Avatar GPU upload failed";}
    impl_->failed_generation=generation;impl_->failed_avatar=avatar;impl_->failed_skin=skin;
    try{impl_->failed_selection=selection;}catch(...){impl_->failed_generation=0;}
    impl_->state=NativeAvatarUploadState::Failed;
    if(impl_->job&&!impl_->job->submitted)impl_->job.reset();
    return impl_->state;
}
std::shared_ptr<NativeAvatarGpuAssets> NativeAvatarAssetStager::ready(std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& asset)const noexcept {
    return impl_->state==NativeAvatarUploadState::Ready&&impl_->job&&!impl_->job->cancelled&&impl_->job->generation==generation&&impl_->job->assets->avatar()==asset?impl_->job->assets:nullptr;
}
std::shared_ptr<NativeAvatarGpuAssets> NativeAvatarAssetStager::ready(std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& asset,const NativeSurfaceSelection& selection)const noexcept {
    auto result=ready(generation,asset);return result&&result->surface_selection()==selection?result:nullptr;
}
std::shared_ptr<NativeAvatarGpuAssets> NativeAvatarAssetStager::take_ready(std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& asset)noexcept {
    auto result=ready(generation,asset);if(result){impl_->job.reset();impl_->clear_failure();impl_->state=NativeAvatarUploadState::Idle;}return result;
}
std::shared_ptr<NativeAvatarGpuAssets> NativeAvatarAssetStager::take_ready(std::uint64_t generation,const std::shared_ptr<const vrm::Avatar>& asset,const NativeSurfaceSelection& selection)noexcept {
    auto result=ready(generation,asset,selection);if(result){impl_->job.reset();impl_->clear_failure();impl_->state=NativeAvatarUploadState::Idle;}return result;
}
void NativeAvatarAssetStager::cancel()noexcept{impl_->clear_failure();if(impl_->job)impl_->job->cancelled=true;else if(impl_->state==NativeAvatarUploadState::Failed)impl_->state=NativeAvatarUploadState::Idle;}
const std::string& NativeAvatarAssetStager::error()const noexcept{return impl_->error;}
void NativeAvatarAssetStager::device_idle_complete()noexcept{if(impl_->job)impl_->job->submitted=false;impl_->job.reset();impl_->clear_failure();impl_->state=NativeAvatarUploadState::Idle;}
}
