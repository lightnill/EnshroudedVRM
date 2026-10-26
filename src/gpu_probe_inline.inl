// Included inside evrm. Every inline copy is inside an attested native primary.
void GpuProbe::Impl::cancel_recorded(VkCommandBuffer command){
    for(auto& slot:async_slots)if(slot.recorded&&slot.original==command){
        slot.recorded=false;slot.publishable=false;
        if(!slot.pending){slot.ready=false;slot.plan={};}
    }
}
void GpuProbe::commands_allocated(const VkCommandBufferAllocateInfo& info,const VkCommandBuffer* handles){
    if(!enabled()||!handles||info.commandBufferCount>512)return;
    std::lock_guard<std::mutex> copies(impl->readback_mutex);std::lock_guard<std::mutex> lock(impl->mutex);
    for(uint32_t i=0;i<info.commandBufferCount;++i){impl->cancel_recorded(handles[i]);
        const auto previous=impl->command_pools.find(handles[i]);
        if(previous!=impl->command_pools.end())previous->second=info.commandPool;
        else if(impl->command_pools.size()<512)impl->command_pools.emplace(handles[i],info.commandPool);
    }
}
void GpuProbe::command_pool_reset(VkCommandPool pool,bool destroyed){
    if(!enabled())return;
    std::lock_guard<std::mutex> copies(impl->readback_mutex);std::lock_guard<std::mutex> lock(impl->mutex);
    for(auto item=impl->command_pools.begin();item!=impl->command_pools.end();){
        if(item->second!=pool){++item;continue;}
        impl->cancel_recorded(item->first);impl->commands.erase(item->first);
        if(destroyed)item=impl->command_pools.erase(item);else ++item;
    }
}
bool GpuProbe::capture_inline(VkCommandBuffer command,uint32_t family)noexcept{
    if(!enabled()||player_anchor_capture_enabled())return false;
    try{
        std::lock_guard<std::mutex> copying(impl->readback_mutex);
        {std::lock_guard<std::mutex> lock(impl->mutex);
            const auto found=impl->commands.find(command);if(found==impl->commands.end())return false;
            found->second.inline_required=true;
            if(!impl->command_pools.count(command))return false;
        }
        if(impl->async_disabled||!impl->async_queue)return false;
        std::vector<SubmittedDraw> draws;uint64_t generation=0;
        {std::lock_guard<std::mutex> lock(impl->mutex);
            const auto queue=impl->queues.find(impl->async_queue);const auto found=impl->commands.find(command);
            if(queue==impl->queues.end()||queue->second!=family||found==impl->commands.end())return false;
            auto& state=found->second;generation=state.generation;
            impl->resolve_command(impl->async_queue,command,draws,state.inline_boundary,true);state.inline_boundary=state.draws.size();
            for(const auto& draw:draws)for(const auto& binding:draw.draw.bindings){const auto set=impl->sets.find(binding.second.set);
                if(set==impl->sets.end()||!set->second.layout.inline_safe||set->second.layout.inline_unsafe_bindings.count(binding.second.binding))return false;
            }
        }
        if(draws.empty())return false;
        auto plan=impl->make_plan(impl->async_queue,impl->async_limits,draws,true);
        if(plan.reads.empty()||!plan.additional.empty())return false;
        Impl::AsyncSlot* selected=nullptr;
        for(auto& slot:impl->async_slots)if(!slot.pending&&!slot.ready&&!slot.recorded){selected=&slot;break;}
        if(!selected){++impl->async_skipped;return false;}
        impl->prepare_slot(*selected,impl->async_queue,std::move(plan));
        // Retain before the first original-primary command can reference it.
        selected->recorded=true;selected->publishable=true;selected->original=command;selected->original_generation=generation;
        selected->started_ms=GetTickCount64();
        impl->record_copy(command,*selected,selected->plan);return true;
    }catch(const std::exception& error){try{log_event("probe.inline.rejected "+std::string(error.what()));}catch(...){}}
    catch(...){}return false;
}
void GpuProbe::Impl::mark_submitted(VkQueue queue,const std::vector<AsyncSlot*>& selected,uint64_t epoch,bool publishable){
    VkResult marker=VK_SUCCESS;
    for(auto* slot:selected){
        // Only a failed original submit can retain a recording with an older
        // pending marker. Its retry needs a fresh completion, even if polling
        // has already consumed the earlier rejection.
        if(slot->pending)drain_slot(*slot);
        slot->ready=false;
        marker=proc<PFN_vkResetFences>("vkResetFences")(device,1,&slot->fence);
        if(marker!=VK_SUCCESS)break;
    }
    if(marker==VK_SUCCESS){
        for(auto* slot:selected){slot->queue=queue;slot->epoch=epoch;slot->sequence=++async_sequence;
            slot->recorded=false;slot->pending=true;slot->publishable=publishable&&!slot->plan.reads.empty();}
        for(auto* slot:selected){marker=proc<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,0,nullptr,slot->fence);if(marker!=VK_SUCCESS)break;}
    }
    if(marker!=VK_SUCCESS){
        async_disabled=true;
        auto completion=proc<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(queue);
        while(completion!=VK_SUCCESS&&completion!=VK_ERROR_DEVICE_LOST){Sleep(1);completion=proc<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(queue);}
        for(auto* slot:selected){slot->pending=false;slot->ready=false;slot->plan={};}
        log_event("probe.async.marker-failed result="+std::to_string(marker));
    }else{async_enqueued+=selected.size();if(publishable&&queue==async_queue)async_frame=true;}
}
void GpuProbe::inline_submitted2(VkQueue queue,uint32_t count,const VkSubmitInfo2* submits,VkResult result,uint64_t epoch)noexcept{
    if(!enabled())return;
    try{
        std::lock_guard<std::mutex> copying(impl->readback_mutex);std::vector<Impl::AsyncSlot*> selected;
        if(!submits)return;
        std::lock_guard<std::mutex> lock(impl->mutex);
        for(uint32_t i=0;i<count;++i)for(uint32_t c=0;c<submits[i].commandBufferInfoCount;++c)
            for(auto& slot:impl->async_slots){const auto state=impl->commands.find(submits[i].pCommandBufferInfos[c].commandBuffer);
                if(slot.recorded&&slot.original==submits[i].pCommandBufferInfos[c].commandBuffer&&state!=impl->commands.end()&&
                    slot.original_generation==state->second.generation&&std::find(selected.begin(),selected.end(),&slot)==selected.end())selected.push_back(&slot);}
        if(selected.empty())return;
        // Submit2 retains copies until completion but never grants replacement.
        impl->mark_submitted(queue,selected,epoch,false);
        if(result!=VK_SUCCESS)for(auto* slot:selected)slot->recorded=true;
    }catch(...){impl->async_disabled=true;}
}
