#include "native_volume_snapshot.hpp"
#include "native_private_idle.hpp"
#include <mutex>
#include <stdexcept>
namespace evrm { namespace {
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void checked(VkResult r,const char* why){require(r==VK_SUCCESS,why);}
bool power_two(VkDeviceSize n){return n&&!(n&(n-1));}
bool pure(VkMemoryPropertyFlags f){return (f&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)&&!(f&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT));}
void validate(const std::array<NativeVolumeSnapshotSource,2>& s,const NativeVolumeSnapshotCapabilities& c){
    const auto e=s[0].extent;const std::uint64_t voxels=std::uint64_t(e.width)*e.height*e.depth;
    require(e.width&&e.height&&e.depth&&e.width<=1024&&e.height<=1024&&e.depth<=128&&voxels<=NativeVolumeSnapshot::voxel_limit,"Snapshot extent/budget");
    require(s[0].image&&s[1].image&&s[0].image!=s[1].image&&s[0].generation&&s[1].generation,"Snapshot image identity");
    for(const auto& a:s)require(a.type==VK_IMAGE_TYPE_3D&&a.extent.width==e.width&&a.extent.height==e.height&&a.extent.depth==e.depth&&a.samples==VK_SAMPLE_COUNT_1_BIT&&a.mip_levels==1&&a.array_layers==1&&a.usage==(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT),"Snapshot source shape");
    require(s[0].format==VK_FORMAT_R32_UINT&&s[0].flags==VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT&&s[0].format_count==2,"Snapshot mutable R32 source");
    require((s[0].formats[0]==VK_FORMAT_R32_UINT&&s[0].formats[1]==VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)||(s[0].formats[1]==VK_FORMAT_R32_UINT&&s[0].formats[0]==VK_FORMAT_E5B9G9R9_UFLOAT_PACK32),"Snapshot allowed packed view");
    require(s[1].format==VK_FORMAT_R16_UNORM&&!s[1].flags&&!s[1].format_count,"Snapshot visibility source");
    require((c.formats[0]&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)&&(c.formats[1]&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),"Snapshot source sampling unsupported");
    constexpr VkFormatFeatureFlags output=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    for(unsigned i=0;i<2;++i){const auto required=output|(i?VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT:0);const auto& p=c.outputs[i];require((c.formats[i+2]&required)==required&&p.maxExtent.width>=e.width&&p.maxExtent.height>=e.height&&p.maxExtent.depth>=e.depth&&p.maxMipLevels&&p.maxArrayLayers&&(p.sampleCounts&VK_SAMPLE_COUNT_1_BIT)&&p.maxResourceSize>=voxels*4,"Snapshot output format/size unsupported");}
    constexpr VkFormatFeatureFlags sampled=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    require((c.formats[4]&sampled)==sampled,"Snapshot packed sampled format unsupported");
    const auto& l=c.limits;require(l.maxImageDimension3D>=e.width&&l.maxImageDimension3D>=e.height&&l.maxImageDimension3D>=e.depth&&l.maxBoundDescriptorSets&&l.maxPerStageDescriptorSampledImages>=2&&l.maxDescriptorSetSampledImages>=2&&l.maxPerStageDescriptorStorageImages>=2&&l.maxDescriptorSetStorageImages>=2&&l.maxPerStageResources>=4&&l.maxComputeWorkGroupInvocations>=64,"Snapshot device limits");
    const std::array<std::uint32_t,3> dimensions{e.width,e.height,e.depth};for(unsigned i=0;i<3;++i)require(l.maxComputeWorkGroupSize[i]>=4&&l.maxComputeWorkGroupCount[i]>=(dimensions[i]+3)/4,"Snapshot compute limits");
    require(c.memory.memoryTypeCount&&c.memory.memoryTypeCount<=VK_MAX_MEMORY_TYPES&&c.memory.memoryHeapCount&&c.memory.memoryHeapCount<=VK_MAX_MEMORY_HEAPS,"Snapshot memory metadata");bool found=false;
    for(unsigned i=0;i<c.memory.memoryTypeCount;++i)if(pure(c.memory.memoryTypes[i].propertyFlags)&&c.memory.memoryTypes[i].heapIndex<c.memory.memoryHeapCount)found=true;
    require(found,"Snapshot pure device-local memory unavailable");
}
}
#define SNAPSHOT_FUNCTIONS(X) \
X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) \
X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
X(vkCmdPipelineBarrier) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch)
namespace {
struct SnapshotDriver {
#define DECLARE(n) PFN_##n n{};
    SNAPSHOT_FUNCTIONS(DECLARE)
#undef DECLARE
    SnapshotDriver(VkDevice device,PFN_vkGetDeviceProcAddr get){
#define LOAD(n) n=reinterpret_cast<PFN_##n>(get(device,#n));require(n!=nullptr,"Snapshot missing driver entry");
        SNAPSHOT_FUNCTIONS(LOAD)
#undef LOAD
    }
};
// Source-free set layout, pipeline layout, shader and pipeline. Built in a
// second step so a failed call retires the earlier handles in the destructor.
struct SnapshotProgram {
    VkDevice device{};std::shared_ptr<const SnapshotDriver> driver;const bool* parent=nullptr;bool uncertain=false;
    VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};
    bool quarantined()const noexcept{return uncertain||(parent&&*parent);}
    void build(const std::uint32_t* words,std::size_t word_count){const auto& d=*driver;
        const std::array<VkDescriptorSetLayoutBinding,4> bindings={{{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
        VkDescriptorSetLayoutCreateInfo set{};set.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set.bindingCount=4;set.pBindings=bindings.data();checked(d.vkCreateDescriptorSetLayout(device,&set,nullptr,&dsl),"Snapshot private DSL");
        VkPipelineLayoutCreateInfo pipeline_layout{};pipeline_layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pipeline_layout.setLayoutCount=1;pipeline_layout.pSetLayouts=&dsl;checked(d.vkCreatePipelineLayout(device,&pipeline_layout,nullptr,&layout),"Snapshot private layout");
        VkShaderModuleCreateInfo module{};module.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;module.codeSize=word_count*4;module.pCode=words;checked(d.vkCreateShaderModule(device,&module,nullptr,&shader),"Snapshot private shader");
        VkComputePipelineCreateInfo compute{};compute.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;compute.layout=layout;compute.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;compute.stage.module=shader;compute.stage.pName="cs_snapshot";checked(d.vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline),"Snapshot private compute pipeline");
    }
    ~SnapshotProgram(){if(quarantined()||!driver)return;const auto& d=*driver;
        if(pipeline)d.vkDestroyPipeline(device,pipeline,nullptr);
        if(shader)d.vkDestroyShaderModule(device,shader,nullptr);
        if(layout)d.vkDestroyPipelineLayout(device,layout,nullptr);
        if(dsl)d.vkDestroyDescriptorSetLayout(device,dsl,nullptr);
    }
};
// The two owned output images with memory and views, and the descriptor pool
// and set that name them. Contents are never carried between snapshots.
struct SnapshotStorage {
    VkDevice device{};std::shared_ptr<const SnapshotDriver> driver;const bool* parent=nullptr;bool uncertain=false;
    VkExtent3D extent{};VkDescriptorSetLayout dsl{};VkDeviceSize allocated=0;
    std::array<VkImage,2> images{};std::array<VkDeviceMemory,2> memory{};std::array<VkImageView,2> views{};VkImageView radiance_view{};
    VkDescriptorPool pool{};VkDescriptorSet set{};
    bool quarantined()const noexcept{return uncertain||(parent&&*parent);}
    void build_images(const NativeVolumeSnapshotCapabilities& caps){const auto& d=*driver;
        for(unsigned i=0;i<2;++i){
            const std::array<VkFormat,2> formats={VK_FORMAT_R32_UINT,VK_FORMAT_E5B9G9R9_UFLOAT_PACK32};
            VkImageFormatListCreateInfo list{};list.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;list.viewFormatCount=static_cast<std::uint32_t>(formats.size());list.pViewFormats=formats.data();
            VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.pNext=i?nullptr:&list;image.flags=i?0:VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;image.imageType=VK_IMAGE_TYPE_3D;image.format=i?VK_FORMAT_R32_SFLOAT:VK_FORMAT_R32_UINT;image.extent=extent;image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;checked(d.vkCreateImage(device,&image,nullptr,&images[i]),"Snapshot owned image");
            VkMemoryRequirements r{};d.vkGetImageMemoryRequirements(device,images[i],&r);require(r.size&&r.size<=NativeVolumeSnapshot::memory_byte_limit-allocated&&power_two(r.alignment)&&r.alignment<=NativeVolumeSnapshot::memory_byte_limit,"Snapshot allocation budget/requirements");allocated+=r.size;
            unsigned type=VK_MAX_MEMORY_TYPES;for(unsigned n=0;n<caps.memory.memoryTypeCount;++n){const auto& t=caps.memory.memoryTypes[n];if((r.memoryTypeBits&(1u<<n))&&pure(t.propertyFlags)&&t.heapIndex<caps.memory.memoryHeapCount&&caps.memory.memoryHeaps[t.heapIndex].size>=r.size){type=n;break;}}
            require(type<VK_MAX_MEMORY_TYPES,"Snapshot owned memory type");VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=r.size;a.memoryTypeIndex=type;checked(d.vkAllocateMemory(device,&a,nullptr,&memory[i]),"Snapshot owned allocation");checked(d.vkBindImageMemory(device,images[i],memory[i],0),"Snapshot owned binding");
            VkImageViewCreateInfo v{};v.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;v.image=images[i];v.viewType=VK_IMAGE_VIEW_TYPE_3D;v.format=image.format;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};checked(d.vkCreateImageView(device,&v,nullptr,&views[i]),"Snapshot owned output view");
            if(!i){VkImageViewUsageCreateInfo use{};use.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;use.usage=VK_IMAGE_USAGE_SAMPLED_BIT;v.pNext=&use;v.format=VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;checked(d.vkCreateImageView(device,&v,nullptr,&radiance_view),"Snapshot packed sampled output view");}
        }
    }
    void build_set(VkDescriptorSetLayout layout){const auto& d=*driver;
        const std::array<VkDescriptorPoolSize,2> sizes={{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2}}};VkDescriptorPoolCreateInfo create{};create.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;create.maxSets=1;create.poolSizeCount=2;create.pPoolSizes=sizes.data();checked(d.vkCreateDescriptorPool(device,&create,nullptr,&pool),"Snapshot private pool");
        VkDescriptorSetAllocateInfo sets{};sets.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;sets.descriptorPool=pool;sets.descriptorSetCount=1;sets.pSetLayouts=&layout;checked(d.vkAllocateDescriptorSets(device,&sets,&set),"Snapshot private descriptor set");dsl=layout;
    }
    void release_pool()noexcept{if(pool)driver->vkDestroyDescriptorPool(device,pool,nullptr);pool={};set={};}
    // Unchanged order: the packed view, then per image the caller's source
    // view (when given), the owned view, the image and its memory.
    void release_images(std::array<VkImageView,2>* sources)noexcept{const auto& d=*driver;
        if(radiance_view)d.vkDestroyImageView(device,radiance_view,nullptr);radiance_view={};
        for(unsigned i=0;i<2;++i){if(sources&&(*sources)[i]){d.vkDestroyImageView(device,(*sources)[i],nullptr);(*sources)[i]={};}
            if(views[i])d.vkDestroyImageView(device,views[i],nullptr);if(images[i])d.vkDestroyImage(device,images[i],nullptr);if(memory[i])d.vkFreeMemory(device,memory[i],nullptr);
            views[i]={};images[i]={};memory[i]={};}
    }
    ~SnapshotStorage(){if(quarantined()||!driver)return;release_pool();release_images(nullptr);}
};
}
struct NativeVolumeSnapshotReuse::Impl {
    const bool* parent=nullptr;std::mutex mutex;VkDevice device{};PFN_vkGetDeviceProcAddr get{};
    std::shared_ptr<const SnapshotDriver> driver;std::shared_ptr<SnapshotProgram> program;std::atomic<std::uint64_t> created{0},reissued{0};
    NativePrivateIdle<SnapshotStorage,NativeVolumeSnapshotReuse::idle_limit> idle;
    Impl(const bool* p,NativeVolumeSnapshotReuse::Clock clock):parent(p),idle(NativeVolumeSnapshotReuse::idle_milliseconds,clock){}
    bool quarantined()const noexcept{return parent&&*parent;}
    // One driver table and one program per device, made on first use.
    std::shared_ptr<const SnapshotDriver> driver_for(VkDevice d,PFN_vkGetDeviceProcAddr g){
        std::lock_guard<std::mutex> lock(mutex);
        require(!quarantined()&&(!device||(device==d&&get==g)),"Snapshot reuse owner");
        if(!driver){auto loaded=std::make_shared<SnapshotDriver>(d,g);device=d;get=g;driver=std::move(loaded);}
        return driver;
    }
    std::shared_ptr<SnapshotProgram> program_for(const std::uint32_t* words,std::size_t word_count){
        std::lock_guard<std::mutex> lock(mutex);
        require(driver&&!quarantined()&&(!program||!program->quarantined()),"Snapshot reuse program");
        if(!program){auto built=std::make_shared<SnapshotProgram>();built->device=device;built->driver=driver;built->parent=parent;built->build(words,word_count);program=std::move(built);}
        return program;
    }
};
NativeVolumeSnapshotReuse::NativeVolumeSnapshotReuse(const bool* parent_quarantine,Clock clock):impl(std::make_shared<Impl>(parent_quarantine,clock)){}
NativeVolumeSnapshotReuse::~NativeVolumeSnapshotReuse(){retain(false);}
// Entries leave the holder before they are destroyed, so no Vulkan call runs
// under its lock. A quarantined entry's destructor issues no Vulkan call.
void NativeVolumeSnapshotReuse::retain(bool keep)noexcept{if(impl){auto entries=impl->idle.drain(keep);for(auto& entry:entries)entry.reset();}}
void NativeVolumeSnapshotReuse::release_expired()noexcept{if(impl&&impl->idle.size()){auto entries=impl->idle.expire();for(auto& entry:entries)entry.reset();}}
std::size_t NativeVolumeSnapshotReuse::idle_storages()const noexcept{return impl?impl->idle.size():0;}
VkDeviceSize NativeVolumeSnapshotReuse::idle_bytes()const noexcept{VkDeviceSize bytes=0;if(impl)impl->idle.visit([&](const SnapshotStorage& s){bytes+=s.allocated;});return bytes;}
bool NativeVolumeSnapshotReuse::try_idle(std::size_t& storages,VkDeviceSize& bytes)const noexcept{storages=0;bytes=0;if(!impl)return true;
    std::size_t count=0;VkDeviceSize sum=0;if(!impl->idle.try_visit([&](const SnapshotStorage& s){++count;sum+=s.allocated;}))return false;
    storages=count;bytes=sum;return true;}
