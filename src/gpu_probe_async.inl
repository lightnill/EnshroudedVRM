// Included inside namespace evrm, after the synchronous diagnostic implementation.
// Copies are part of the native batch: its signals and fence therefore cover
// every borrowed source read. The following empty submit owns only a fence.
void GpuProbe::buffer_bound(VkBuffer buffer,VkDeviceMemory memory,bool ordinary) {
    if(!enabled())return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    const auto found=impl->buffers.find(buffer);
    if(found!=impl->buffers.end())found->second.memory=ordinary?memory:VK_NULL_HANDLE;
}
void GpuProbe::memory_destroyed(VkDeviceMemory memory) {
    if(!enabled()||!memory)return;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        for(auto& buffer:impl->buffers)if(buffer.second.memory==memory)buffer.second.memory=VK_NULL_HANDLE;
    }
    std::lock_guard<std::mutex> lock(impl->readback_mutex);
    for(auto& slot:impl->async_slots) {
        bool borrowed=false;for(const auto& lease:slot.plan.memories)borrowed|=lease.second==memory;
        if(borrowed){impl->drain_slot(slot);slot.ready=false;slot.plan={};}
    }
}
bool GpuProbe::Impl::complete_slot(AsyncSlot& slot,bool wait) {
    if(!slot.pending)return true;
    const auto result=wait?proc<PFN_vkWaitForFences>("vkWaitForFences")(device,1,&slot.fence,VK_TRUE,UINT64_MAX):
        proc<PFN_vkGetFenceStatus>("vkGetFenceStatus")(device,slot.fence);
    if(result==VK_SUCCESS){slot.pending=false;slot.ready=true;return true;}
    if(result==VK_NOT_READY||result==VK_TIMEOUT)return false;
    async_disabled=true;
    if(result==VK_ERROR_DEVICE_LOST){slot.pending=false;slot.ready=false;slot.plan={};return true;}
    return false; // An error is not completion; preserve all pending resources.
}
void GpuProbe::Impl::drain_slot(AsyncSlot& slot) {
    while(slot.pending&&!complete_slot(slot,true))Sleep(1);
}
void GpuProbe::Impl::release_slot(AsyncSlot& slot) {
    if(slot.pending)return;
    if(slot.mapped)proc<PFN_vkUnmapMemory>("vkUnmapMemory")(device,slot.memory);
    if(slot.fence)proc<PFN_vkDestroyFence>("vkDestroyFence")(device,slot.fence,nullptr);
    if(slot.pool)proc<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device,slot.pool,nullptr);
    if(slot.buffer)proc<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device,slot.buffer,nullptr);
    if(slot.memory)proc<PFN_vkFreeMemory>("vkFreeMemory")(device,slot.memory,nullptr);
    slot={};
}
void GpuProbe::Impl::record_copy(VkCommandBuffer target,AsyncSlot& slot,const CapturePlan& plan) {
    VkMemoryBarrier before{};before.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;before.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(target,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&before,0,nullptr,0,nullptr);
    for(const auto& read:plan.transfer_reads.empty()?plan.reads:plan.transfer_reads) {
        const VkBufferCopy copy{read.target.binding.offset,read.destination,read.size};
        proc<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer")(target,read.target.binding.buffer,slot.buffer,1,&copy);
    }
    // Also order the read before future native writes, including work in a
    // subsequent submit on this queue. Host visibility alone would not do so.
    VkMemoryBarrier after{};after.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    after.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    after.dstAccessMask=VK_ACCESS_HOST_READ_BIT|VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(target,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,nullptr,0,nullptr);
}
void GpuProbe::Impl::prepare_slot(AsyncSlot& slot,VkQueue queue,CapturePlan plan) {
    if(slot.pending||slot.ready||slot.recorded)throw std::runtime_error("Async slot is still leased");
    if(plan.reads.empty()||!plan.total||plan.total>readback_budget)throw std::runtime_error("Invalid async capture plan");
    if(slot.capacity<plan.total||slot.family!=plan.family) {
        release_slot(slot);
        try {
            slot.capacity=std::min(readback_budget,(plan.total+65535u)&~VkDeviceSize(65535u));slot.family=plan.family;
            VkBufferCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;ci.size=slot.capacity;ci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            check(proc<PFN_vkCreateBuffer>("vkCreateBuffer")(device,&ci,nullptr,&slot.buffer),"async staging buffer");
            VkMemoryRequirements requirements{};proc<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device,slot.buffer,&requirements);
            uint32_t type=memory_properties.memoryTypeCount;
            constexpr auto required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            for(uint32_t i=0;i<memory_properties.memoryTypeCount;++i) {
                const auto flags=memory_properties.memoryTypes[i].propertyFlags;
                if(!(requirements.memoryTypeBits&(1u<<i))||(flags&required)!=required)continue;
                if(type==memory_properties.memoryTypeCount)type=i;
                if(flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT){type=i;break;}
            }
            if(type==memory_properties.memoryTypeCount)throw std::runtime_error("No coherent async memory");
            VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=requirements.size;ai.memoryTypeIndex=type;
            check(proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device,&ai,nullptr,&slot.memory),"async memory");
            check(proc<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device,slot.buffer,slot.memory,0),"async binding");
            check(proc<PFN_vkMapMemory>("vkMapMemory")(device,slot.memory,0,slot.capacity,0,&slot.mapped),"async mapping");
            VkCommandPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pi.queueFamilyIndex=plan.family;pi.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            check(proc<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device,&pi,nullptr,&slot.pool),"async command pool");
            VkCommandBufferAllocateInfo ca{};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=slot.pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
            check(proc<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device,&ca,&slot.command),"async command");
            if(set_loader_data)check(set_loader_data(device,slot.command),"async loader data");
            VkFenceCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            check(proc<PFN_vkCreateFence>("vkCreateFence")(device,&fi,nullptr,&slot.fence),"async fence");
        }catch(...){release_slot(slot);throw;}
    }else {
        check(proc<PFN_vkResetCommandPool>("vkResetCommandPool")(device,slot.pool,0),"async pool reset");
        check(proc<PFN_vkResetFences>("vkResetFences")(device,1,&slot.fence),"async fence reset");
    }
    VkCommandBufferBeginInfo bi{};bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(proc<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(slot.command,&bi),"async begin");
    record_copy(slot.command,slot,plan);
    check(proc<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(slot.command),"async end");
    slot.queue=queue;slot.plan=std::move(plan);
}
void GpuProbe::configure_async(VkQueue queue,const std::map<std::string,std::size_t>& limits) noexcept {
    if(!enabled()||player_anchor_capture_enabled())return;
    try {
        std::lock_guard<std::mutex> lock(impl->readback_mutex);
        if(impl->async_disabled)return;
        if(impl->async_queue&&impl->async_queue!=queue){impl->async_disabled=true;return;}
        if(!impl->async_queue)log_event("probe.async.enabled slots=3 maximum-slot-bytes=33554432");
        impl->async_queue=queue;impl->async_limits=limits;
    }catch(...){}
}
bool GpuProbe::async_frame_scheduled(VkQueue queue) const noexcept {
    try {std::lock_guard<std::mutex> lock(impl->readback_mutex);return !impl->async_disabled&&impl->async_queue==queue&&impl->async_frame;}
    catch(...){return false;}
}
VkResult GpuProbe::submit_async(VkQueue queue,uint32_t count,const VkSubmitInfo* submits,VkFence fence,PFN_vkQueueSubmit next,uint64_t epoch) noexcept {
    if(!enabled()||!next)return next?next(queue,count,submits,fence):VK_ERROR_INITIALIZATION_FAILED;
    bool called=false;VkResult result=VK_ERROR_OUT_OF_HOST_MEMORY;
    try {
        std::lock_guard<std::mutex> copying(impl->readback_mutex);
        // Reuse the native backend's existing single-device shape proof. Its
        // one known timeline payload is unchanged because no semaphore count,
        // value or signal scope is changed. Unknown/protected chains decline.
        if(!count||count>64||!submits)return next(queue,count,submits,fence);
        std::vector<Impl::AsyncSlot*> selected;
        std::set<VkCommandBuffer> inline_commands;
        {std::lock_guard<std::mutex> lock(impl->mutex);
            for(uint32_t i=0;i<count;++i)for(uint32_t c=0;c<submits[i].commandBufferCount;++c){const auto command=submits[i].pCommandBuffers[c];
                const auto state=impl->commands.find(command);
                if(state!=impl->commands.end()&&state->second.inline_required)inline_commands.insert(command);
                for(auto& slot:impl->async_slots)if(slot.recorded&&slot.original==command&&state!=impl->commands.end()&&
                    slot.original_generation==state->second.generation&&std::find(selected.begin(),selected.end(),&slot)==selected.end())selected.push_back(&slot);
            }
        }
        bool supported=!impl->async_disabled&&impl->async_queue==queue&&epoch;
        for(uint32_t i=0;i<count;++i)supported=supported&&native_legacy_submit_supported(submits[i]);
        if(!supported){called=true;result=next(queue,count,submits,fence);
            if(!selected.empty()){impl->mark_submitted(queue,selected,epoch,false);if(result!=VK_SUCCESS)for(auto* slot:selected)slot->recorded=true;}
            return result;
        }
        if(++impl->async_observed<=3) {
            std::string detail="probe.async.submit-shape batches="+std::to_string(count);
            for(uint32_t i=0;i<count;++i)detail+=" commands="+std::to_string(submits[i].commandBufferCount)+" chain="+std::to_string(submits[i].pNext?static_cast<const VkBaseInStructure*>(submits[i].pNext)->sType:0);
            log_event(detail);
        }
        std::vector<VkSubmitInfo> batches(submits,submits+count);
        std::vector<std::vector<VkCommandBuffer>> commands(count);
        bool planned=false;
        for(uint32_t i=0;i<count;++i) {
            const auto& batch=submits[i];
            if(!batch.commandBufferCount)continue;
            for(uint32_t c=0;c<batch.commandBufferCount;++c){
                const auto original=batch.pCommandBuffers[c];commands[i].push_back(original);
                if(inline_commands.count(original)){planned=true;continue;}
                std::vector<SubmittedDraw> draws;
                {std::lock_guard<std::mutex> lock(impl->mutex);impl->resolve_command(queue,original,draws);}
                if(draws.empty())continue;
                auto plan=impl->make_plan(queue,impl->async_limits,draws,true);
                if(plan.reads.empty())continue;
                planned=true;
                // Shared culling/indirect scratch may be overwritten by the next
                // primary, even in this batch. Copy directly after its producer.
                // The original batch's signals still cover every borrowed read.
                Impl::AsyncSlot* available=nullptr;
                for(auto& slot:impl->async_slots)if(!slot.pending&&!slot.ready&&!slot.recorded&&
                    std::find(selected.begin(),selected.end(),&slot)==selected.end()){available=&slot;break;}
                if(!available){++impl->async_skipped;continue;}
                impl->prepare_slot(*available,queue,std::move(plan));commands[i].push_back(available->command);
                available->epoch=epoch;available->started_ms=GetTickCount64();
                selected.push_back(available);
            }
            if(commands[i].size()!=batch.commandBufferCount){batches[i].commandBufferCount=static_cast<uint32_t>(commands[i].size());batches[i].pCommandBuffers=commands[i].data();}
        }
        // Ready slots belong to the consumer until it decodes them. A full
        // ring skips sampling and keeps the native submit moving.
        if(selected.empty()){const auto forwarded=next(queue,count,submits,fence);if(planned&&forwarded==VK_SUCCESS)impl->async_frame=true;return forwarded;}
        called=true;result=next(queue,count,batches.data(),fence);
        if(result!=VK_SUCCESS){std::vector<Impl::AsyncSlot*> recorded;
            for(auto* slot:selected)if(slot->recorded)recorded.push_back(slot);else slot->plan={};
            if(!recorded.empty()){impl->mark_submitted(queue,recorded,epoch,false);for(auto* slot:recorded)slot->recorded=true;}
            return result;
        }
        // Native completion now covers the copy. This marker adds no source
        // access and never borrows or consumes an application semaphore/fence.
        impl->mark_submitted(queue,selected,epoch,true);
        return result;
    }catch(const std::exception& error){try{log_event("probe.async.prepare-rejected "+std::string(error.what()));}catch(...){}}
    catch(...){}
    return called?result:next(queue,count,submits,fence);
}
GpuSnapshot GpuProbe::poll_async(VkQueue queue) noexcept {
    GpuSnapshot latest;
    try {
        std::lock_guard<std::mutex> lock(impl->readback_mutex);
        if(impl->async_disabled||impl->async_queue!=queue)return latest;
        std::vector<GpuSnapshot> views;
        const auto retain=[&](GpuSnapshot view){
            const auto binding=view.buffers.at("instances").binding;
            const auto found=std::find_if(views.begin(),views.end(),[&](const auto& previous){
                const auto source=previous.buffers.at("instances").binding;
                return source.buffer==binding.buffer&&source.offset==binding.offset&&source.range==binding.range;
            });
            if(found==views.end())views.push_back(std::move(view));else if(found->sequence<view.sequence)*found=std::move(view);
        };
        bool rejected=false;
        for(auto& slot:impl->async_slots) {
            if(slot.recorded&&!slot.pending)continue;
            const auto completed=impl->complete_slot(slot,false);
            // A lost/uncertain device invalidates the entire pass, including
            // bytes decoded before this error. Never touch another mapped slot.
            if(impl->async_disabled)return {};
            if(!completed){++impl->async_pending_polls;continue;}
            if(!slot.ready)continue;
            if(!slot.publishable||slot.queue!=queue){slot.ready=false;if(!slot.recorded)slot.plan={};++impl->async_rejected;rejected=true;continue;}
            try {
                const auto now=GetTickCount64();
                if(now<slot.started_ms||now-slot.started_ms>250)throw std::runtime_error("Async pose expired");
                auto captured=impl->decode_plan(slot.plan,slot.mapped,true);
                captured.source_epoch=slot.epoch;captured.captured_milliseconds=slot.started_ms;
                captured.sequence=slot.sequence;captured.asynchronous=true;
                for(auto& view:captured.additional_views){view.source_epoch=slot.epoch;view.captured_milliseconds=slot.started_ms;
                    view.sequence=slot.sequence;view.asynchronous=true;}
                auto children=std::move(captured.additional_views);captured.additional_views.clear();
                retain(std::move(captured));for(auto& view:children)retain(std::move(view));
                ++impl->async_decoded;
            }catch(...){++impl->async_rejected;rejected=true;}
            slot.ready=false;slot.plan={};
        }
        auto ordered=std::move(views);
        std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.sequence>b.sequence;});
        if(!ordered.empty()){
            latest=std::move(ordered.front());
            for(std::size_t i=1;i<ordered.size()&&latest.additional_views.size()<3;++i)
                if(ordered[i].source_epoch==latest.source_epoch)latest.additional_views.push_back(std::move(ordered[i]));
        }
        // Completed rejection is different from unfinished work: permit the
        // existing synchronous recovery path rather than starving it forever.
        if(latest.buffers.empty()&&rejected)impl->async_frame=false;
        if(latest.sequence&&impl->async_decoded%300==0)log_event("probe.async.stats enqueued="+std::to_string(impl->async_enqueued)+" decoded="+std::to_string(impl->async_decoded)+" pending-polls="+std::to_string(impl->async_pending_polls)+" ring-full="+std::to_string(impl->async_skipped)+" rejected="+std::to_string(impl->async_rejected)+" normal-host-waits=0");
    }catch(...){}
    return latest;
}
