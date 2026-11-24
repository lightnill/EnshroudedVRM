#pragma once
#include "native_scene_image_metadata.hpp"
#include "native_submit.hpp"
#include <map>
#include <sstream>
#include <cstring>

namespace evrm {
// Bounded evidence for immutable, transfer-uploaded cubes. No GPU operations,
// backing lease, descriptor authority or ownership permission are granted here.
// A complete mip-zero upload and image dependency must be submitted successfully.
// A separate reader transition also requires observed original host completion
// before its submit and known compatible queue-family ownership.
class NativeCubeUpload {
    struct Image {std::uint64_t generation=0,revision=0;VkExtent3D extent{};VkQueue queue{};
        VkQueue upload_queue{};VkFence upload_fence{};std::uint64_t upload_revision=0;bool upload_complete=false;const char* completion="none";
        VkSharingMode sharing=VK_SHARING_MODE_MAX_ENUM;std::uint32_t family_count=0;std::array<std::uint32_t,8> families{};};
    struct Write {std::uint64_t generation=0,revision=0;std::uint32_t copied=0,visible=0;bool reader_only=false,completed_before_submit=false;};
    struct Command {std::map<VkImage,Write> writes;bool ended=false;};
    struct Allocation {VkCommandBuffer command{};VkCommandPool pool{};};
    struct Queue {VkQueue handle{};std::uint32_t family=UINT32_MAX;};
    std::array<Queue,32> queues_{};
    struct Fence {VkFence handle{};bool eligible=false;};
    std::array<Fence,512> fences_{};
    bool fence_capacity_exhausted_=false;
    // Diagnostic history is separate from permission state. Bounded recent
    // upload events survive command retirement and cannot admit an image.
    struct AuditEvent {
        const char* operation="";const char* reason="";VkCommandBuffer command{};VkQueue queue{};VkFence fence{};
        std::uint64_t revision=0,src_stage=0,dst_stage=0,src_access=0,dst_access=0;
        std::uint32_t copied=0,visible=0,regions=0,src_family=UINT32_MAX,dst_family=UINT32_MAX;
        VkImageLayout old_layout=VK_IMAGE_LAYOUT_UNDEFINED,new_layout=VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageSubresourceRange range{};VkOffset3D offset{};VkExtent3D extent{};VkResult result=VK_SUCCESS;
    };
    struct Audit {std::uint64_t generation=0,copies=0,barriers=0,submits=0;std::uint32_t count=0,next=0,dropped=0;std::array<AuditEvent,24> events{};};
    std::array<Allocation,512> allocations_{};
    std::map<VkImage,Image> images_;
    std::map<VkCommandBuffer,Command> commands_;
    std::map<VkImage,Audit> audits_;
    bool available_=true;
    std::uint64_t revision_=0;
    std::uint64_t copy_calls_=0,barrier_calls_=0,submit_calls_=0;
    AuditEvent* audit(VkImage h,const char* operation,const char* reason,VkCommandBuffer command=VK_NULL_HANDLE)noexcept{
        const auto found=audits_.find(h);if(found==audits_.end())return nullptr;auto& a=found->second;
        if(!std::strcmp(operation,"copy"))++a.copies;else if(!std::strcmp(operation,"barrier"))++a.barriers;else if(!std::strcmp(operation,"submit"))++a.submits;
        if(a.count>=a.events.size())++a.dropped;else++a.count;auto& e=a.events[a.next];a.next=(a.next+1)%a.events.size();e={};e.operation=operation;e.reason=reason;e.command=command;
        const auto image=images_.find(h);if(image!=images_.end())e.revision=image->second.revision;return &e;
    }
    void audit_command(VkCommandBuffer c,const char* operation,const char* reason,VkResult result=VK_SUCCESS)noexcept{
        const auto command=commands_.find(c);if(command==commands_.end())return;for(const auto& entry:command->second.writes)
            if(auto* e=audit(entry.first,operation,reason,c)){e->copied=entry.second.copied;e->visible=entry.second.visible;e->result=result;}
    }
    static std::uint32_t faces(const VkImageSubresourceRange& r)noexcept{
        if(r.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||r.baseMipLevel||!r.levelCount||r.baseArrayLayer>=6)return 0;
        const auto count=r.layerCount==VK_REMAINING_ARRAY_LAYERS?6-r.baseArrayLayer:r.layerCount;
        return count&&count<=6-r.baseArrayLayer?((1u<<count)-1u)<<r.baseArrayLayer:0;
    }
    void disable()noexcept{available_=false;images_.clear();commands_.clear();}
    void revise(Image& image)noexcept{
        if(revision_==UINT64_MAX){disable();return;}image.revision=++revision_;image.queue=VK_NULL_HANDLE;
        image.upload_queue=VK_NULL_HANDLE;image.upload_fence=VK_NULL_HANDLE;image.upload_revision=0;image.upload_complete=false;image.completion="none";
    }
    std::uint32_t queue_family(VkQueue q)const noexcept{for(const auto& known:queues_)if(known.handle==q)return known.family;return UINT32_MAX;}
    bool fence_eligible(VkFence f)const noexcept{for(const auto& known:fences_)if(known.handle==f&&f)return known.eligible;return false;}
    const char* fence_rejection(VkFence f)const noexcept{for(const auto& known:fences_)if(known.handle==f&&f)return "external-or-imported-fence";return fence_capacity_exhausted_?"fence-registry-capacity":"unobserved-fence";}
    bool ownership_compatible(const Image& image,VkQueue reader)const noexcept{
        const auto source=queue_family(image.upload_queue),destination=queue_family(reader);if(source==UINT32_MAX||destination==UINT32_MAX)return false;
        if(image.sharing==VK_SHARING_MODE_EXCLUSIVE)return source==destination;
        if(image.sharing!=VK_SHARING_MODE_CONCURRENT)return false;bool s=false,d=false;
        for(std::uint32_t n=0;n<image.family_count;++n){s=s||image.families[n]==source;d=d||image.families[n]==destination;}return s&&d;
    }
public:
    static constexpr std::size_t image_limit=128,command_limit=256;
    bool available()const noexcept{return available_;}
    bool tracks(VkImage h)const noexcept{return available_&&images_.find(h)!=images_.end();}
    bool has_pending_fence(VkFence fence)const noexcept{for(const auto& i:images_)if(i.second.upload_fence==fence&&fence)return true;return false;}
    bool completion_pending()const noexcept{for(const auto& i:images_)if(i.second.upload_queue&&!i.second.upload_complete)return true;return false;}
    template<class F>void each_pending_fence(F f)const noexcept{for(const auto& i:images_)if(i.second.upload_fence&&!i.second.upload_complete)f(i.second.upload_fence);}
    void fence_created(VkFence f,const VkFenceCreateInfo* info)noexcept{
        if(!f)return;fence_changing(1,&f);Fence* slot=nullptr;for(auto& known:fences_)if(known.handle==f){slot=&known;break;}
        if(!slot)for(auto& known:fences_)if(!known.handle){slot=&known;break;}if(!slot){fence_capacity_exhausted_=true;return;}
        *slot={f,info&&info->sType==VK_STRUCTURE_TYPE_FENCE_CREATE_INFO&&!info->pNext&&!(info->flags&~VK_FENCE_CREATE_SIGNALED_BIT)};
    }
    void fence_retired(VkFence f)noexcept{fence_changing(1,&f);for(auto& known:fences_)if(known.handle==f)known={};}
    void queue_observed(VkQueue handle,std::uint32_t family)noexcept{
        if(!handle||family==UINT32_MAX)return;for(auto& q:queues_)if(q.handle==handle){if(q.family!=family)disable();return;}
        for(auto& q:queues_)if(!q.handle){q={handle,family};return;}disable();
    }
    void fence_changing(std::uint32_t count,const VkFence* fences)noexcept{
        if(count>4096||(count&&!fences)){disable();return;}
        for(std::uint32_t n=0;n<count;++n)for(auto& entry:images_){auto& i=entry.second;if(!fences[n]||i.upload_fence!=fences[n])continue;
            if(auto* e=audit(entry.first,"fence-retire",i.upload_complete?"completed-receipt-retained":"pending-receipt-withdrawn")){e->fence=fences[n];e->queue=i.upload_queue;}
            i.upload_fence=VK_NULL_HANDLE;}
    }
    void fence_result(VkFence fence,VkResult result,bool unambiguous=true,const char* operation="fence-wait")noexcept{
        for(auto& entry:images_){auto& i=entry.second;if(!fence||i.upload_fence!=fence)continue;
            const bool complete=result==VK_SUCCESS&&unambiguous&&i.upload_queue&&i.upload_revision==i.revision;
            if(auto* e=audit(entry.first,operation,complete?"upload-complete":!unambiguous?"wait-any-ambiguous":"not-complete")){e->fence=fence;e->queue=i.upload_queue;e->result=result;}
            if(complete){i.upload_complete=true;i.upload_fence=VK_NULL_HANDLE;i.completion=operation;}
        }
    }
    void queue_idle(VkQueue queue,VkResult result)noexcept{
        for(auto& entry:images_){auto& i=entry.second;if(!i.upload_queue||(queue&&queue!=i.upload_queue)||i.upload_revision!=i.revision)continue;
            if(auto* e=audit(entry.first,queue?"queue-idle":"device-idle",result==VK_SUCCESS?"upload-complete":"not-complete")){e->queue=i.upload_queue;e->result=result;}
            if(result==VK_SUCCESS){i.upload_complete=true;i.upload_fence=VK_NULL_HANDLE;i.completion=queue?"queue-idle":"device-idle";}
        }
    }
    template<class F>void each_image(F f)const noexcept{for(const auto& i:images_)f(i.first);}
    template<class F>void each_command(F f)const noexcept{for(const auto& c:commands_)f(c.first);}
    void unknown()noexcept{disable();}
    void reset_commands()noexcept{for(const auto& c:commands_)for(const auto& w:c.second.writes)unobserved_write(w.first);commands_.clear();}
    void allocated(const VkCommandBufferAllocateInfo& info,const VkCommandBuffer* commands)noexcept{
        if(!available_)return;if(info.commandBufferCount>allocations_.size()||(info.commandBufferCount&&!commands)){disable();return;}
        for(std::uint32_t n=0;n<info.commandBufferCount;++n){Allocation* slot=nullptr;
            for(auto& a:allocations_)if(a.command==commands[n]){slot=&a;break;}
            if(!slot)for(auto& a:allocations_)if(!a.command){slot=&a;break;}
            if(!slot){disable();return;}*slot={commands[n],info.commandPool};}
    }
    void freed(std::uint32_t count,const VkCommandBuffer* commands)noexcept{
        if(count>4096||(count&&!commands)){disable();return;}for(std::uint32_t n=0;n<count;++n){command_reset(commands[n]);for(auto& a:allocations_)if(a.command==commands[n])a={};}
    }
    void pool_reset(VkCommandPool pool,bool destroyed)noexcept{
        for(auto c=commands_.begin();c!=commands_.end();){VkCommandPool owner{};for(const auto& a:allocations_)if(a.command==c->first){owner=a.pool;break;}
            if(!owner){for(const auto& w:c->second.writes)unobserved_write(w.first);c=commands_.erase(c);}
            else if(owner==pool){audit_command(c->first,"retire",destroyed?"pool-destroy":"pool-reset");c=commands_.erase(c);}else++c;}
        if(destroyed)for(auto& a:allocations_)if(a.pool==pool)a={};
    }
    void image_created(const NativeSceneImageFacts& f)noexcept{
        try{const auto h=f.identity.image;images_.erase(h);audits_.erase(h);if(!available_||!h||!f.identity.image_generation)return;
            constexpr auto allowed=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            if(f.image_type!=VK_IMAGE_TYPE_2D||!(f.image_flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)||f.image_layers!=6||
               f.image_extent.width!=f.image_extent.height||f.image_extent.depth!=1||!f.image_extent.width||
               f.samples!=VK_SAMPLE_COUNT_1_BIT||f.tiling!=VK_IMAGE_TILING_OPTIMAL||
               (f.image_usage&~allowed)||(f.image_usage&(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT))!=(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT))return;
            if(images_.size()>=image_limit){disable();return;}
            images_[h]={f.identity.image_generation,0,f.image_extent,{}};
            auto& tracked=images_[h];tracked.sharing=f.sharing_mode;tracked.family_count=f.queue_family_count;tracked.families=f.queue_families;
            if(audits_.size()<image_limit){audits_[h].generation=f.identity.image_generation;if(auto* e=audit(h,"create","tracked")){e->extent=f.image_extent;e->regions=f.image_mips;}}
        }catch(...){disable();}
    }
    void image_destroyed(VkImage h)noexcept{images_.erase(h);audits_.erase(h);}
    void command_reset(VkCommandBuffer c)noexcept{audit_command(c,"retire","command-reset");commands_.erase(c);}
    void command_end(VkCommandBuffer c,VkResult result)noexcept{
        const auto found=commands_.find(c);if(found!=commands_.end()){found->second.ended=result==VK_SUCCESS;audit_command(c,"end",result==VK_SUCCESS?"success":"failed",result);}
    }
    void unobserved_write(VkImage h)noexcept{
        // Its command association/producer is unknown: this generation can no
        // longer be qualified by a later recording that executes before it.
        audit(h,"reject","unobserved-write");images_.erase(h);
    }
    void invalidate_recorded(VkCommandBuffer c,VkImage h)noexcept{
        try{const auto i=images_.find(h);if(i==images_.end())return;revise(i->second);if(!available_)return;
            if(!c||(commands_.find(c)==commands_.end()&&commands_.size()>=command_limit)){disable();return;}
            auto& cmd=commands_[c];cmd.writes[h]={i->second.generation,i->second.revision,0,0};
        }catch(...){disable();}
    }
    void copy(VkCommandBuffer c,VkImage h,VkImageLayout layout,std::uint32_t count,const VkBufferImageCopy* regions)noexcept{
        try{const auto i=images_.find(h);if(i==images_.end()||!available_)return;
            ++copy_calls_;
            if(auto* e=audit(h,"copy","observed",c)){e->new_layout=layout;e->regions=count;}
            auto& image=i->second;const auto prior=image.revision;revise(image);if(!available_)return;
            if(!c||layout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL||!count||count>96||!regions){audit(h,"copy-reject","unsupported-shape",c);unobserved_write(h);return;}
            if(commands_.find(c)==commands_.end()&&commands_.size()>=command_limit){disable();return;}
            auto& cmd=commands_[c];if(cmd.ended){audit(h,"copy-reject","command-ended",c);return;}auto& w=cmd.writes[h];
            if(w.generation!=image.generation||w.revision!=prior)w={image.generation,0,0,0};
            w.revision=image.revision;w.visible=0;
            for(std::uint32_t n=0;n<count;++n){const auto& r=regions[n];
                if(r.imageSubresource.mipLevel)continue;
                const auto mask=faces({r.imageSubresource.aspectMask,0,1,r.imageSubresource.baseArrayLayer,r.imageSubresource.layerCount});
                auto* event=audit(h,"mip0-region","full-face",c);if(event){event->range={r.imageSubresource.aspectMask,0,1,r.imageSubresource.baseArrayLayer,r.imageSubresource.layerCount};event->offset=r.imageOffset;event->extent=r.imageExtent;event->copied=mask;}
                if(!mask||r.imageOffset.x||r.imageOffset.y||r.imageOffset.z||r.imageExtent.width!=image.extent.width||
                   r.imageExtent.height!=image.extent.height||r.imageExtent.depth!=1){if(event)event->reason="partial-or-invalid-region";w.copied=0;return;}
                w.copied|=mask;
            }
            if(auto* e=audit(h,"copy-result",w.copied==63?"all-six-faces":"incomplete-faces",c)){e->copied=w.copied;e->visible=w.visible;}
        }catch(...){disable();}
    }
    void barrier(VkCommandBuffer c,VkImage h,VkImageLayout old_layout,VkImageLayout new_layout,
                 std::uint32_t src_family,std::uint32_t dst_family,const VkImageSubresourceRange& range,
                 std::uint64_t src_stage,std::uint64_t dst_stage,std::uint64_t src_access,std::uint64_t dst_access)noexcept{
        const auto i=images_.find(h);if(i==images_.end()||!available_||range.baseMipLevel)return;
        ++barrier_calls_;
        auto* event=audit(h,"barrier","observed",c);if(event){event->old_layout=old_layout;event->new_layout=new_layout;event->src_stage=src_stage;event->dst_stage=dst_stage;event->src_access=src_access;event->dst_access=dst_access;event->src_family=src_family;event->dst_family=dst_family;event->range=range;}
        const auto mask=faces(range);if(!mask){if(event)event->reason="invalid-range";return;}
        if(old_layout==VK_IMAGE_LAYOUT_UNDEFINED||new_layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||
           src_family!=VK_QUEUE_FAMILY_IGNORED||dst_family!=VK_QUEUE_FAMILY_IGNORED){if(event)event->reason=old_layout==VK_IMAGE_LAYOUT_UNDEFINED?"discard":new_layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL?"not-read-only":"queue-family-indices";invalidate_recorded(c,h);return;}
        auto command=commands_.find(c);
        // A completed upload may be followed by its image transition in a
        // different command/queue. This records a candidate only: the original
        // reader submit must later prove completion and compatible ownership.
        if((command==commands_.end()||command->second.writes.find(h)==command->second.writes.end())&&i->second.upload_queue&&i->second.upload_revision==i->second.revision){
            try{if(command==commands_.end()&&commands_.size()>=command_limit){disable();return;}
                auto& candidate=commands_[c];if(!candidate.ended)candidate.writes[h]={i->second.generation,i->second.revision,63,0,true};command=commands_.find(c);
            }catch(...){disable();return;}
        }
        if(command==commands_.end()||command->second.ended){if(event)event->reason=command==commands_.end()?"no-command-copy":"command-ended";return;}
        const auto write=command->second.writes.find(h);if(write==command->second.writes.end()){if(event)event->reason="no-image-copy";return;}auto& w=write->second;
        const bool transfer=(src_stage&(VK_PIPELINE_STAGE_2_COPY_BIT|VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT|VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT))!=0;
        const bool compute=(dst_stage&(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT))!=0;
        if(w.generation==i->second.generation&&w.revision==i->second.revision&&old_layout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&transfer&&compute&&
           (src_access&(VK_ACCESS_2_TRANSFER_WRITE_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT))&&(dst_access&(VK_ACCESS_2_SHADER_READ_BIT|VK_ACCESS_2_SHADER_SAMPLED_READ_BIT|VK_ACCESS_2_MEMORY_READ_BIT)))w.visible|=mask&w.copied;
        if(event){event->copied=w.copied;event->visible=w.visible;event->reason=w.visible==63?"all-six-visible":
            w.generation!=i->second.generation||w.revision!=i->second.revision?"stale-copy":old_layout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL?"old-layout":!transfer?"source-stage":!compute?"destination-stage":
            !(src_access&(VK_ACCESS_2_TRANSFER_WRITE_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT))?"source-access":!(dst_access&(VK_ACCESS_2_SHADER_READ_BIT|VK_ACCESS_2_SHADER_SAMPLED_READ_BIT|VK_ACCESS_2_MEMORY_READ_BIT))?"destination-access":"incomplete-faces";}
    }
    void legacy(VkCommandBuffer c,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,std::uint32_t count,const VkImageMemoryBarrier* barriers)noexcept{
        if(!available_||!count)return;if(!barriers||count>512){disable();return;}
        for(std::uint32_t n=0;n<count;++n){const auto& b=barriers[n];if(flags||b.pNext){unobserved_write(b.image);continue;}
            barrier(c,b.image,b.oldLayout,b.newLayout,b.srcQueueFamilyIndex,b.dstQueueFamilyIndex,b.subresourceRange,src,dst,b.srcAccessMask,b.dstAccessMask);}
    }
    void sync2(VkCommandBuffer c,const VkDependencyInfo* info)noexcept{
        if(!available_||!info)return;if(info->imageMemoryBarrierCount>512||(info->imageMemoryBarrierCount&&!info->pImageMemoryBarriers)){disable();return;}
        for(std::uint32_t n=0;n<info->imageMemoryBarrierCount;++n){const auto& b=info->pImageMemoryBarriers[n];
            if(info->pNext||info->dependencyFlags||b.pNext){unobserved_write(b.image);continue;}
            barrier(c,b.image,b.oldLayout,b.newLayout,b.srcQueueFamilyIndex,b.dstQueueFamilyIndex,b.subresourceRange,b.srcStageMask,b.dstStageMask,b.srcAccessMask,b.dstAccessMask);}
    }
    void submitted(VkQueue q,std::uint32_t count,const VkSubmitInfo* submissions,VkResult result,VkFence fence=VK_NULL_HANDLE)noexcept{
        if(!available_||!q||!count)return;if(count>256||!submissions){disable();return;}
        for(std::uint32_t s=0;s<count;++s){const auto& submit=submissions[s];
            if(submit.commandBufferCount>4096||(submit.commandBufferCount&&!submit.pCommandBuffers)){disable();return;}
            bool shape=submit.sType==VK_STRUCTURE_TYPE_SUBMIT_INFO;
            if(submit.pNext){const auto& t=*static_cast<const VkTimelineSemaphoreSubmitInfo*>(submit.pNext);
                shape=shape&&t.sType==VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO&&!t.pNext&&
                    t.waitSemaphoreValueCount==submit.waitSemaphoreCount&&t.signalSemaphoreValueCount==submit.signalSemaphoreCount;}
            for(std::uint32_t c=0;c<submit.commandBufferCount;++c){const auto command=commands_.find(submit.pCommandBuffers[c]);if(command==commands_.end())continue;++submit_calls_;
                for(const auto& entry:command->second.writes){const auto i=images_.find(entry.first);if(i==images_.end())continue;const auto& w=entry.second;
                    auto* event=audit(entry.first,"submit","observed",submit.pCommandBuffers[c]);if(event){event->queue=q;event->result=result;event->copied=w.copied;event->visible=w.visible;
                        event->fence=fence;
                        event->regions=submit.commandBufferCount;event->src_family=submit.waitSemaphoreCount;event->dst_family=submit.signalSemaphoreCount;
                        event->src_stage=submit.pNext?static_cast<const VkBaseInStructure*>(submit.pNext)->sType:0;}
                    if(i->second.generation!=w.generation||i->second.revision!=w.revision){if(event)event->reason="stale-recording";revise(i->second);if(!available_)return;continue;}
                    const bool uploaded=shape&&result==VK_SUCCESS&&command->second.ended&&w.copied==63;
                    if(uploaded&&!w.reader_only){i->second.upload_queue=q;i->second.upload_fence=fence_eligible(fence)?fence:VK_NULL_HANDLE;i->second.upload_revision=w.revision;i->second.upload_complete=false;i->second.completion="none";
                        if(fence&&!i->second.upload_fence)audit(entry.first,"fence-reject",fence_rejection(fence),submit.pCommandBuffers[c]);}
                    const bool handoff=!w.reader_only||(w.completed_before_submit&&i->second.upload_complete&&i->second.upload_revision==w.revision&&ownership_compatible(i->second,q));
                    i->second.queue=uploaded&&w.visible==63&&handoff?q:VK_NULL_HANDLE;
                    if(event)event->reason=i->second.queue?"qualified":!shape?"unsupported-submit":result!=VK_SUCCESS?"failed-submit":!command->second.ended?"command-not-ended":w.copied!=63?"incomplete-faces":w.visible!=63?"missing-visible-dependency":!w.completed_before_submit||!i->second.upload_complete?"upload-not-complete-before-submit":"queue-family-ownership";
                }
            }
        }
    }
    void before_submit(std::uint32_t count,const VkSubmitInfo* submissions)noexcept{
        if(!available_||!count)return;if(count>256||!submissions){disable();return;}
        for(std::uint32_t s=0;s<count;++s){const auto& submit=submissions[s];
            if(submit.commandBufferCount>4096||(submit.commandBufferCount&&!submit.pCommandBuffers)){disable();return;}
            for(std::uint32_t c=0;c<submit.commandBufferCount;++c){const auto command=commands_.find(submit.pCommandBuffers[c]);if(command==commands_.end())continue;
                for(auto& entry:command->second.writes){const auto i=images_.find(entry.first);if(i!=images_.end()){i->second.queue=VK_NULL_HANDLE;
                    entry.second.completed_before_submit=entry.second.reader_only&&i->second.upload_complete&&i->second.upload_revision==entry.second.revision;
                    if(!entry.second.reader_only){i->second.upload_queue=VK_NULL_HANDLE;i->second.upload_fence=VK_NULL_HANDLE;i->second.upload_revision=0;i->second.upload_complete=false;i->second.completion="none";}}}}
        }
    }
    void executed(std::uint32_t count,const VkCommandBuffer* children)noexcept{
        if(!available_||!count)return;if(count>4096||!children){disable();return;}
        for(std::uint32_t c=0;c<count;++c){const auto command=commands_.find(children[c]);if(command==commands_.end())continue;
            for(const auto& entry:command->second.writes)unobserved_write(entry.first);}
    }
    void submitted2(std::uint32_t count,const VkSubmitInfo2* submissions)noexcept{
        if(!available_||!count)return;if(count>256||!submissions){disable();return;}
        for(std::uint32_t s=0;s<count;++s){const auto& submit=submissions[s];
            if(submit.commandBufferInfoCount>4096||(submit.commandBufferInfoCount&&!submit.pCommandBufferInfos)){disable();return;}
            for(std::uint32_t c=0;c<submit.commandBufferInfoCount;++c){const auto command=commands_.find(submit.pCommandBufferInfos[c].commandBuffer);
                if(command==commands_.end())continue;for(const auto& entry:command->second.writes)unobserved_write(entry.first);}
        }
    }
    bool initialized(const NativeSceneImageFacts& f,VkQueue q)const noexcept{
        if(!available_||!f||!q||f.view_type!=VK_IMAGE_VIEW_TYPE_CUBE||f.range.baseMipLevel||f.range.baseArrayLayer||f.range.layerCount!=6)return false;
        const auto i=images_.find(f.identity.image);return i!=images_.end()&&i->second.generation==f.identity.image_generation&&i->second.queue==q;
    }
    std::string diagnostic(VkImage h,VkQueue q)const{
        const auto i=images_.find(h);std::uint32_t copied=0,visible=0;bool ended=false;
        if(i!=images_.end())for(const auto& c:commands_){const auto w=c.second.writes.find(h);if(w!=c.second.writes.end()&&w->second.revision==i->second.revision){copied=w->second.copied;visible=w->second.visible;ended=c.second.ended;}}
        std::size_t allocations=0,fence_count=0;for(const auto& a:allocations_)if(a.command)++allocations;for(const auto& f:fences_)if(f.handle)++fence_count;
        std::ostringstream out;out<<"available="<<available_<<" tracked="<<(i!=images_.end())<<" images="<<images_.size()<<" commands="<<commands_.size()<<" allocations="<<allocations
            <<" fences="<<fence_count<<" fence-capacity-exhausted="<<fence_capacity_exhausted_<<" copies="<<copy_calls_<<" barriers="<<barrier_calls_<<" submits="<<submit_calls_<<" faces="<<copied<<" visible="<<visible<<" ended="<<ended
            <<" producer_queue="<<(i==images_.end()?VK_NULL_HANDLE:i->second.queue)<<" reader_queue="<<q;
        if(i!=images_.end()){const auto& tracked=i->second;out<<" upload-queue="<<tracked.upload_queue<<" upload-fence="<<tracked.upload_fence<<" upload-complete="<<tracked.upload_complete
            <<" upload-completion="<<tracked.completion<<" upload-revision="<<tracked.upload_revision<<" sharing="<<tracked.sharing<<" queue-families="<<queue_family(tracked.upload_queue)<<","<<queue_family(q);}
        const auto history=audits_.find(h);if(history!=audits_.end()){const auto& a=history->second;out<<" history-generation="<<a.generation<<" history-counts="<<a.copies<<","<<a.barriers<<","<<a.submits<<" history-dropped="<<a.dropped<<" history=[";
            for(std::uint32_t n=0;n<a.count;++n){const auto& e=a.events[(a.count<a.events.size()?n:a.next+n)%a.events.size()];if(n)out<<";";out<<e.operation<<":"<<e.reason<<",cb="<<e.command<<",rev="<<e.revision;
                if(!std::strcmp(e.operation,"create"))out<<",extent="<<e.extent.width<<","<<e.extent.height<<","<<e.extent.depth<<",mips="<<e.regions;
                else if(!std::strcmp(e.operation,"copy"))out<<",layout="<<e.new_layout<<",regions="<<e.regions;
                else if(!std::strcmp(e.operation,"mip0-region"))out<<",layers="<<e.range.baseArrayLayer<<","<<e.range.layerCount<<",offset="<<e.offset.x<<","<<e.offset.y<<","<<e.offset.z<<",extent="<<e.extent.width<<","<<e.extent.height<<","<<e.extent.depth;
                else if(!std::strcmp(e.operation,"barrier"))out<<",layouts="<<e.old_layout<<","<<e.new_layout<<",families="<<e.src_family<<","<<e.dst_family<<",stages="<<e.src_stage<<","<<e.dst_stage<<",access="<<e.src_access<<","<<e.dst_access<<",range="<<e.range.aspectMask<<","<<e.range.baseMipLevel<<","<<e.range.levelCount<<","<<e.range.baseArrayLayer<<","<<e.range.layerCount;
                else if(!std::strcmp(e.operation,"submit"))out<<",queue="<<e.queue<<",fence="<<e.fence<<",result="<<e.result<<",commands="<<e.regions<<",waits-signals="<<e.src_family<<","<<e.dst_family<<",next-type="<<e.src_stage;
                else if(e.queue||e.fence)out<<",queue="<<e.queue<<",fence="<<e.fence<<",result="<<e.result;
                out<<",faces="<<e.copied<<",visible="<<e.visible;
            }out<<"]";
        }return out.str();
    }
};
}