#ifdef EVRM_PRIVATE_REUSE_TESTING
void NativeVolumeSnapshotReuse::test_hold_idle(void(*inside)(void*),void* context)const{impl->idle.test_hold([&]{inside(context);});}
#endif
std::uint64_t NativeVolumeSnapshotReuse::created_storages()const noexcept{return impl?impl->created.load(std::memory_order_relaxed):0;}
std::uint64_t NativeVolumeSnapshotReuse::reissued_storages()const noexcept{return impl?impl->reissued.load(std::memory_order_relaxed):0;}
struct NativeVolumeSnapshot::Impl {
    VkDevice device{};VkExtent3D extent{};std::uint32_t set_limit=0;bool recorded=false,failed=false,uncertain=false;
    std::shared_ptr<const SnapshotDriver> driver;std::shared_ptr<SnapshotProgram> program;std::unique_ptr<SnapshotStorage> storage;
    std::array<VkImageView,2> source_views{};std::shared_ptr<NativeVolumeSnapshotReuse::Impl> reuse;
    ~Impl(){
        // Uncertain completion: no Vulkan teardown, and nothing is offered for
        // reuse. The marks are sticky for the storage and the shared program.
        if(uncertain||(reuse&&reuse->quarantined())){if(storage)storage->uncertain=true;if(program)program->uncertain=true;return;}
        if(!driver)return;const auto& d=*driver;
        if(reuse){
            for(auto view:source_views)if(view)d.vkDestroyImageView(device,view,nullptr);
            // This is the existing destruction point: the primary recording
            // and every consumer of the outputs have already retired. Whatever
            // the holder does not keep is destroyed here, outside its lock.
            if(storage&&storage->set){auto unkept=reuse->idle.keep(std::move(storage));unkept.reset();}
            return;
        }
        if(storage)storage->release_pool();
        program.reset();
        if(storage)storage->release_images(&source_views);
        else for(auto view:source_views)if(view)d.vkDestroyImageView(device,view,nullptr);
    }
};
NativeVolumeSnapshot::NativeVolumeSnapshot(VkDevice device,PFN_vkGetDeviceProcAddr get,const std::array<NativeVolumeSnapshotSource,2>& sources,const NativeVolumeSnapshotCapabilities& caps,const std::uint32_t* words,std::size_t word_count,std::shared_ptr<NativeVolumeSnapshotReuse> reuse){
    validate(sources,caps);require(device&&get&&words&&word_count>=5&&word_count<=4*1024*1024,"Snapshot shader/driver inputs");require(words[0]==0x07230203,"Snapshot shader header");
    impl=std::make_unique<Impl>();auto& p=*impl;p.device=device;p.extent=sources[0].extent;p.set_limit=caps.limits.maxBoundDescriptorSets<32?caps.limits.maxBoundDescriptorSets:32;
    if(reuse&&reuse->impl){p.reuse=reuse->impl;p.driver=p.reuse->driver_for(device,get);}
    else p.driver=std::make_shared<SnapshotDriver>(device,get);
    const auto& d=*p.driver;
    for(unsigned i=0;i<2;++i){VkImageViewUsageCreateInfo use{};use.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;use.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageViewCreateInfo v{};v.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;v.pNext=&use;v.image=sources[i].image;v.viewType=VK_IMAGE_VIEW_TYPE_3D;v.format=i?VK_FORMAT_R16_UNORM:VK_FORMAT_R32_UINT;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};checked(d.vkCreateImageView(device,&v,nullptr,&p.source_views[i]),"Snapshot sampled source view");}
    // Exact extent only: reissued storage is what build_images would create.
    const auto fits=[&](const SnapshotStorage& s){return s.device==device&&s.extent.width==p.extent.width&&s.extent.height==p.extent.height&&s.extent.depth==p.extent.depth;};
    if(p.reuse)p.storage=p.reuse->idle.take([&](const SnapshotStorage& s){return !s.quarantined()&&fits(s);});
    const bool reissued=static_cast<bool>(p.storage);
    // Nothing idle fits. Idle storage of another extent is destroyed before
    // its replacement is allocated, so a resize never needs both at once, as
    // before reuse. Idle storage has no live lease; storage in flight is not
    // in the holder; quarantined storage is left alone. Destroyed outside the
    // holder's lock.
    if(!reissued&&p.reuse){auto stale=p.reuse->idle.evict([&](const SnapshotStorage& s){return !s.quarantined()&&!fits(s);});for(auto& entry:stale)entry.reset();}
    if(!reissued){p.storage=std::make_unique<SnapshotStorage>();auto& s=*p.storage;s.device=device;s.driver=p.driver;s.parent=p.reuse?p.reuse->parent:nullptr;s.extent=p.extent;s.build_images(caps);}
    if(p.reuse)p.program=p.reuse->program_for(words,word_count);
    else{p.program=std::make_shared<SnapshotProgram>();p.program->device=device;p.program->driver=p.driver;p.program->build(words,word_count);}
    if(!reissued)p.storage->build_set(p.program->dsl);
    require(p.storage->dsl==p.program->dsl&&p.storage->set,"Snapshot storage layout");
    // All four bindings are written for every snapshot: the source views are
    // new each time and the set may come from an earlier snapshot.
    const auto& s=*p.storage;
    const std::array<VkDescriptorImageInfo,4> info={{{nullptr,p.source_views[0],VK_IMAGE_LAYOUT_GENERAL},{nullptr,p.source_views[1],VK_IMAGE_LAYOUT_GENERAL},{nullptr,s.views[0],VK_IMAGE_LAYOUT_GENERAL},{nullptr,s.views[1],VK_IMAGE_LAYOUT_GENERAL}}};std::array<VkWriteDescriptorSet,4> writes{};
    for(unsigned i=0;i<4;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=s.set;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=i<2?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;w.pImageInfo=&info[i];}d.vkUpdateDescriptorSets(device,4,writes.data(),0,nullptr);
    if(p.reuse)(reissued?p.reuse->reissued:p.reuse->created).fetch_add(1,std::memory_order_relaxed);
}
NativeVolumeSnapshot::~NativeVolumeSnapshot()=default;
bool NativeVolumeSnapshot::record(VkCommandBuffer command,VkPipeline original,VkPipelineLayout compatible,std::uint32_t count,const VkDescriptorSet* sets,std::uint32_t x,std::uint32_t y,std::uint32_t z)noexcept{
    if(!impl)return false;
    auto& p=*impl;
    if(p.failed||p.recorded||p.uncertain||!command||!original||!compatible||!count||count>p.set_limit||!sets||x!=(p.extent.width+3)/4||y!=(p.extent.height+3)/4||z!=(p.extent.depth+3)/4){p.failed=true;return false;}
    for(unsigned i=0;i<count;++i)if(!sets[i]){p.failed=true;return false;}
    p.recorded=true;const auto& d=*p.driver;const auto& s=*p.storage;
    std::array<VkImageMemoryBarrier,2> images{};for(unsigned i=0;i<2;++i){auto& b=images[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=s.images[i];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
    d.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,2,images.data());
    VkMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    d.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    d.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.program->pipeline);d.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.program->layout,0,1,&s.set,0,nullptr);d.vkCmdDispatch(command,x,y,z);
    barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    d.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    for(auto& b:images){b.oldLayout=VK_IMAGE_LAYOUT_GENERAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;}
    d.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,2,images.data());
    d.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,original);d.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,compatible,0,count,sets,0,nullptr);return true;
}
std::array<VkDescriptorImageInfo,2> NativeVolumeSnapshot::outputs()const noexcept{
    if(!impl||!impl->storage)return {};
    return {{{nullptr,impl->storage->radiance_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{nullptr,impl->storage->views[1],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
}
void NativeVolumeSnapshot::quarantine()noexcept{if(impl)impl->uncertain=true;}
#undef SNAPSHOT_FUNCTIONS
}
