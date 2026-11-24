#include "native_mtoon_frame.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace evrm {namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
bool power_two(VkDeviceSize n)noexcept{return n&&!(n&(n-1));}
VkDeviceSize aligned(VkDeviceSize n,VkDeviceSize a){require(power_two(a)&&n<=~VkDeviceSize(0)-(a-1),"MToon frame alignment");return (n+a-1)&~(a-1);}
#define FRAME_FUNCTIONS(X) \
X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCmdFillBuffer) X(vkCmdUpdateBuffer) X(vkCmdPipelineBarrier) X(vkCmdCopyBuffer)
struct FrameDriver {
#define DECLARE(n) PFN_##n n{};
    FRAME_FUNCTIONS(DECLARE)
#undef DECLARE
    PFN_vkResetDescriptorPool vkResetDescriptorPool{}; // Optional reuse; creation remains the fallback.
    FrameDriver(VkDevice d,PFN_vkGetDeviceProcAddr get){
#define LOAD(n) n=reinterpret_cast<PFN_##n>(get(d,#n));require(n!=nullptr,"MToon frame driver entry");
        FRAME_FUNCTIONS(LOAD)
#undef LOAD
        vkResetDescriptorPool=reinterpret_cast<PFN_vkResetDescriptorPool>(get(d,"vkResetDescriptorPool"));
    }
};
struct FrameBank {
    VkDevice device{};std::shared_ptr<FrameDriver> driver;bool uncertain=false;
    NativeMToonProfileQueries profile;
    std::array<VkBuffer,3> buffers{};std::array<VkDeviceMemory,3> memory{};void* mapped=nullptr;
    std::array<VkDeviceSize,3> capacities{};VkDeviceSize allocated=0;
    VkDescriptorPool descriptors{};std::array<std::uint32_t,5> descriptor_counts{};
    // Work storage of this slot's last frame. A slot is reissued only after its final frame lease retired, so that frame's work object is gone.
    std::shared_ptr<NativeMToonWorkStorage> work;
    ~FrameBank(){if(uncertain){profile.quarantine();native_mtoon_work_storage_quarantine(work);}if(uncertain||!driver)return;if(descriptors)driver->vkDestroyDescriptorPool(device,descriptors,nullptr);if(mapped)driver->vkUnmapMemory(device,memory[2]);for(auto b:buffers)if(b)driver->vkDestroyBuffer(device,b,nullptr);for(auto m:memory)if(m)driver->vkFreeMemory(device,m,nullptr);}
};
// Called only for a new bank or while the pool is its sole owner. The parent
// has already proven completion before releasing the final recording lease.
void prepare_descriptors(FrameBank& b,const VkDescriptorPoolCreateInfo& create){
    const std::array<std::uint32_t,5> counts{create.maxSets,create.pPoolSizes[0].descriptorCount,create.pPoolSizes[1].descriptorCount,
        create.pPoolSizes[2].descriptorCount,create.pPoolSizes[3].descriptorCount};
    auto& d=*b.driver;
    if(b.descriptors){
        if(b.descriptor_counts==counts&&d.vkResetDescriptorPool&&d.vkResetDescriptorPool(b.device,b.descriptors,0)==VK_SUCCESS)return;
        d.vkDestroyDescriptorPool(b.device,b.descriptors,nullptr);b.descriptors={};
    }
    require(d.vkCreateDescriptorPool(b.device,&create,nullptr,&b.descriptors)==VK_SUCCESS&&b.descriptors,"MToon frame descriptor pool");
    b.descriptor_counts=counts;
}
void allocate_bank(FrameBank& b,const NativeMToonWorkCapabilities& caps,VkDeviceSize available){
    auto& d=*b.driver;
    for(unsigned n=0;n<3;++n){VkBufferCreateInfo i{};i.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;i.size=b.capacities[n];
        i.usage=n==2?(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT):
            (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        require(d.vkCreateBuffer(b.device,&i,nullptr,&b.buffers[n])==VK_SUCCESS&&b.buffers[n],"MToon frame buffer");VkMemoryRequirements r{};d.vkGetBufferMemoryRequirements(b.device,b.buffers[n],&r);
        require(r.size>=i.size&&power_two(r.alignment)&&r.size<=available-b.allocated,"MToon frame actual allocation budget");
        const VkMemoryPropertyFlags needed=n==2?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        unsigned type=caps.memory.memoryTypeCount;for(unsigned k=0;k<caps.memory.memoryTypeCount;++k){const auto& t=caps.memory.memoryTypes[k];
            if((r.memoryTypeBits&(1u<<k))&&(t.propertyFlags&needed)==needed&&!(t.propertyFlags&(VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT|VK_MEMORY_PROPERTY_PROTECTED_BIT))&&
                t.heapIndex<caps.memory.memoryHeapCount&&caps.memory.memoryHeaps[t.heapIndex].size>=r.size){type=k;break;}}
        require(type<caps.memory.memoryTypeCount,"MToon frame memory type");VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=r.size;a.memoryTypeIndex=type;
        require(d.vkAllocateMemory(b.device,&a,nullptr,&b.memory[n])==VK_SUCCESS&&b.memory[n],"MToon frame memory");b.allocated+=r.size;
        require(d.vkBindBufferMemory(b.device,b.buffers[n],b.memory[n],0)==VK_SUCCESS,"MToon frame buffer binding");
    }
    require(d.vkMapMemory(b.device,b.memory[2],0,b.capacities[2],0,&b.mapped)==VK_SUCCESS&&b.mapped,"MToon frame control map");
}
bool valid_buffer(const VkDescriptorBufferInfo& b,VkDeviceSize minimum,bool uniform,const VkPhysicalDeviceLimits& l)noexcept{
    const auto alignment=uniform?l.minUniformBufferOffsetAlignment:l.minStorageBufferOffsetAlignment;
    return b.buffer&&b.range!=VK_WHOLE_SIZE&&b.range>=minimum&&b.range<=(uniform?l.maxUniformBufferRange:l.maxStorageBufferRange)&&
        power_two(alignment)&&b.offset%alignment==0&&b.offset<=~VkDeviceSize(0)-b.range;
}
bool valid_image(const VkDescriptorImageInfo& b)noexcept{return b.imageView&&b.sampler&&(b.imageLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||b.imageLayout==VK_IMAGE_LAYOUT_GENERAL);}
}
struct NativeMToonFramePool::Impl {
    VkDevice device{};PFN_vkGetDeviceProcAddr get{};const bool* parent=nullptr;NativeMToonWorkCapabilities caps;
    std::shared_ptr<FrameDriver> driver;std::vector<std::shared_ptr<FrameBank>> banks;std::uint64_t next_generation=1;bool uncertain=false;
    VkDeviceSize allocated()const noexcept{VkDeviceSize n=0;for(const auto& bank:banks)n+=bank->allocated;return n;}
    bool usable()const noexcept{return !uncertain&&parent&&!*parent;}
    void quarantine()noexcept{uncertain=true;for(auto& b:banks)b->uncertain=true;}
    ~Impl(){if(parent&&*parent)quarantine();}
    std::shared_ptr<FrameBank> acquire(const std::array<VkDeviceSize,3>& bytes,const VkDescriptorPoolCreateInfo& descriptors){
        require(usable(),"MToon frame pool quarantined");for(const auto& bank:banks)if(bank.use_count()==1&&!bank->uncertain&&bank->capacities[0]>=bytes[0]&&bank->capacities[1]>=bytes[1]&&bank->capacities[2]>=bytes[2]){
            prepare_descriptors(*bank,descriptors);return bank;}
        // Discard only a fully retired, inadequate bank. In-flight leases are
        // never resized or reused; inability to fit retains native fallback.
        for(auto it=banks.begin();it!=banks.end();){if(it->use_count()==1)it=banks.erase(it);else ++it;}
        require(banks.size()<3,"MToon retained frame slot budget");const auto used=allocated();require(used<byte_limit,"MToon retained frame byte budget");
        auto bank=std::make_shared<FrameBank>();bank->device=device;bank->driver=driver;bank->capacities=bytes;allocate_bank(*bank,caps,byte_limit-used);prepare_descriptors(*bank,descriptors);banks.push_back(bank);return bank;
    }
};
struct NativeMToonFrame::Impl {
    std::shared_ptr<NativeMToonFramePool::Impl> pool;std::shared_ptr<FrameBank> bank;
    NativeMToonProfileFrame profile;std::array<std::uint32_t,3> shade_groups{};
    std::shared_ptr<const NativeMToonRasterProgram> program;std::unique_ptr<NativeMToonWork> work;
    std::vector<std::shared_ptr<const void>> leases;std::vector<NativeMToonFrameShadeDraw> shade;
    struct Draw {NativeMToonMainCall call;NativeMToonRasterFamily family=NativeMToonRasterFamily::ordinary;bool selected=false,capture_only=false;VkDeviceSize control=0;};
    std::vector<Draw> main;std::vector<NativeMToonOutlineCall> outlines;std::vector<VkDeviceSize> shade_controls;
    std::vector<NativeMToonMainCall> forward;std::vector<VkDeviceSize> forward_controls;std::uint32_t forward_actors=0;
    NativeMToonForwardLayout forward_layout;VkDeviceSize forward_offset=0,forward_packet_offset=0;std::array<bool,32> forward_prepared{};
    VkCommandBuffer command{};VkExtent2D extent{};std::uint32_t generation=0;VkDeviceSize id_bytes=0,packet_bytes=0,permit=0;
    static constexpr unsigned observation_side=17,outline_observation_side=129;
    unsigned observation_samples=0;
    std::vector<VkBufferCopy> observation_ids,observation_packets;
    VkDeviceSize observation_offset=0,observation_bytes=0;bool observation_recorded=false;
    MToonNumericLayout numeric_layout;MToonNumericConfig numeric_config;
    std::array<std::array<std::uint32_t,4>,MToonNumeric::draw_limit> numeric_draws{};
    VkDeviceSize numeric_observation_offset=0;
    bool initialized=false,visibility=false,gbuffer=false,worked=false,active=false,uncertain=false;
    bool usable()const noexcept{return !uncertain&&pool&&pool->usable()&&bank&&!bank->uncertain&&program&&program->reusable();}
    void quarantine()noexcept{uncertain=true;if(bank){bank->uncertain=true;bank->profile.quarantine();}if(program)program->quarantine();if(work)work->quarantine();}
    VkDescriptorBufferInfo meta(VkDeviceSize at,VkDeviceSize bytes)const noexcept{return {bank->buffers[2],at,bytes};}
    void write(VkDescriptorSet set,unsigned binding,VkDescriptorType type,const VkDescriptorBufferInfo& b){VkWriteDescriptorSet w{};w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=binding;w.descriptorType=type;w.descriptorCount=1;w.pBufferInfo=&b;pool->driver->vkUpdateDescriptorSets(pool->device,1,&w,0,nullptr);}
    void image(VkDescriptorSet set,unsigned binding,VkDescriptorType type,const VkDescriptorImageInfo* b,unsigned count=1){VkWriteDescriptorSet w{};w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=binding;w.descriptorType=type;w.descriptorCount=count;w.pImageInfo=b;pool->driver->vkUpdateDescriptorSets(pool->device,1,&w,0,nullptr);}
    ~Impl(){if(uncertain||(pool&&!pool->usable())||(bank&&bank->uncertain)||(program&&!program->reusable()))quarantine();}
};
NativeMToonFramePool::NativeMToonFramePool(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonWorkCapabilities& caps,const bool* parent){
    require(device&&get&&parent&&!*parent&&caps.memory.memoryTypeCount&&caps.memory.memoryTypeCount<=VK_MAX_MEMORY_TYPES&&caps.memory.memoryHeapCount&&caps.memory.memoryHeapCount<=VK_MAX_MEMORY_HEAPS,"MToon frame pool inputs");
    require(power_two(caps.limits.minUniformBufferOffsetAlignment)&&power_two(caps.limits.minStorageBufferOffsetAlignment),"MToon frame buffer alignment limits");
    impl=std::make_shared<Impl>();impl->device=device;impl->get=get;impl->caps=caps;impl->parent=parent;impl->driver=std::make_shared<FrameDriver>(device,get);
}
NativeMToonFramePool::~NativeMToonFramePool(){if(impl&&impl->parent&&*impl->parent)impl->quarantine();}
VkDeviceSize NativeMToonFramePool::allocated_bytes()const noexcept{return impl?impl->allocated():0;}
std::size_t NativeMToonFramePool::retained_slots()const noexcept{return impl?impl->banks.size():0;}
void NativeMToonFramePool::quarantine()noexcept{if(impl)impl->quarantine();}
std::unique_ptr<NativeMToonFrame> NativeMToonFramePool::prepare(VkCommandBuffer command,VkExtent2D extent,const std::vector<NativeMToonFrameDraw>& draws,std::shared_ptr<const NativeMToonRasterProgram> program,std::shared_ptr<const NativeMToonWorkProgram> work,bool collect_observations,const NativeMToonProfileConfig& profile,const MToonNumericConfig& numeric){
    return std::unique_ptr<NativeMToonFrame>(new NativeMToonFrame(impl,command,extent,draws,std::move(program),std::move(work),collect_observations,profile,numeric));
}
NativeMToonFrame::NativeMToonFrame(std::shared_ptr<NativeMToonFramePool::Impl> pool,VkCommandBuffer command,VkExtent2D extent,const std::vector<NativeMToonFrameDraw>& inputs,std::shared_ptr<const NativeMToonRasterProgram> program,std::shared_ptr<const NativeMToonWorkProgram> work_program,bool collect_observations,const NativeMToonProfileConfig& profile,const MToonNumericConfig& numeric){
    require(pool&&pool->usable()&&command&&program&&program->reusable()&&program->belongs_to(pool->device,pool->parent)&&work_program&&work_program->reusable(),"MToon frame prepared program/owner");
    const auto& l=pool->caps.limits;const auto pixels=std::uint64_t(extent.width)*extent.height;
    require(extent.width&&extent.height&&extent.width<=16384&&extent.height<=16384&&pixels<=NativeMToonWork::pixel_limit&&extent.width<=l.maxFramebufferWidth&&extent.height<=l.maxFramebufferHeight&&
        pixels*4<=l.maxStorageBufferRange&&16+pixels*16<=l.maxStorageBufferRange&&!inputs.empty()&&inputs.size()<=4096,"MToon frame extent/draw budget");
    std::uint32_t triangles=0,outline_count=0,forward_count=0;bool selected=false,opaque_anchor=false;
    for(const auto& d:inputs){const auto& u=d.uniform;const auto& g=d.geometry;
        require(d.lease&&u.geometry[1]&&u.geometry[3]&&u.geometry[3]%3==0&&u.geometry[0]<=UINT32_MAX-u.geometry[1]&&
            u.geometry[2]<=UINT32_MAX-u.geometry[3]&&std::isfinite(u.time[0])&&d.cull<=2&&d.outline_cull<=2&&d.control.facts[0]<=2&&d.control.facts[1]<=1&&d.control.facts[2]<=2&&
            d.control.facts[3]==0&&std::isfinite(d.control.time[0])&&(!d.enhanced||!d.selected)&&(!d.outline||d.selected),"MToon frame draw/control shape");
        require(valid_buffer(g.frame,224,true,l)&&valid_buffer(g.material,80,true,l)&&valid_buffer(g.vertices,VkDeviceSize(u.geometry[0]+u.geometry[1])*44,false,l)&&
            valid_buffer(g.indices,VkDeviceSize(u.geometry[2]+u.geometry[3])*4,false,l)&&valid_image(g.base),"MToon frame owned geometry/material spans");
        require(!d.needs_surface||valid_buffer(g.surface,VkDeviceSize(u.geometry[0]+u.geometry[1])*32,false,l),"MToon frame required posed surface stream");
        const auto optional=[&](const VkDescriptorBufferInfo& b,VkDeviceSize bytes,bool uniform){return !b.buffer||valid_buffer(b,bytes,uniform,l);};
        require(optional(g.surface,32,false)&&optional(g.enhanced_uniform,112,true)&&optional(d.history.raw_uv,8,false)&&
            optional(d.history.frame,224,true)&&optional(d.history.vertices,44,false)&&optional(d.history.legacy,64,false)&&
            optional(d.history.previous_legacy,64,false)&&optional(d.history.batches,80,false)&&optional(d.history.previous_batches,80,false),"MToon frame optional owned descriptor span");
        if(d.enhanced){require(valid_buffer(g.enhanced_uniform,112,true,l),"MToon ordinary Enhanced uniform");for(const auto& image:g.enhanced_textures)require(valid_image(image),"MToon ordinary Enhanced texture");}
        if(d.menu_cutout_material)require(d.selected&&!d.capture_only&&!d.forward_actor&&
            d.menu_cutout_material->modes[0]==1&&d.menu_cutout_material->modes[1]<=1&&
            (d.menu_cutout_material->modes[2]==2||d.menu_cutout_material->modes[2]==3),"MToon menu cutout derivation scope");
        if(!d.capture_only)opaque_anchor=true;
        if(d.selected||d.forward_actor){selected=true;require(valid_buffer(d.material.uniform,752,true,l),"MToon frame authored uniform");for(const auto& image:d.material.textures)require(valid_image(image),"MToon frame authored texture/sampler");
            bool raw=false;for(unsigned slot=0;slot<11;++slot)raw=raw||u.uv[slot]!=UINT32_MAX;require(!raw||valid_buffer(d.history.raw_uv,8,false,l),"MToon frame required raw UV stream");}
        if(d.forward_actor){require(d.forward_actor<=32&&program->has_forward()&&u.geometry[2]%3==0&&(!d.needs_surface||valid_buffer(d.forward_surface,VkDeviceSize(u.geometry[0]+u.geometry[1])*32,false,l)),"MToon forward capture sources");++forward_count;}
        if(d.control.facts[1])require(valid_buffer(d.history.frame,224,true,l)&&valid_buffer(d.history.vertices,VkDeviceSize(u.geometry[0]+u.geometry[1])*44,false,l),"MToon frame joined history span");
        if(d.outline){require(program->has_outlines(),"MToon frame outline program pending");++outline_count;
            if(d.legacy_required)require(d.control.facts[0]!=0&&valid_buffer(d.history.legacy,64,false,l)&&
                (d.control.facts[0]!=2||valid_buffer(d.history.batches,80,false,l)),"MToon frame exact current legacy outline source");}
        const auto count=u.geometry[3]/3;require(triangles<=0x7fffffffu-count,"MToon frame main rank overflow");triangles+=count;
    }
    require(selected&&opaque_anchor&&inputs.size()+outline_count<=NativeMToonWork::draw_limit&&pool->next_generation<=UINT32_MAX,"MToon frame selection/generation budget");
    std::uint32_t all_triangles=triangles;for(const auto& d:inputs)if(d.outline){const auto count=d.uniform.geometry[3]/3;require(all_triangles<=0x7fffffffu-count,"MToon frame outline rank overflow");all_triangles+=count;}
    const auto alignment=l.minUniformBufferOffsetAlignment;VkDeviceSize cursor=256;
    const auto zero_toon=aligned(cursor,alignment);cursor=zero_toon+752;const auto zero_surface=aligned(cursor,alignment);cursor=zero_surface+112;
    const auto permit=aligned(cursor,alignment);cursor=permit+16;
    const auto shadow_stride=forward_count?aligned(16,l.minStorageBufferOffsetAlignment):0;
    const auto shadow_ranges_offset=forward_count?aligned(cursor,l.minStorageBufferOffsetAlignment):0;
    if(forward_count)cursor=shadow_ranges_offset+32u*shadow_stride;
    struct Offset {VkDeviceSize uniform=0,control=0,shade=0,outline=0,outline_shade=0,forward=0,forward_uniform=0,forward_control=0,forward_shade=0,menu_material=0;};std::vector<Offset> offsets(inputs.size());
    for(std::size_t n=0;n<inputs.size();++n){auto& o=offsets[n];o.uniform=aligned(cursor,alignment);cursor=o.uniform+96;o.control=aligned(cursor,alignment);cursor=o.control+32;
        if(inputs[n].menu_cutout_material){o.menu_material=aligned(cursor,alignment);cursor=o.menu_material+sizeof(MToonGpuMaterial);}
        if(inputs[n].selected){o.shade=aligned(cursor,alignment);cursor=o.shade+64;}if(inputs[n].outline){o.outline=aligned(cursor,alignment);cursor=o.outline+96;o.outline_shade=aligned(cursor,alignment);cursor=o.outline_shade+64;}
        if(inputs[n].forward_actor){o.forward=aligned(cursor,alignment);cursor=o.forward+96;o.forward_uniform=aligned(cursor,alignment);cursor=o.forward_uniform+96;
            o.forward_control=aligned(cursor,alignment);cursor=o.forward_control+32;o.forward_shade=aligned(cursor,alignment);cursor=o.forward_shade+64;}}
    unsigned observation_samples=0;VkDeviceSize observation_offset=0,observation_bytes=0;
    (void)numeric;const MToonNumericLayout numeric_layout{};
    if(collect_observations){
        observation_samples=Impl::observation_side*Impl::observation_side+(outline_count?Impl::outline_observation_side*Impl::outline_observation_side:0u);
        observation_offset=aligned(cursor,16);observation_bytes=32+VkDeviceSize(inputs.size()+outline_count)*32+observation_samples*20+(forward_count?1328:0);
        observation_bytes+=numeric_layout.bytes*(forward_count?2u:1u);
        cursor=observation_offset+observation_bytes;
    }
    require(cursor<=8ull*1024*1024,"MToon frame control allocation budget");
    impl=std::make_unique<Impl>();auto& p=*impl;p.pool=std::move(pool);p.program=std::move(program);p.command=command;p.extent=extent;p.id_bytes=pixels*4;p.packet_bytes=16+pixels*16;p.permit=permit;p.generation=std::uint32_t(p.pool->next_generation++);
    p.numeric_layout=numeric_layout;p.numeric_config=numeric;p.packet_bytes+=numeric_layout.bytes;
    require(p.packet_bytes<=l.maxStorageBufferRange,"numeric opaque range budget");
    if(numeric_layout){
        p.numeric_observation_offset=observation_offset+observation_bytes-numeric_layout.bytes*(forward_count?2u:1u);
        for(unsigned n=0;n<numeric_layout.draws;++n)p.numeric_draws[n]={inputs[n].numeric_material,inputs[n].numeric_actor,0u,0u};
    }
    if(collect_observations){
        p.observation_offset=observation_offset;p.observation_bytes=observation_bytes;
        p.observation_samples=observation_samples;p.observation_ids.resize(observation_samples);p.observation_packets.resize(observation_samples);
        const auto samples=observation_offset+32+VkDeviceSize(inputs.size()+outline_count)*32;
        // Preserve both original fixed grids only for requested observations.
        // Prepare copy arrays here (throwing boundary), never on the render
        // callback's small stack or inside a noexcept allocation.
        for(unsigned n=0;n<observation_samples;++n){const unsigned base=Impl::observation_side*Impl::observation_side;
            const unsigned side=n<base?Impl::observation_side:Impl::outline_observation_side,at=n<base?n:n-base;
            const auto x=(2*(at%side)+1)*extent.width/(2*side),y=(2*(at/side)+1)*extent.height/(2*side);const VkDeviceSize pixel=VkDeviceSize(y)*extent.width+x;
            p.observation_ids[n]={pixel*4,samples+n*20,4};p.observation_packets[n]={16+pixel*16,samples+n*20+4,16};}
    }

    if(forward_count){p.forward_layout=native_mtoon_forward_layout(extent.width,extent.height,262144);
        require(bool(p.forward_layout)&&p.forward_layout.capture_bytes<=l.maxStorageBufferRange&&p.forward_layout.packet_bytes+numeric_layout.bytes<=l.maxStorageBufferRange,"MToon forward range budget");
        p.forward_offset=aligned(p.id_bytes,l.minStorageBufferOffsetAlignment);p.forward_packet_offset=aligned(p.packet_bytes,l.minStorageBufferOffsetAlignment);}
    const auto layouts=p.program->descriptor_layouts();const auto total=unsigned(inputs.size()),outlines=outline_count;
    const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,total*7+outlines*3+forward_count*7},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,total*10+outlines*7+forward_count*10},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,(total+forward_count)*15},{VK_DESCRIPTOR_TYPE_SAMPLER,(total+forward_count)*15}};
    VkDescriptorPoolCreateInfo create{};create.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;create.maxSets=total*3+outlines+forward_count*2;create.poolSizeCount=4;create.pPoolSizes=sizes;
    p.bank=p.pool->acquire({p.forward_layout?p.forward_offset+p.forward_layout.capture_bytes:p.id_bytes,p.forward_layout?p.forward_packet_offset+p.forward_layout.packet_bytes+numeric_layout.bytes:p.packet_bytes,cursor},create);std::memset(p.bank->mapped,0,std::size_t(cursor));
    if(forward_count){const auto ranges=native_mtoon_shadow_actor_ranges(inputs,p.generation);
        for(unsigned n=0;n<ranges.size();++n)std::memcpy(static_cast<char*>(p.bank->mapped)+shadow_ranges_offset+n*shadow_stride,ranges[n].data(),16);}
    for(unsigned n=0;n<total;++n)if(inputs[n].menu_cutout_material){
        auto derived=*inputs[n].menu_cutout_material;derived.modes[2]=1;derived.animation[3]=.5f;
        std::memcpy(static_cast<char*>(p.bank->mapped)+offsets[n].menu_material,&derived,sizeof derived);
    }
    const auto material_binding=[&](unsigned n){return inputs[n].menu_cutout_material?
        p.meta(offsets[n].menu_material,sizeof(MToonGpuMaterial)):inputs[n].material.uniform;};
    const auto defaults=p.meta(0,256);const auto material_default=p.meta(zero_toon,752),surface_default=p.meta(zero_surface,112);
    const auto allocate=[&](const VkDescriptorSetLayout* set_layouts,unsigned count,VkDescriptorSet* out){VkDescriptorSetAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;a.descriptorPool=p.bank->descriptors;a.descriptorSetCount=count;a.pSetLayouts=set_layouts;
        require(p.pool->driver->vkAllocateDescriptorSets(p.pool->device,&a,out)==VK_SUCCESS,"MToon frame descriptor allocation");for(unsigned n=0;n<count;++n)require(bool(out[n]),"MToon frame null descriptor set");};
    std::vector<NativeMToonWorkDraw> work_draws;work_draws.reserve(total+outlines);p.main.reserve(total);p.leases.reserve(total);p.shade.reserve(total+outlines);p.shade_controls.reserve(total+outlines);
    const auto span=[&](const VkDescriptorBufferInfo& b,const VkDescriptorBufferInfo& fallback){return b.buffer?b:fallback;};
    const auto draw_descriptors=[&](VkDescriptorSet set,const NativeMToonFrameDraw& d,const VkDescriptorBufferInfo& uniform,const VkDescriptorBufferInfo& control){
        const VkDescriptorBufferInfo buffers[]{uniform,{p.bank->buffers[0],0,p.id_bytes},span(d.history.raw_uv,defaults),span(d.history.frame,d.geometry.frame),span(d.history.vertices,d.geometry.vertices),
            span(d.history.legacy,defaults),span(d.history.previous_legacy,defaults),control,span(d.history.batches,defaults),span(d.history.previous_batches,defaults)};
        for(unsigned n=0;n<10;++n)p.write(set,n,(n==0||n==3||n==7)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,buffers[n]);
    };
    const auto shade_draw=[&](const NativeMToonFrameDraw& d,const VkDescriptorBufferInfo& material,const VkDescriptorBufferInfo& uniform,VkDeviceSize control,unsigned work_index,unsigned main_index,bool outline,bool forward=false){
        NativeMToonShadeControl value;value.domain={extent.width,extent.height,p.generation,0};value.extra[2]=numeric_layout?4u:0u;value.extra[3]=d.outline&&!forward?main_index+1u:0u;value.draw={work_index,forward?2u:outline?1u:0u,d.control.facts[0],forward?d.forward_actor:outline?unsigned(d.outline_cull):0u};std::memcpy(static_cast<char*>(p.bank->mapped)+control,&value,64);
        NativeMToonFrameShadeDraw entry;entry.work_index=work_index;entry.outline_owner=value.extra[3];entry.outline=outline;entry.forward_actor=forward?d.forward_actor:0;auto& in=entry.inputs;in.material=material;
        for(unsigned slot=0;slot<11;++slot){in.material_maps[slot]=d.material.textures[slot];in.material_samplers[slot]=d.material.textures[slot].sampler;}
        in.owned={p.meta(control,64),uniform,d.geometry.frame,d.geometry.vertices,d.geometry.indices,span(d.geometry.surface,defaults),span(d.history.raw_uv,defaults),
            span(d.history.legacy,defaults),span(d.history.batches,defaults),VkDescriptorBufferInfo{p.bank->buffers[0],0,p.id_bytes},VkDescriptorBufferInfo{p.bank->buffers[1],0,p.packet_bytes},{},{},{}};
        if(forward){in.owned[5]=span(d.forward_surface,defaults);in.owned[9]={p.bank->buffers[0],p.forward_offset,p.forward_layout.capture_bytes};in.owned[10]={p.bank->buffers[1],p.forward_packet_offset,p.forward_layout.packet_bytes+numeric_layout.bytes};
            in.owned[12]=d.shadow_actor==d.forward_actor&&d.shadow_actor&&d.shadow_actor<=32?
                p.meta(shadow_ranges_offset+(d.shadow_actor-1u)*shadow_stride,16):defaults;}
        p.shade.push_back(entry);p.shade_controls.push_back(control);++p.shade_groups[forward?1:outline?2:0];
    };
    unsigned rank=0;for(unsigned n=0;n<total;++n){const auto& d=inputs[n];const auto& o=offsets[n];p.leases.push_back(d.lease);Impl::Draw draw;draw.selected=d.selected;draw.capture_only=d.capture_only;draw.family=d.enhanced?NativeMToonRasterFamily::enhanced:NativeMToonRasterFamily::ordinary;draw.control=o.control;draw.call.first_index=d.uniform.geometry[2];draw.call.index_count=d.uniform.geometry[3];draw.call.cull=d.selected?d.cull:VK_CULL_MODE_NONE;allocate(layouts.data(),3,draw.call.sets.data());
        auto uniform=d.uniform;uniform.domain={extent.width,extent.height,rank,d.uniform.geometry[3]/3};uniform.uv[11]=d.selected?1u:0u;std::memcpy(static_cast<char*>(p.bank->mapped)+o.uniform,&uniform,96);std::memcpy(static_cast<char*>(p.bank->mapped)+o.control,&d.control,32);
        const auto& g=d.geometry;const unsigned bindings[]{0,3,4,5,11,12};const VkDescriptorBufferInfo buffers[]{g.frame,g.material,g.vertices,g.indices,span(g.surface,defaults),span(g.enhanced_uniform,surface_default)};
        for(unsigned k=0;k<6;++k)p.write(draw.call.sets[0],bindings[k],(k<2||k==5)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,buffers[k]);
        p.image(draw.call.sets[0],1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,&g.base);p.image(draw.call.sets[0],2,VK_DESCRIPTOR_TYPE_SAMPLER,&g.base);
        for(unsigned slot=0;slot<3;++slot){const auto image=valid_image(g.enhanced_textures[slot])?g.enhanced_textures[slot]:g.base;p.image(draw.call.sets[0],13+slot*2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,&image);p.image(draw.call.sets[0],14+slot*2,VK_DESCRIPTOR_TYPE_SAMPLER,&image);}
        p.write(draw.call.sets[1],0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,d.selected||d.forward_actor?material_binding(n):material_default);std::array<VkDescriptorImageInfo,11> textures=d.material.textures;if(!d.selected&&!d.forward_actor)textures.fill(g.base);
        p.image(draw.call.sets[1],1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,textures.data(),11);p.image(draw.call.sets[1],2,VK_DESCRIPTOR_TYPE_SAMPLER,textures.data(),11);draw_descriptors(draw.call.sets[2],d,p.meta(o.uniform,96),p.meta(o.control,32));
        work_draws.push_back({rank,uniform.domain[3],d.selected||d.forward_actor?1u:0u,0});rank+=uniform.domain[3];if(d.selected)shade_draw(d,material_binding(n),p.meta(o.uniform,96),o.shade,n,n,false);p.main.push_back(draw);
    }
    for(unsigned n=0;n<total;++n)if(inputs[n].forward_actor){const auto& d=inputs[n];const auto& o=offsets[n];auto call=p.main[n].call;
        call.cull=d.cull;allocate(&layouts[0],1,&call.sets[0]);allocate(&layouts[2],1,&call.sets[2]);
        const unsigned copied[]{0,1,2,3,4,5,11,12,13,14,15,16,17,18};std::array<VkCopyDescriptorSet,14> copies{};
        for(unsigned k=0;k<14;++k){auto& c=copies[k];c.sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;c.srcSet=p.main[n].call.sets[0];c.dstSet=call.sets[0];c.srcBinding=c.dstBinding=copied[k];c.descriptorCount=1;}
        p.pool->driver->vkUpdateDescriptorSets(p.pool->device,0,nullptr,unsigned(copies.size()),copies.data());
        p.write(call.sets[0],11,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,span(d.forward_surface,defaults));
        auto u=d.uniform;u.uv[11]=1;u.domain={extent.width,extent.height,p.generation,u.geometry[3]/3};std::memcpy(static_cast<char*>(p.bank->mapped)+o.forward,&u,96);
        NativeMToonOutlineControl c;c.facts[1]=d.forward_actor;std::memcpy(static_cast<char*>(p.bank->mapped)+o.forward_control,&c,32);
        draw_descriptors(call.sets[2],d,p.meta(o.forward,96),p.meta(o.forward_control,32));
        p.write(call.sets[2],1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,{p.bank->buffers[0],p.forward_offset,p.forward_layout.capture_bytes});
        u.domain[2]=u.geometry[2]/3;std::memcpy(static_cast<char*>(p.bank->mapped)+o.forward_uniform,&u,96);
        shade_draw(d,material_binding(n),p.meta(o.forward_uniform,96),o.forward_shade,n,n,false,true);p.forward.push_back(call);p.forward_controls.push_back(o.forward_control);
        p.forward_actors|=1u<<(d.forward_actor-1u);
    }
    for(unsigned n=0;n<total;++n)if(inputs[n].outline){const auto& d=inputs[n];const auto& o=offsets[n];NativeMToonOutlineCall draw;draw.sets=p.main[n].call.sets;draw.first_index=d.uniform.geometry[2];draw.index_count=d.uniform.geometry[3];draw.cull=d.outline_cull;allocate(&layouts[2],1,&draw.sets[2]);
        auto uniform=d.uniform;uniform.domain={extent.width,extent.height,rank,d.uniform.geometry[3]/3};uniform.uv[11]=1;std::memcpy(static_cast<char*>(p.bank->mapped)+o.outline,&uniform,96);draw_descriptors(draw.sets[2],d,p.meta(o.outline,96),p.meta(o.control,32));
        shade_draw(d,material_binding(n),p.meta(o.outline,96),o.outline_shade,unsigned(work_draws.size()),n,true);work_draws.push_back({rank,uniform.domain[3],1,0});rank+=uniform.domain[3];p.outlines.push_back(draw);
    }
    p.work=std::make_unique<NativeMToonWork>(p.pool->device,p.pool->get,p.pool->caps,VkDescriptorBufferInfo{p.bank->buffers[0],0,p.id_bytes},
        NativeMToonWorkParameters{extent.width,extent.height,unsigned(work_draws.size()),NativeMToonWork::group_budget_limit},work_draws,std::array<NativeMToonWorkShader,4>{},p.pool->parent,std::move(work_program),&p.bank->work);
    if(profile.enabled)p.profile.prepare(p.bank->profile,p.pool->device,p.pool->get,profile,p.generation,command,&p.bank->uncertain);
}
NativeMToonFrame::~NativeMToonFrame()=default;
VkDescriptorBufferInfo NativeMToonFrame::forward_capture()const noexcept{return impl&&impl->forward_layout?VkDescriptorBufferInfo{impl->bank->buffers[0],impl->forward_offset,impl->forward_layout.capture_bytes}:VkDescriptorBufferInfo{};}
VkDescriptorBufferInfo NativeMToonFrame::forward_packets()const noexcept{return impl&&impl->forward_layout?VkDescriptorBufferInfo{impl->bank->buffers[1],impl->forward_packet_offset,impl->forward_layout.packet_bytes+impl->numeric_layout.bytes}:VkDescriptorBufferInfo{};}
void NativeMToonFrame::forward_ready(bool ready)noexcept{if(impl&&!impl->active)impl->forward_prepared.fill(ready&&bool(impl->forward_layout));}
void NativeMToonFrame::forward_ready(std::uint32_t actor,bool ready)noexcept{if(impl&&!impl->active&&actor&&actor<=impl->forward_prepared.size())impl->forward_prepared[actor-1]=ready&&bool(impl->forward_layout);}
bool NativeMToonFrame::has_shadows()const noexcept{return impl&&impl->usable()&&impl->program->has_shadows();}
bool NativeMToonFrame::record_shadow(VkCommandBuffer command,VkDescriptorSet light,const std::uint32_t* indices,std::uint32_t count,
    VkPipeline original,VkPipelineLayout compatible,std::uint32_t sets,const VkDescriptorSet* descriptors)noexcept{
    if(!has_shadows()||!impl->initialized||command!=impl->command||!light||!indices||!count||count>4096||!original||!compatible||!sets||sets>32||!descriptors)return false;
    for(unsigned n=0;n<sets;++n)if(!descriptors[n])return false;
    for(unsigned n=0;n<count;++n)if(indices[n]>=impl->main.size()||!impl->main[indices[n]].selected)return false;
    for(unsigned n=0;n<count;++n)if(!impl->program->record_shadow(command,light,&impl->main[indices[n]].call,1,original,compatible,sets,descriptors))return false;
    return true;
}
bool NativeMToonFrame::initialize(VkCommandBuffer command)noexcept{
    if(!impl||!impl->usable()||impl->initialized||command!=impl->command)return false;auto& p=*impl;auto& d=*p.pool->driver;
    if(p.profile.enabled())(void)p.profile.reset(command,NativeMToonProfileFrame::Clock::now());
    d.vkCmdFillBuffer(command,p.bank->buffers[0],0,p.id_bytes,0);d.vkCmdFillBuffer(command,p.bank->buffers[1],16,p.packet_bytes-16,0);
    const std::array<std::uint32_t,4> header{0x4d544350u,p.generation,p.extent.width,p.extent.height};d.vkCmdUpdateBuffer(command,p.bank->buffers[1],0,16,header.data());
    if(p.forward_layout){
        d.vkCmdFillBuffer(command,p.bank->buffers[0],p.forward_offset+16,16,0);
        d.vkCmdFillBuffer(command,p.bank->buffers[0],p.forward_offset+32,p.id_bytes,0xffffffffu);
        const std::array<std::uint32_t,8> capture{0x4d544652u,p.generation,p.extent.width,p.extent.height,0,0,p.forward_layout.capacity,0};
        d.vkCmdUpdateBuffer(command,p.bank->buffers[0],p.forward_offset,32,capture.data());
        d.vkCmdFillBuffer(command,p.bank->buffers[0],p.forward_offset+p.forward_layout.record_offset,32u*24u,0);
        d.vkCmdFillBuffer(command,p.bank->buffers[1],p.forward_packet_offset+16,p.forward_layout.packet_bytes-16,0);
        const std::array<std::uint32_t,4> packets{0x4d544650u,p.generation,p.forward_layout.capacity,0};
        d.vkCmdUpdateBuffer(command,p.bank->buffers[1],p.forward_packet_offset,16,packets.data());
    }
    VkMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT;
    d.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);p.initialized=true;return true;
}
bool NativeMToonFrame::record_raster(VkCommandBuffer command,bool gbuffer,VkPipeline original,VkPipelineLayout layout,unsigned sets,const VkDescriptorSet* descriptors)noexcept{
    if(!impl||!impl->usable()||command!=impl->command||!impl->initialized||!original||!layout||!sets||sets>32||!descriptors)return false;auto& p=*impl;
    if((gbuffer&&(!p.visibility||p.gbuffer))||(!gbuffer&&p.visibility))return false;for(unsigned n=0;n<sets;++n)if(!descriptors[n])return false;
    for(const auto& d:p.main){if(d.capture_only)continue;if(d.selected){auto fallback=d.call;fallback.cull=VK_CULL_MODE_NONE;if(!p.program->record(command,gbuffer,NativeMToonRasterFamily::fallback,&fallback,1,original,layout,sets,descriptors)||
            !p.program->record(command,gbuffer,NativeMToonRasterFamily::authored,&d.call,1,original,layout,sets,descriptors))return false;}
        else if(!p.program->record(command,gbuffer,d.family,&d.call,1,original,layout,sets,descriptors))return false;}
    const auto outline_stage=gbuffer?NativeMToonProfileStage::outline_gbuffer:NativeMToonProfileStage::outline_visibility;
    if(!p.outlines.empty())(void)p.profile.begin(command,outline_stage);
    if(!p.program->record_outline(command,gbuffer,p.outlines.data(),unsigned(p.outlines.size()),original,layout,sets,descriptors))return false;
    if(!p.outlines.empty())(void)p.profile.end(command,outline_stage);
    if(gbuffer){if(!p.forward.empty())(void)p.profile.begin(command,NativeMToonProfileStage::forward_capture);
        if(!p.program->record_forward(command,p.forward.data(),unsigned(p.forward.size()),original,layout,sets,descriptors))return false;
        if(!p.forward.empty())(void)p.profile.end(command,NativeMToonProfileStage::forward_capture);}
    if(gbuffer)p.gbuffer=true;else p.visibility=true;return true;
}
bool NativeMToonFrame::record_work(VkCommandBuffer command)noexcept{
    if(!impl||!impl->usable()||command!=impl->command||!impl->gbuffer||impl->worked)return false;auto& p=*impl;
    if(!p.work->record(command))return false;
    // Work publishes its actual owned ranges only after successful recording.
    // Shading descriptors are constructed by the parent after this boundary.
    for(auto& d:p.shade){
        if(d.forward_actor)d.inputs.owned[11]={p.bank->buffers[0],0,p.id_bytes};
        else{d.inputs.owned[11]=p.work->records();d.inputs.owned[12]=p.work->summary();}}
    p.worked=true;return true;
}
bool NativeMToonFrame::source_masks(unsigned buffers,unsigned images,unsigned samplers,unsigned water,bool quad)noexcept{
    if(!impl||!impl->usable()||impl->active||(buffers&~127u)||(images&~16383u)||(samplers&~127u)||(water&~31u))return false;
    for(auto at:impl->shade_controls){auto* control=reinterpret_cast<NativeMToonShadeControl*>(static_cast<char*>(impl->bank->mapped)+at);control->grants={0,buffers,images,samplers};control->extra={water,quad?1u:0u,(impl->observation_recorded?1u:0u)|(impl->numeric_layout?4u:0u),control->extra[3]};}return true;
}
bool NativeMToonFrame::set_active(bool admitted)noexcept{
    if(!impl||!impl->usable())return false;auto& p=*impl;const bool active=admitted&&p.initialized&&p.visibility&&p.gbuffer&&p.worked;
    for(const auto& draw:p.main){auto* control=reinterpret_cast<NativeMToonOutlineControl*>(static_cast<char*>(p.bank->mapped)+draw.control);control->facts[3]=active&&draw.selected?1u:0u;}
    for(auto at:p.forward_controls){auto* control=reinterpret_cast<NativeMToonOutlineControl*>(static_cast<char*>(p.bank->mapped)+at);control->facts[3]=active&&control->facts[1]&&control->facts[1]<=32&&p.forward_prepared[control->facts[1]-1]?2u:0u;}
    for(auto at:p.shade_controls){auto* control=reinterpret_cast<NativeMToonShadeControl*>(static_cast<char*>(p.bank->mapped)+at);const bool enabled=active&&(control->draw[1]!=2u||(control->draw[3]&&control->draw[3]<=32&&p.forward_prepared[control->draw[3]-1]));control->domain[3]=enabled?p.generation:0;control->grants[0]=enabled?1u:0u;}
    const std::array<std::uint32_t,4> permit{active?1u:0u,active?p.generation:0u,0,0};std::memcpy(static_cast<char*>(p.bank->mapped)+p.permit,permit.data(),16);p.active=active;return active;
}
bool NativeMToonFrame::record_observation(VkCommandBuffer command)noexcept{
    if(!impl||!impl->usable()||!impl->observation_bytes||command!=impl->command||!impl->worked||impl->observation_recorded)return false;
    auto& p=*impl;auto& driver=*p.pool->driver;const auto records=p.work->records(),summary=p.work->summary();
    const auto count=draw_count();if(!count||records.range!=VkDeviceSize(count)*32||summary.range!=16||
        p.observation_bytes!=32+VkDeviceSize(count)*32+p.observation_samples*20+(p.forward_layout?1328:0)+p.numeric_layout.bytes*(p.forward_layout?2u:1u)||
        p.observation_offset>p.bank->capacities[2]||p.observation_bytes>p.bank->capacities[2]-p.observation_offset)return false;
    const VkDescriptorBufferInfo sources[]{records,summary,{p.bank->buffers[0],0,p.id_bytes},{p.bank->buffers[1],0,p.packet_bytes}};
    std::array<VkBufferMemoryBarrier,5> barriers{};
    for(unsigned n=0;n<4;++n){auto& b=barriers[n];b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=sources[n].buffer;b.offset=sources[n].offset;b.size=n>=2&&p.forward_layout?p.bank->capacities[n-2]:sources[n].range;}
    auto& target=barriers[4];target.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;target.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;target.srcQueueFamilyIndex=target.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;target.buffer=p.bank->buffers[2];target.offset=p.observation_offset;target.size=p.observation_bytes;
    driver.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,unsigned(barriers.size()),barriers.data(),0,nullptr);
    VkBufferCopy copy{summary.offset,p.observation_offset,16};driver.vkCmdCopyBuffer(command,summary.buffer,target.buffer,1,&copy);
    copy={0,p.observation_offset+16,16};driver.vkCmdCopyBuffer(command,p.bank->buffers[1],target.buffer,1,&copy);
    copy={records.offset,p.observation_offset+32,records.range};driver.vkCmdCopyBuffer(command,records.buffer,target.buffer,1,&copy);
    const auto samples=p.observation_offset+32+records.range;
    if(p.observation_ids.size()!=p.observation_samples||p.observation_packets.size()!=p.observation_samples)return false;
    driver.vkCmdCopyBuffer(command,p.bank->buffers[0],target.buffer,p.observation_samples,p.observation_ids.data());
    driver.vkCmdCopyBuffer(command,p.bank->buffers[1],target.buffer,p.observation_samples,p.observation_packets.data());
    if(p.forward_layout){const auto at=samples+p.observation_samples*20;
        const VkBufferCopy capture[]{ {p.forward_offset,at,32}, {p.forward_offset+p.forward_layout.record_offset,at+48,32u*24u} };
        driver.vkCmdCopyBuffer(command,p.bank->buffers[0],target.buffer,2,capture);
        const VkBufferCopy packets[]{ {p.forward_packet_offset,at+32,16}, {p.forward_packet_offset+16,at+48+32u*24u,32u*16u} };
        driver.vkCmdCopyBuffer(command,p.bank->buffers[1],target.buffer,2,packets);
    }
    VkBufferMemoryBarrier host=target;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    driver.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    // Like the existing coherent active decision, these owned control writes
    // precede the original submission, despite being armed after recording.
    // Other frames retain zero and never emit diagnostic packet markers.
    for(auto at:p.shade_controls){auto* control=reinterpret_cast<NativeMToonShadeControl*>(static_cast<char*>(p.bank->mapped)+at);control->extra[2]|=1;}
    p.observation_recorded=true;return true;
}
NativeMToonGpuObservation NativeMToonFrame::observation_after_original_completion()const noexcept{
    NativeMToonGpuObservation out;if(!impl||!impl->usable()||!impl->observation_bytes||!impl->observation_recorded||!impl->bank->mapped)return out;
    const auto& p=*impl;out.available=true;out.generation=p.generation;out.width=p.extent.width;out.height=p.extent.height;out.work_draws=draw_count();
    const auto* data=static_cast<const unsigned char*>(p.bank->mapped)+p.observation_offset;
    std::array<std::uint32_t,4> summary{},header{};std::memcpy(summary.data(),data,16);std::memcpy(header.data(),data+16,16);
    out.header_valid=header==std::array<std::uint32_t,4>{0x4d544350u,p.generation,p.extent.width,p.extent.height};out.group_work=summary[0];out.declined=summary[1];
    out.work_valid=!summary[2]&&!summary[3]&&summary[1]<=1&&summary[1]==unsigned(summary[0]>NativeMToonWork::group_budget_limit)&&summary[0]<=128u*(NativeMToonWork::group_budget_limit+1u);
    for(unsigned n=0;n<out.work_draws;++n){std::array<std::uint32_t,8> row{};std::memcpy(row.data(),data+32+n*32,32);
        bool valid=row[6]==1&&!row[7]&&row[4]<=(p.extent.width+7)/8&&row[5]&&row[5]<=(p.extent.height+7)/8;
        if(row[4]){valid=valid&&row[0]<=row[2]&&row[1]<=row[3]&&row[2]<p.extent.width&&row[3]<p.extent.height&&row[4]==(row[2]-row[0]+8)/8&&row[5]==(row[3]-row[1]+8)/8;
            for(const auto& shade:p.shade)if(!shade.forward_actor&&shade.work_index==n){++out.selected_work;if(shade.outline)++out.outline_work;}}
        else valid=valid&&row[5]==1;out.work_valid=out.work_valid&&valid;
    }
    const auto finite_half=[](unsigned value){return (value&0x7c00u)!=0x7c00u;};
    for(unsigned n=0;n<p.observation_samples;++n){std::array<std::uint32_t,5> sample{};std::memcpy(sample.data(),data+32+out.work_draws*32+n*20,20);const auto key=sample[0];bool selected=false,outline=false;unsigned owner=0;
        if(key>>1){++out.identity_samples;const auto ordinal=(key>>1)-1;
            for(const auto& shade:p.shade){if(shade.forward_actor)continue;const auto& uniform=shade.inputs.owned[1];if(uniform.buffer!=p.bank->buffers[2]||uniform.range<96||uniform.offset>p.observation_offset||96>p.observation_offset-uniform.offset)continue;
                NativeMToonRasterUniform value{};std::memcpy(&value,static_cast<const unsigned char*>(p.bank->mapped)+uniform.offset,sizeof value);
                if(ordinal>=value.domain[2]&&ordinal-value.domain[2]<value.domain[3]){selected=true;outline=shade.outline;owner=shade.outline_owner;break;}}
            out.selected_samples+=selected;out.outline_samples+=outline;
        }
        const auto grey=sample[3]&65535u,tag=sample[3]>>24u;
        const bool colour_kind=owner?owner<=4096u&&(((sample[2]>>16)&4095u)+1u)==owner&&tag==(outline?4u:3u)&&(outline||(sample[2]>>28)==15u):tag==1u&&(sample[2]>>16)==0x3c00u;
        const bool matched=selected&&(key>>1)&&sample[4]==key&&colour_kind&&
            finite_half(sample[1]&65535u)&&finite_half(sample[1]>>16)&&finite_half(sample[2]&65535u)&&finite_half(grey)&&(!(grey&0x8000u)||!(grey&0x7fffu));
        out.matching_packets+=matched;out.matching_outlines+=matched&&outline;
        if(matched&&outline){++out.outline_coverage_samples;out.outline_coverage_units+=sample[2]>>28;}
        const auto stage=sample[3]&0x00ffffffu;
        const bool water_detail=stage==7&&sample[1]>=1&&sample[1]<=out.water_declines.size()&&(sample[2]&~255u)==0;
        const bool point_detail=stage==11&&sample[1]>=1&&sample[1]<=out.point_declines.size()&&(sample[2]&~2047u)==0;
        const bool diagnostic=out.header_valid&&out.work_valid&&selected&&sample[4]==key&&((!sample[1]&&!sample[2])||water_detail||point_detail)&&
            (sample[3]&0xff000000u)==0x02000000u&&stage>=1&&stage<=out.shader_declines.size();
        if(diagnostic){++out.shader_declines[stage-1];if(water_detail){++out.water_declines[sample[1]-1];out.water_facts|=sample[2];}if(point_detail){++out.point_declines[sample[1]-1];out.point_facts|=sample[2];}}
        out.malformed_packets+=!matched&&!diagnostic&&(sample[1]||sample[2]||sample[3]||sample[4]);
    }
    if(p.forward_layout){const auto* at=data+32+out.work_draws*32+p.observation_samples*20;
        std::array<std::uint32_t,8> capture{};std::array<std::uint32_t,4> packets{};std::memcpy(capture.data(),at,32);std::memcpy(packets.data(),at+32,16);
        out.forward_present=true;out.forward_capacity=p.forward_layout.capacity;out.forward_active=p.active;
        out.forward_capture_header=capture;out.forward_packet_header=packets;
        out.forward_capture_header_valid=capture[0]==0x4d544652u&&capture[1]==p.generation&&capture[2]==p.extent.width&&capture[3]==p.extent.height&&capture[6]==p.forward_layout.capacity;
        out.forward_packet_header_valid=packets[0]==0x4d544650u&&packets[1]==p.generation&&packets[2]==p.forward_layout.capacity;
        for(unsigned n=0;n<p.forward_prepared.size();++n)if(p.forward_prepared[n])out.forward_prepared_mask|=1u<<n;
        out.forward_capture_draws=unsigned(p.forward_controls.size());
        for(auto offset:p.forward_controls){const auto* control=reinterpret_cast<const NativeMToonOutlineControl*>(static_cast<const char*>(p.bank->mapped)+offset);
            if(control->facts[3]==2u){++out.forward_enabled_draws;if(control->facts[1]&&control->facts[1]<=32)out.forward_enabled_mask|=1u<<(control->facts[1]-1u);}}
        for(auto offset:p.shade_controls){const auto* control=reinterpret_cast<const NativeMToonShadeControl*>(static_cast<const char*>(p.bank->mapped)+offset);
            if(control->draw[1]==2u){++out.forward_shade_controls;out.forward_enabled_shades+=control->grants[0]==1u&&control->domain[3]==p.generation;}}
        if(out.forward_capture_header_valid&&out.forward_packet_header_valid){out.forward_fragments=capture[4];out.forward_overflow=capture[5];out.forward_colours=packets[3];
            if(!capture[5]&&capture[4]<=capture[6])for(unsigned n=0;n<std::min(32u,capture[4]);++n){std::array<unsigned,6> record{};std::array<unsigned,4> packet{};
                std::memcpy(record.data(),at+48+n*24,24);std::memcpy(packet.data(),at+48+32*24+n*16,16);++out.forward_samples;
                if(packet[2]==0x46574431u&&packet[3]==record[1]&&finite_half(packet[0]&65535u)&&finite_half(packet[0]>>16u)&&finite_half(packet[1]&65535u)&&finite_half(packet[1]>>16u))++out.forward_matches;
                else if(packet[2]==0x46574432u&&packet[3]==record[1]&&packet[0]>0&&packet[0]<=14)++out.forward_declines[packet[0]-1];
            }}
    }
    return out;
}
bool NativeMToonFrame::numeric_enabled()const noexcept{return impl&&bool(impl->numeric_layout);}
bool NativeMToonFrame::begin_gpu_stage(VkCommandBuffer command,NativeMToonProfileStage stage)noexcept{
    return impl&&impl->usable()&&impl->initialized&&impl->profile.begin(command,stage);
}
bool NativeMToonFrame::end_gpu_stage(VkCommandBuffer command,NativeMToonProfileStage stage)noexcept{
    return impl&&impl->usable()&&impl->initialized&&impl->profile.end(command,stage);
}
std::array<std::uint32_t,3> NativeMToonFrame::shade_group_counts()const noexcept{return impl?impl->shade_groups:std::array<std::uint32_t,3>{};}
NativeMToonProfileResult NativeMToonFrame::gpu_profile_after_original_completion()noexcept{
    if(!impl||!impl->usable())return {};
    return impl->profile.after_original_completion(impl->profile.enabled()?NativeMToonProfileFrame::Clock::now():NativeMToonProfileFrame::Clock::time_point{});
}
VkDescriptorBufferInfo NativeMToonFrame::identities()const noexcept{return impl&&impl->bank?VkDescriptorBufferInfo{impl->bank->buffers[0],0,impl->id_bytes}:VkDescriptorBufferInfo{};}
VkDescriptorBufferInfo NativeMToonFrame::packets()const noexcept{return impl&&impl->bank?VkDescriptorBufferInfo{impl->bank->buffers[1],0,impl->packet_bytes}:VkDescriptorBufferInfo{};}
VkDescriptorBufferInfo NativeMToonFrame::composite_permit()const noexcept{return impl&&impl->bank?impl->meta(impl->permit,16):VkDescriptorBufferInfo{};}
VkDescriptorBufferInfo NativeMToonFrame::work_records()const noexcept{return impl&&impl->work?impl->work->records():VkDescriptorBufferInfo{};}
VkDescriptorBufferInfo NativeMToonFrame::work_summary()const noexcept{return impl&&impl->work?impl->work->summary():VkDescriptorBufferInfo{};}
const std::vector<NativeMToonFrameShadeDraw>& NativeMToonFrame::shade_draws()const noexcept{static const std::vector<NativeMToonFrameShadeDraw> empty;return impl?impl->shade:empty;}
std::uint32_t NativeMToonFrame::forward_actor_mask()const noexcept{return impl&&impl->usable()?impl->forward_actors:0;}
std::uint32_t NativeMToonFrame::generation()const noexcept{return impl?impl->generation:0;}
std::uint32_t NativeMToonFrame::draw_count()const noexcept{return impl?unsigned(impl->main.size()+impl->outlines.size()):0;}
bool NativeMToonFrame::has_outlines()const noexcept{return impl&&!impl->outlines.empty();}
void NativeMToonFrame::quarantine()noexcept{if(impl)impl->quarantine();}
}
