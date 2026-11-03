std::vector<NativePlayerPoseOwner> NativeBackend::player_pose_owners(std::uint64_t epoch,const std::vector<AvatarSubjectSelection>& subjects)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);
        if(!impl->replacement||!impl->replacement_enabled||impl->disabled||impl->context.uncertain||!epoch||impl->metadata.buffer_epoch()!=epoch||subjects.size()>16){impl->player_observation_valid=false;impl->player_observations.clear();return {};}
        std::vector<NativePlayerIdentity> identities;std::vector<std::string> names;
        for(const auto& s:subjects){
            const bool candidate=s.candidate&&s.candidate->model&&s.candidate->subject==s.subject&&s.candidate->generation==s.desired_generation;
            if(s.replacement_enabled&&(s.model||candidate)&&s.identity.account&&s.identity.entity_id){identities.push_back(s.identity);names.push_back(s.subject);}
        }
        if(identities.empty()){
            impl->player_observation_valid=false;impl->player_observations.clear();
            for(const auto& s:subjects)if(s.subject.empty()&&s.replacement_enabled){std::string reason;const auto local=impl->sample_pose_owner(epoch,reason);std::vector<NativePlayerPoseOwner> result;if(local)result.push_back(NativePlayerPoseOwner{std::string{},local});impl->observe_player_owners(false,5,1,true,result.size(),bool(local),GetTickCount64());return result;}
            return {};
        }
        const auto now=GetTickCount64();
        bool reuse=impl->player_observation_valid&&impl->player_observation_epoch==epoch&&
            impl->player_observation_source==impl->player_owner_source()&&impl->player_requested.size()==identities.size()&&
            impl->player_requested_subjects==names&&impl->player_availability.size()==identities.size()&&
            now>=impl->player_observed_at&&now-impl->player_observed_at<250;
        const bool local_requested=std::find(names.begin(),names.end(),std::string{})!=names.end();
        std::size_t ready_count=0;
        for(std::size_t i=0;reuse&&i<identities.size();++i){reuse=Impl::same_player(impl->player_requested[i],identities[i]);
            if(impl->player_availability[i]!=NativePlayerOwnerStatus::Ready)continue;
            if(ready_count>=impl->player_observations.size()){reuse=false;break;}
            const auto& old=impl->player_observations[ready_count++];
            reuse=reuse&&old.subject==names[i]&&old.owner&&old.owner->source_epoch==epoch&&
                old.owner->candidate.account==identities[i].account&&old.owner->candidate.entity_id==identities[i].entity_id&&
                old.owner->candidate.connection==identities[i].connection&&impl->hint_source_current(*old.owner,old.owner->source);
        }
        reuse=reuse&&ready_count==impl->player_observations.size();
        if(reuse){const bool local_selected=std::any_of(impl->player_observations.begin(),impl->player_observations.end(),[](const auto& p){return p.subject.empty();});
            impl->observe_player_owners(true,0,identities.size(),local_requested,impl->player_observations.size(),local_selected,now);return impl->player_observations;}
        impl->player_observations.clear();impl->player_observation_valid=false;impl->player_observed_at=now;
        std::string reason;NativeOwnerObservationStats stats;
        auto batch=impl->capture_available_player_owners(identities,reason,40,&stats);
        if(!batch.valid){impl->observe_player_owners(false,1,identities.size(),local_requested,0,false,now,&stats,reason.c_str());return {};}
        if(batch.players.size()!=identities.size()||batch.proof.empty()){impl->observe_player_owners(false,2,identities.size(),local_requested,0,false,now,&stats);return {};}
        std::vector<NativePlayerPoseOwner> result;std::vector<NativePlayerOwnerStatus> availability;
        for(std::size_t i=0;i<batch.players.size();++i){auto& row=batch.players[i];
            if(!Impl::same_player(row.identity,identities[i]))return {};
            availability.push_back(row.status);
            if(row.status==NativePlayerOwnerStatus::EntityAbsent||row.status==NativePlayerOwnerStatus::AssociationUnavailable){
                if(!row.candidate.guards.empty()||!row.candidate.models.empty()||row.candidate.entity)return {};
                continue;
            }
            if(row.status!=NativePlayerOwnerStatus::Ready||&row.candidate.guards.view()!=&batch.proof.view())return {};
            auto owner=std::make_shared<NativePoseOwner>();owner->candidate=std::move(row.candidate);owner->source_epoch=epoch;
            owner->source=impl->metadata.buffer(reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(owner->candidate.native_buffer)));
            if(!native_pose_owner_valid(*owner)||!impl->hint_source_current(*owner,owner->source)||owner->candidate.account!=identities[i].account||owner->candidate.entity_id!=identities[i].entity_id||owner->candidate.connection!=identities[i].connection){impl->observe_player_owners(false,3,identities.size(),local_requested,0,false,now,&stats);return {};}
            result.push_back({names[i],std::move(owner)});
        }
        if(impl->metadata.buffer_epoch()!=epoch){impl->observe_player_owners(false,4,identities.size(),local_requested,0,false,now,&stats);return {};}
        impl->player_observations=result;impl->player_requested=identities;impl->player_requested_subjects=names;
        impl->player_availability=std::move(availability);impl->player_observation_epoch=epoch;
        impl->player_observation_source=impl->player_owner_source();impl->player_observation_valid=true;
        const bool local_selected=std::any_of(result.begin(),result.end(),[](const auto& p){return p.subject.empty();});
        impl->observe_player_owners(true,0,identities.size(),local_requested,result.size(),local_selected,now);return result;
    }catch(...){return {};}
}
bool NativeBackend::player_model_ready(const AvatarCandidate& candidate)const noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->player_models.find(candidate.subject);
        return impl->replacement_enabled&&!impl->disabled&&!impl->context.uncertain&&candidate.model&&found!=impl->player_models.end()&&
            found->second.generation==candidate.generation&&found->second.assets&&found->second.assets->avatar()==candidate.model->asset&&found->second.skin==candidate.model->skin;
    }catch(...){return false;}
}
NativeAvatarUploadState NativeBackend::prepare_player_model(VkQueue queue,std::uint32_t family,const AvatarCandidatePtr& candidate)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);
        if(!candidate)return NativeAvatarUploadState::Idle;
        if(!candidate->model||!candidate->model->asset||!candidate->generation||impl->disabled||impl->context.uncertain||!impl->replacement_enabled)return NativeAvatarUploadState::Failed;
        if(impl->queue&&(impl->queue!=queue||impl->family!=family))return NativeAvatarUploadState::Failed;
        impl->players_managed=true;
        const auto* selection=impl->player_selection(candidate->subject);
        if(!selection||!selection->replacement_enabled||
            !((selection->candidate&&selection->candidate==candidate)||(selection->selection&&selection->selection==candidate)))return NativeAvatarUploadState::Idle;
        if(!impl->player_models.count(candidate->subject)&&impl->player_models.size()>=16)return NativeAvatarUploadState::Failed;
        auto& target=impl->player_models[candidate->subject]; // Allocate before the atomic activation callback.
        if(target.staged&&target.staged->generation==candidate->generation&&target.staged->model==candidate->model&&target.ready)return NativeAvatarUploadState::Ready;
        target.staged.reset();target.ready.reset();
        const auto staged=[&](std::shared_ptr<NativeAvatarGpuAssets> ready){target.staged=candidate;target.ready=std::move(ready);return NativeAvatarUploadState::Ready;};
        for(const auto& entry:impl->player_models){
            if(entry.second.assets&&entry.second.assets->avatar()==candidate->model->asset&&entry.second.skin==candidate->model->skin)return staged(entry.second.assets);
            if(entry.second.staged&&entry.second.ready&&entry.second.staged->model->asset==candidate->model->asset&&entry.second.staged->model->skin==candidate->model->skin)return staged(entry.second.ready);
        }
        if(impl->assets&&impl->assets->avatar()==candidate->model->asset&&!impl->assets->skin()&&!candidate->model->skin)return staged(impl->assets);
        if(impl->player_upload&&(impl->player_upload->subject!=candidate->subject||impl->player_upload->generation!=candidate->generation||impl->player_upload->model!=candidate->model))return NativeAvatarUploadState::Pending;
        impl->queue=queue;impl->family=family;impl->initialize();impl->player_upload=candidate;
        impl->ensure_asset_stager();const auto state=impl->asset_stager->update(queue,family,candidate->generation,candidate->model->asset,candidate->model->skin);
        if(state==NativeAvatarUploadState::Ready){auto ready=impl->asset_stager->take_ready(candidate->generation,candidate->model->asset);impl->player_upload.reset();if(!ready)return NativeAvatarUploadState::Failed;return staged(std::move(ready));}
        if(state==NativeAvatarUploadState::Failed)impl->player_upload.reset();
        return state;
    }catch(...){return NativeAvatarUploadState::Failed;}
}
bool NativeBackend::activate_player_model(const AvatarCandidate& candidate,bool(*commit)(void*,std::uint64_t)noexcept,void* context)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);
        const auto found=impl->player_models.find(candidate.subject);
        if(!impl->replacement_enabled||impl->disabled||impl->context.uncertain||!candidate.model||found==impl->player_models.end()||!commit)return false;
        auto& target=found->second;
        if(!target.staged||!target.ready||target.staged->subject!=candidate.subject||target.staged->generation!=candidate.generation||target.staged->model!=candidate.model||target.ready->avatar()!=candidate.model->asset)return false;
        if(!commit(context,impl->metadata.buffer_epoch()))return false;
        target.assets=std::move(target.ready);target.skin=candidate.model->skin;target.generation=candidate.generation;target.staged.reset();
        impl->tool_slots.disable_subject(candidate.subject);
        // Existing publications fail assignment/asset checks; other subjects
        // and recorded resource leases remain independently usable.
        if(candidate.subject.empty()){impl->assets.reset();impl->discard_owner_hint();}
        return true;
    }catch(...){return false;}
}
void NativeBackend::retain_player_models(const std::vector<AvatarSubjectSelection>& subjects)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);
        if(subjects.size()>16)return;
        impl->players_managed=true;std::vector<std::string> retained;
        for(const auto& s:subjects)if(s.replacement_enabled&&s.model){retained.push_back(s.subject);
            const auto* old=impl->player_selection(s.subject);if(old&&!Impl::same_player(old->identity,s.identity))impl->tool_slots.disable_subject(s.subject);}
        impl->tool_slots.retain_subjects(retained);impl->player_selections=subjects;
        if(impl->player_upload){const auto* s=impl->player_selection(impl->player_upload->subject);
            const bool current=s&&s->replacement_enabled&&((s->candidate&&s->candidate->generation==impl->player_upload->generation&&s->candidate->model==impl->player_upload->model)||
                (s->selection&&s->selection->generation==impl->player_upload->generation&&s->selection->model==impl->player_upload->model));
            if(!current){if(impl->asset_stager)impl->asset_stager->cancel();impl->player_upload.reset();}
        }
        for(auto it=impl->player_models.begin();it!=impl->player_models.end();){const auto* s=impl->player_selection(it->first);
            if(!s||!s->replacement_enabled||(!s->model&&!s->candidate)){impl->tool_slots.disable_subject(it->first);it=impl->player_models.erase(it);}
            else{auto& target=it->second;const auto current=s->candidate?s->candidate:s->selection;
                if(target.staged&&(!current||current->generation!=target.staged->generation||current->model!=target.staged->model)){target.staged.reset();target.ready.reset();}
                ++it;
            }
        }
        // Poll cancellation without introducing a new upload. A pending job
        // remains owned by the stager until its normal fence retires.
        if(!impl->player_upload&&impl->asset_stager&&!impl->context.uncertain&&impl->queue)impl->asset_stager->update(impl->queue,impl->family,0,{});
        const auto previous=std::atomic_load(&impl->submitted_histories);
        if(previous){auto bank=std::make_shared<Impl::HistoryBank>(*previous);
            for(auto it=bank->begin();it!=bank->end();)if(!it->second||!it->second->pose||!impl->player_current(*it->second->pose))it=bank->erase(it);else ++it;
            std::atomic_store(&impl->submitted_histories,std::shared_ptr<const Impl::HistoryBank>(std::move(bank)));}
        if(const auto local=impl->player_selection({});!local||!local->replacement_enabled)impl->assets.reset();
    }catch(...){try{std::lock_guard<std::mutex> lock(impl->mutex);impl->player_selections.clear();impl->tool_slots.disable();}catch(...){}}
}
