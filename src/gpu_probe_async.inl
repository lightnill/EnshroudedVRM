// Included inside namespace evrm, after the synchronous diagnostic implementation.
// Copies are part of the native batch: its signals and fence therefore cover
// every borrowed source read. The following empty submit owns only a fence.
void GpuProbe::buffer_bound(VkBuffer buffer,VkDeviceMemory memory,bool ordinary) {
    if(!enabled())return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    const auto found=impl->buffers.find(buffer);
    if(found!=impl->buffers.end()){
        found->second.memory=ordinary?memory:VK_NULL_HANDLE;
    }
}
void GpuProbe::memory_destroyed(VkDeviceMemory memory) {
    if(!enabled()||!memory)return;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        for(auto& buffer:impl->buffers)if(buffer.second.memory==memory){
            buffer.second.memory=VK_NULL_HANDLE;
        }
    }
    std::lock_guard<std::mutex> lock(impl->readback_mutex);
    for(auto& slot:impl->async_slots) {
        bool borrowed=false;for(const auto& lease:slot.plan.memories)borrowed|=lease.second==memory;
        if(borrowed){
            impl->drain_slot(slot);slot.publishable=false;
            // A CPU job owns this staging mapping until it returns. Native
            // memory retirement invalidates its result, not its byte storage.
            if(!slot.decoding){slot.ready=false;slot.plan={};slot.decoded.reset();slot.decode_finished=false;}
        }
    }
}
bool GpuProbe::Impl::complete_slot(AsyncSlot& slot,bool wait) {
    if(!slot.pending)return true;
    const auto result=wait?proc<PFN_vkWaitForFences>("vkWaitForFences")(device,1,&slot.fence,VK_TRUE,UINT64_MAX):
        (async_fence_status?async_fence_status:proc<PFN_vkGetFenceStatus>("vkGetFenceStatus"))(device,slot.fence);
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
    if(slot.pending||slot.decoding)return;
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
    if(slot.pending||slot.ready||slot.recorded||slot.decoding)throw std::runtime_error("Async slot is still leased");
    slot.decoded.reset();slot.decode_finished=false;slot.decode_error={};
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
        if(impl->decode_worker_requested&&!impl->decode_worker_attempted){
            impl->decode_worker_attempted=true;
            impl->async_fence_status=impl->proc<PFN_vkGetFenceStatus>("vkGetFenceStatus");
            auto worker=std::make_unique<NativeDecodeWorker>();
            if(worker->ready()&&impl->async_fence_status){impl->decode_worker=std::move(worker);log_event("probe.async.decode-worker enabled=1 slots=3 start=submit fence-query=worker queue-calls=0");}
            else log_event("probe.async.decode-worker unavailable; synchronous decode retained");
        }
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
                for(const auto& read:plan.reads)if(read.target.role=="instances")native_preview_trace_event(bits(read.target.binding.buffer),NativePreviewTraceStage::Submit);
                for(const auto& view:plan.additional)for(const auto& read:view.reads)if(read.target.role=="instances")native_preview_trace_event(bits(read.target.binding.buffer),NativePreviewTraceStage::Submit);
                planned=true;
                // Shared culling/indirect scratch may be overwritten by the next
                // primary, even in this batch. Copy directly after its producer.
                // The original batch's signals still cover every borrowed read.
                Impl::AsyncSlot* available=nullptr;
                for(auto& slot:impl->async_slots)if(!slot.pending&&!slot.ready&&!slot.recorded&&!slot.decoding&&
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
        // Inline plans may have been recorded well before this submission.
        // Validate their descriptor and source leases at the copy boundary,
        // before the application can begin using the recorded commands.
        {std::lock_guard<std::mutex> lock(impl->mutex);
            const auto live=[&](const auto& self,const CapturePlan& plan)->bool {
                for(const auto& version:plan.descriptor_leases){const auto set=impl->sets.find(version.first);if(set==impl->sets.end()||set->second.version!=version.second)return false;}
                for(const auto& generation:plan.generations){const auto buffer=impl->buffers.find(generation.first);const auto memory=plan.memories.find(generation.first);
                    if(buffer==impl->buffers.end()||memory==plan.memories.end()||buffer->second.generation!=generation.second||buffer->second.memory!=memory->second)return false;}
                for(const auto& view:plan.additional)if(!self(self,view))return false;
                return true;
            };
            for(auto* slot:selected)slot->publishable=live(live,slot->plan);
        }
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
bool GpuProbe::Impl::enqueue_decode(AsyncSlot& slot,bool early)noexcept {
    if(!decode_worker||slot.decoding||slot.decode_finished||!slot.publishable)return false;
    slot.decode_owner=this;slot.decoding=true;
    NativeDecodeWorker::Task task{[](void* raw)noexcept{auto& s=*static_cast<AsyncSlot*>(raw);s.decode_owner->decode_slot(s);},&slot};
    if(early)task.ready=[](void* raw)noexcept{auto& s=*static_cast<AsyncSlot*>(raw);return s.decode_owner->decode_slot_ready(s);};
    task.cancel=[](void* raw)noexcept{auto& s=*static_cast<AsyncSlot*>(raw);s.decode_owner->cancel_decode(s);};
    if(decode_worker->submit(task)){++decode_worker_enqueued;if(early)++decode_worker_early;else ++decode_worker_late;return true;}
    slot.decoding=false;return false;
}
bool GpuProbe::Impl::decode_slot_ready(AsyncSlot& slot)noexcept {
    std::lock_guard<std::mutex> lock(readback_mutex);
    if(async_disabled||!slot.publishable||!slot.decoding)return true;
    ++decode_worker_fence_polls;
    const bool completed=complete_slot(slot,false);
    return completed||async_disabled;
}
void GpuProbe::Impl::cancel_decode(AsyncSlot& slot)noexcept {
    std::lock_guard<std::mutex> lock(readback_mutex);
    slot.decoding=false;slot.decode_finished=false;slot.decoded.reset();slot.publishable=false;++decode_worker_cancelled;
    // The established main-thread teardown still drains pending GPU work.
}
void GpuProbe::Impl::decode_slot(AsyncSlot& slot)noexcept {
    {
        std::lock_guard<std::mutex> lock(readback_mutex);
        if(async_disabled||!slot.publishable||slot.pending||!slot.ready){
            slot.decoding=false;slot.decode_finished=true;slot.decoded.reset();
            std::memcpy(slot.decode_error.data(),"Worker source unavailable",26);++decode_worker_cancelled;return;
        }
    }
    std::unique_ptr<GpuSnapshot> decoded;std::array<char,160> error{};
    try{decoded=std::make_unique<GpuSnapshot>(decode_plan(slot.plan,slot.mapped,true));}
    catch(const std::exception& value){const auto size=std::min(std::strlen(value.what()),error.size()-1);std::memcpy(error.data(),value.what(),size);}
    catch(...){std::memcpy(error.data(),"Unknown worker decode failure",29);}
    std::lock_guard<std::mutex> lock(readback_mutex);
    slot.decoded=std::move(decoded);slot.decode_error=error;
    slot.decode_finished=true;slot.decoding=false;++decode_worker_completed;
}
void GpuProbe::Impl::validate_async_result_locked(const CapturePlan& plan,GpuSnapshot& snapshot){
    for(const auto& generation:plan.generations){
        const auto found=buffers.find(generation.first);
        if(found==buffers.end()||found->second.generation!=generation.second||found->second.memory!=plan.memories.at(generation.first))
            throw std::runtime_error("Async worker source retired or rebound");
    }
    bool anchor_valid=true;for(const auto& anchor:plan.anchor_leases)if(!anchor_live(anchor)){anchor_valid=false;break;}
    if(!anchor_valid){snapshot.buffers.erase("player_anchor");snapshot.player_anchor_dispatches.clear();}
    if(plan.additional.size()!=snapshot.additional_views.size())throw std::runtime_error("Async worker view count changed");
    for(std::size_t n=0;n<plan.additional.size();++n)validate_async_result_locked(plan.additional[n],snapshot.additional_views[n]);
}
void GpuProbe::Impl::validate_async_result(const CapturePlan& plan,GpuSnapshot& snapshot){
    std::lock_guard<std::mutex> lock(mutex);validate_async_result_locked(plan,snapshot);
}
bool GpuProbe::Impl::plan_reads_instances(const CapturePlan& plan,const VkDescriptorBufferInfo& source)noexcept{
    for(const auto& read:plan.reads){const auto& b=read.target.binding;
        if(read.target.role=="instances"&&b.buffer==source.buffer&&b.offset==source.offset&&b.range==source.range)return true;}
    for(const auto& child:plan.additional)if(plan_reads_instances(child,source))return true;
    return false;
}
void GpuProbe::Impl::prune_decode_deliveries()noexcept{
    for(auto& previous:decode_deliveries)if(previous.sequence){
        bool outstanding=false;
        for(const auto& slot:async_slots)if((slot.recorded||slot.pending||slot.ready||slot.decoding)&&plan_reads_instances(slot.plan,previous.instances)){outstanding=true;break;}
        if(!outstanding)previous={};
    }
}
bool GpuProbe::Impl::accept_decode_delivery(const GpuSnapshot& snapshot)noexcept{
    const auto found=snapshot.buffers.find("instances");if(found==snapshot.buffers.end()||!snapshot.sequence)return false;
    const auto& source=found->second.binding;DecodeDelivery* empty=nullptr;
    for(auto& previous:decode_deliveries){
        if(!previous.sequence){if(!empty)empty=&previous;continue;}
        const auto& b=previous.instances;
        if(b.buffer!=source.buffer||b.offset!=source.offset||b.range!=source.range)continue;
        if(snapshot.sequence<=previous.sequence){++decode_worker_discarded_old;return false;}
        previous.sequence=snapshot.sequence;return true;
    }
    if(!empty)return false; // Only this view declines if the bounded proof fails.
    *empty={source,snapshot.sequence};return true;
}
GpuSnapshot GpuProbe::poll_async(VkQueue queue) noexcept {
    GpuSnapshot latest;
    try {
        std::lock_guard<std::mutex> lock(impl->readback_mutex);
        if(impl->async_disabled||impl->async_queue!=queue)return latest;
        if(impl->decode_worker)impl->prune_decode_deliveries();
        std::vector<GpuSnapshot> views;
        const auto retain=[&](GpuSnapshot view){
            if(impl->decode_worker&&!impl->accept_decode_delivery(view))return;
            const auto binding=view.buffers.at("instances").binding;
            const auto found=std::find_if(views.begin(),views.end(),[&](const auto& previous){
                const auto source=previous.buffers.at("instances").binding;
                return source.buffer==binding.buffer&&source.offset==binding.offset&&source.range==binding.range;
            });
            if(found==views.end())views.push_back(std::move(view));else if(found->sequence<view.sequence)*found=std::move(view);
        };
        bool rejected=false;
        const auto note_rejected=[&](const auto& slot,const char* reason){
            const auto now=GetTickCount64();const auto event=impl->async_gaps.failure(now,1,reason);
            if(event.emit)log_event_lazy([&]{return "avatar-fallback.capture-gap episode="+std::to_string(event.episode)+
                " failures="+std::to_string(event.failures)+" gap-ms="+std::to_string(event.elapsed_ms)+
                " reason="+reason+" age-ms="+std::to_string(now>=slot.started_ms?now-slot.started_ms:UINT64_MAX)+
                " epoch="+std::to_string(slot.epoch)+" sequence="+std::to_string(slot.sequence)+
                " publishable="+std::to_string(slot.publishable)+" rejected-total="+std::to_string(impl->async_rejected)+fallback_event_summary(event);});
        };
        for(auto& slot:impl->async_slots) {
            if(slot.decoding)continue;
            if(slot.recorded&&!slot.pending)continue;
            const auto completed=impl->complete_slot(slot,false);
            // A lost/uncertain device invalidates the entire pass, including
            // bytes decoded before this error. Never touch another mapped slot.
            if(impl->async_disabled)return {};
            if(!completed){++impl->async_pending_polls;continue;}
            if(!slot.ready)continue;
            if(!slot.publishable||slot.queue!=queue){slot.ready=false;if(!slot.recorded)slot.plan={};slot.decoded.reset();slot.decode_finished=false;++impl->async_rejected;rejected=true;note_rejected(slot,"unpublishable-or-queue-changed");continue;}
            try {
                const auto now=GetTickCount64();
                if(!native_pose_is_fresh(slot.started_ms,now))throw std::runtime_error("Async pose expired");
                GpuSnapshot captured;
                if(impl->decode_worker){
                    if(!slot.decode_finished){
                        if(impl->enqueue_decode(slot,false))continue;
                        captured=impl->decode_plan(slot.plan,slot.mapped,true);
                    }else{
                        if(!slot.decoded)throw std::runtime_error(slot.decode_error[0]?slot.decode_error.data():"Worker decode failed");
                        captured=std::move(*slot.decoded);slot.decoded.reset();
                        // Completion of CPU work never extends native source
                        // lifetime or grants authority to its older observation.
                        impl->validate_async_result(slot.plan,captured);
                    }
                }else captured=impl->decode_plan(slot.plan,slot.mapped,true);
                captured.source_epoch=slot.epoch;captured.captured_milliseconds=slot.started_ms;
                captured.sequence=slot.sequence;captured.asynchronous=true;
                for(auto& view:captured.additional_views){view.source_epoch=slot.epoch;view.captured_milliseconds=slot.started_ms;
                    view.sequence=slot.sequence;view.asynchronous=true;}
                auto children=std::move(captured.additional_views);captured.additional_views.clear();
                retain(std::move(captured));for(auto& view:children)retain(std::move(view));
                ++impl->async_decoded;
                const auto event=impl->async_gaps.recovery(now);
                if(event.emit)log_event_lazy([&]{return "avatar-fallback.capture-recovered episode="+std::to_string(event.episode)+
                    " failures="+std::to_string(event.failures)+" gap-ms="+std::to_string(event.elapsed_ms)+" sequence="+std::to_string(slot.sequence)+fallback_event_summary(event);});
            }catch(const std::exception& error){++impl->async_rejected;rejected=true;note_rejected(slot,error.what());}
            catch(...){++impl->async_rejected;rejected=true;note_rejected(slot,"unknown-capture-error");}
            slot.ready=false;slot.plan={};slot.decoded.reset();slot.decode_finished=false;
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
        if(latest.sequence&&impl->async_decoded%300==0)log_event("probe.async.stats enqueued="+std::to_string(impl->async_enqueued)+" decoded="+std::to_string(impl->async_decoded)+" pending-polls="+std::to_string(impl->async_pending_polls)+" ring-full="+std::to_string(impl->async_skipped)+" rejected="+std::to_string(impl->async_rejected)+" normal-host-waits=0 worker-enqueued="+std::to_string(impl->decode_worker_enqueued)+" worker-completed="+std::to_string(impl->decode_worker_completed)+" worker-discarded-old="+std::to_string(impl->decode_worker_discarded_old)+" worker-early="+std::to_string(impl->decode_worker_early)+" worker-late="+std::to_string(impl->decode_worker_late)+" worker-fence-polls="+std::to_string(impl->decode_worker_fence_polls)+" worker-cancelled="+std::to_string(impl->decode_worker_cancelled));
    }catch(...){}
    return latest;
}
