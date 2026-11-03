// Included inside the one NativeBackend::Impl. Subject state never owns a
// second metadata tracker, hook coordinator, queue or native readback.
struct PlayerModel {
    std::shared_ptr<NativeAvatarGpuAssets> assets;
    std::shared_ptr<const NativeSkinAsset> skin;
    std::uint64_t generation=0;
    // Completed uploads wait for this subject's visible pose without occupying
    // the sole in-flight transfer slot. At most one ready choice per subject.
    AvatarCandidatePtr staged;
    std::shared_ptr<NativeAvatarGpuAssets> ready;
};
std::atomic<bool> players_managed{false};
std::map<std::string,PlayerModel> player_models;
std::vector<AvatarSubjectSelection> player_selections;
AvatarCandidatePtr player_upload;
std::unique_ptr<NativePlayerCommit> player_commit;
std::vector<NativePlayerPoseOwner> player_observations;
std::uint64_t player_observed_at=0,owner_work_started=0;
std::vector<NativePlayerIdentity> player_requested;
std::vector<std::string> player_requested_subjects;
std::vector<NativePlayerOwnerStatus> player_availability;
std::uint64_t player_observation_epoch=0;
const void* player_observation_source=nullptr;
bool player_observation_valid=false;
const std::vector<NativeOwnerCandidate>* verified_players=nullptr;
bool verifying_player_group=false,player_group_verified=false;
static bool same_player(const NativePlayerIdentity& a,const NativePlayerIdentity& b)noexcept {
    return a.account==b.account&&a.entity_id==b.entity_id&&a.connection==b.connection&&a.local==b.local;
}
const AvatarSubjectSelection* player_selection(const std::string& subject)const {
    for(const auto& s:player_selections)if(s.subject==subject)return &s;
    return nullptr;
}
std::shared_ptr<NativeAvatarGpuAssets> player_assets(const NativeAvatarPublication& p)const {
    if(!players_managed)return assets&&assets->avatar()==p.asset?assets:nullptr;
    const auto found=player_models.find(p.subject);
    return found!=player_models.end()&&found->second.assets&&found->second.assets->avatar()==p.asset&&(!p.skinning||p.skinning->asset==found->second.skin)?found->second.assets:nullptr;
}
bool player_current(const NativeAvatarPublication& p)const {
    if(!players_managed)return p.subject.empty()&&!p.player_identity.account&&!p.assignment_generation;
    const auto snapshot=avatar_runtime_subjects();
    if(!snapshot&&(p.player_identity.account||!p.subject.empty()||p.assignment_generation))return false;
    const AvatarSubjectSelection* s=nullptr;
    if(snapshot){if(snapshot->size()>16)return false;for(const auto& value:*snapshot)if(value.subject==p.subject){if(s)return false;s=&value;}}
    else s=player_selection(p.subject);
    const auto found=player_models.find(p.subject);
    return s&&s->replacement_enabled&&s->model&&s->model->asset==p.asset&&
        found!=player_models.end()&&found->second.assets&&found->second.assets->avatar()==p.asset&&
        found->second.generation==p.assignment_generation&&s->selection&&s->selection->generation==p.assignment_generation&&
        same_player(s->identity,p.player_identity);
}
std::shared_ptr<const NativeAvatarPublication> current_player_publication(const Bundle& b)const {
    return b.pose?native_publication_subject(publication_for(b.instances.info.buffer),b.pose->subject):nullptr;
}
template<class Visit>static void each_bundle(const std::shared_ptr<Bundle>& root,Visit&& visit) {
    if(!root)return;
    visit(*root);for(const auto& b:root->subjects)visit(*b);
}
// Called only after the existing original-CB retirement/idle completion proof.
// These are counts from owned outputs, never ownership or permit authority.
// The prepare shader clears screen.xy and writes positive inverse extents only
// after all checks succeed; the common commit changes only control[0].
std::string player_preparation_diagnostic(const std::shared_ptr<Bundle>& root)const {
    if(!root||root->subjects.empty()||root->subjects.size()>15)return {};
    unsigned local_prepared=0,remote_prepared=0,local_unprepared=0,remote_unprepared=0;
    unsigned unavailable=0,oit_near_plane=0,oit_other=0,before_oit=0;
    each_bundle(root,[&](const Bundle& b){
        if(!b.submitted||b.awaiting_submit_result||b.failed_submit||b.submitted_queue!=root->submitted_queue||
           b.data.permit[0]!=1||!b.pose||!b.frame||!b.frame->mapped||b.frame->size<sizeof(NativePreparedFrame)){
            ++unavailable;return;
        }
        float screen[2]{};
        std::memcpy(screen,static_cast<const unsigned char*>(b.frame->mapped)+offsetof(NativePreparedFrame,screen),sizeof screen);
        const bool local=b.pose->player_identity.local||b.pose->subject.empty();
        if(std::isfinite(screen[0])&&std::isfinite(screen[1])&&screen[0]>0&&screen[1]>0){
            ++(local?local_prepared:remote_prepared);return;
        }
        ++(local?local_unprepared:remote_unprepared);
        NativeOitPrepareDiagnostic diagnostic;
        if(b.material_plan.mixed&&b.frame->size>=native_oit_diagnostic_offset+sizeof diagnostic)
            std::memcpy(&diagnostic,static_cast<const unsigned char*>(b.frame->mapped)+native_oit_diagnostic_offset,sizeof diagnostic);
        const bool numeric=diagnostic.magic==native_oit_diagnostic_magic&&diagnostic.checkpoint>=1&&diagnostic.checkpoint<=11&&
            !(diagnostic.valid_mask&~15u)&&!diagnostic.reserved&&
            std::all_of(std::begin(diagnostic.spare),std::end(diagnostic.spare),[](uint32_t value){return value==0;});
        if(!numeric){++before_oit;return;}
        float near_w=0;std::memcpy(&near_w,diagnostic.w,sizeof near_w);
        if(diagnostic.checkpoint==4&&(diagnostic.valid_mask&2u)&&std::isfinite(near_w)&&near_w<1.f/1024.f)++oit_near_plane;
        else ++oit_other;
    });
    return "avatar-fallback.player-preparation subjects="+std::to_string(1+root->subjects.size())+
        " local-prepared="+std::to_string(local_prepared)+" remote-prepared="+std::to_string(remote_prepared)+
        " local-unprepared="+std::to_string(local_unprepared)+" remote-unprepared="+std::to_string(remote_unprepared)+
        " unavailable="+std::to_string(unavailable)+" oit-near-plane="+std::to_string(oit_near_plane)+
        " oit-other="+std::to_string(oit_other)+" before-oit="+std::to_string(before_oit);
}
// Recording remains a logical view. Mutable draw bookkeeping is per subject;
// all subjects consume the SAME original first-indirect witness.
template<class Visit>void each_subject(Recording& r,Visit&& visit,bool root_last=false) {
    const auto root=r.bundle;if(!root)return;
    const auto first=r.first_indirect,count=r.first_count;
    const auto run=[&](const std::shared_ptr<Bundle>& b){
        r.bundle=b;r.pass_drawn=b->pass_drawn;r.shadow_view_start=b->shadow_view_start;
        r.first_indirect=first;r.first_count=count;
        visit(r);b->pass_drawn=r.pass_drawn;b->shadow_view_start=r.shadow_view_start;
    };
    try{if(!root_last)run(root);for(const auto& b:root->subjects)run(b);if(root_last)run(root);}catch(...){r.bundle=root;r.tainted=true;throw;}
    r.bundle=root;r.pass_drawn=root->pass_drawn;r.shadow_view_start=root->shadow_view_start;
}
template<class Visit>void each_subject_recording(Visit&& visit) {
    each_recording([&](VkCommandBuffer c,Recording& r){each_subject(r,[&](Recording& s){visit(c,s);});});
}
const void* player_owner_source()const noexcept {
#ifdef EVRM_NATIVE_BACKEND_TESTING
    if(test_owner_source)return test_owner_source.get();
#endif
    return owner_process.get();
}
NativePlayerOwnerBatch capture_available_player_owners(const std::vector<NativePlayerIdentity>& ids,std::string& reason,std::uint64_t budget=40,NativeOwnerObservationStats* stats=nullptr)const {
    if(!budget)return {};
    NativeOwnerLimits limits;limits.max_milliseconds=budget;
#ifdef EVRM_NATIVE_BACKEND_TESTING
    if(test_owner_source)return test_owner_source->capture_available_players(ids,reason,limits,stats);
#endif
    return owner_process?owner_process->capture_available_players(ids,reason,limits,stats):NativePlayerOwnerBatch{};
}
bool verify_player_owners(std::vector<NativeOwnerCandidate>& out,std::string& reason,std::uint64_t budget,NativeOwnerObservationStats* stats=nullptr)const {
    if(!budget)return false;
    NativeOwnerLimits limits;limits.max_milliseconds=budget;
#ifdef EVRM_NATIVE_BACKEND_TESTING
    if(test_owner_source)return test_owner_source->revalidate_players(out,reason,limits,stats);
#endif
    return owner_process&&owner_process->revalidate_players(out,reason,limits,stats);
}
std::uint64_t player_work_remaining()const noexcept {
    const auto now=owner_clock_milliseconds();return now>=owner_work_started&&now-owner_work_started<40?40-(now-owner_work_started):0;
}
void initialize_player_commit() {
    if(player_commit)return;
    std::ifstream input(shader_directory/"native_player_commit.comp.spv",std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Missing player commit shader");
    const auto size=input.tellg();if(size<20||size>1024*1024||size%4)throw std::runtime_error("Player commit shader size");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),size);
    if(!input||words[0]!=0x07230203u)throw std::runtime_error("Player commit shader bytes");
    player_commit=std::make_unique<NativePlayerCommit>(context.device,context.get,words.data(),words.size());
}
bool prepare_player_group(Recording& r,const std::vector<std::shared_ptr<Bundle>>& included) {
    if(included.empty()||included.size()>16||r.bundle)return false;
    NativeBodyMask united;
    for(const auto& b:included){if(!b||!b->body_bits||!b->frame||!b->subjects.empty())return false;
        for(std::size_t i=0;i<united.words.size();++i){if(united.words[i]&b->body_mask.words[i])return false;united.words[i]|=b->body_mask.words[i];}}
    const auto root=included.front();
    if(included.size()>1){
        initialize_player_commit();root->common_gate=make_buffer(context,4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::vector<VkDescriptorBufferInfo> frames;for(const auto& b:included)frames.push_back({b->frame->handle,0,b->frame->size});
        root->common_commit=player_commit->batch(frames,{root->common_gate->handle,0,4});
        if(!root->common_commit)return false;
        root->subjects.assign(included.begin()+1,included.end());
    }
    std::memcpy(root->body_bits->mapped,united.words.data(),sizeof united.words);r.bundle=root;return true;
}
std::shared_ptr<Bundle> make_player_bundle(Recording& r,VkCommandBuffer command) {
    if(!replacement)return make_bundle(r,command);
    const auto graphics=metadata.bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
    const auto source=metadata.buffer_binding(graphics,2,4);
    const auto view=publication_for(source.info.buffer);
    if(!native_publication_subjects_valid(view))return {};
    std::vector<std::shared_ptr<const NativeAvatarPublication>> publications{view};
    publications.insert(publications.end(),view->additional_subjects.begin(),view->additional_subjects.end());
    std::vector<std::shared_ptr<Bundle>> included;
    for(const auto& p:publications){
        const auto model=player_assets(*p);
        if(!model||(players_managed&&!player_current(*p)))continue;
        // An invalid included publication conservatively leaves the whole pass
        // native. Absent/unassigned/None subjects are never added to the union.
        auto b=make_bundle(r,command,p,model);if(!b)return {};
        included.push_back(std::move(b));
    }
    if(!prepare_player_group(r,included))return {};
    return r.bundle;
}
static void share_player_volume(Bundle& target,const Bundle& source) {
    if(!source.snapshot_attempted)return;
    if(target.snapshot_attempted&&target.volume_snapshot!=source.volume_snapshot){target.invalid=true;return;}
    target.volume_snapshot=source.volume_snapshot;target.volume_storage=source.volume_storage;target.volume_allocations=source.volume_allocations;
    target.volume_bound=source.volume_bound;target.volume_restore=source.volume_restore;target.snapshot_attempted=source.snapshot_attempted;
    target.snapshot_recorded=source.snapshot_recorded;target.snapshot_decline=source.snapshot_decline;target.snapshot_extent=source.snapshot_extent;
    target.snapshot_dispatch=source.snapshot_dispatch;target.snapshot_decline_bound=source.snapshot_decline_bound;
    target.snapshot_decline_image=source.snapshot_decline_image;target.snapshot_decline_binding=source.snapshot_decline_binding;
    target.invalid=target.invalid||source.invalid;
}
void common_cpu_permit(const std::shared_ptr<Bundle>& root,bool permit) {
    each_bundle(root,[&](Bundle& b){b.data.permit[0]=permit?1:0;if(!permit)b.data.history[0]=0;
        b.data.config[3]=native_prepare_remote_actor_mode(b.pose.get(),b.cpu_owner,permit,b.menu_preview);
        std::memcpy(b.control->mapped,&b.data,sizeof b.data);});
    if(root->common_gate)*static_cast<std::uint32_t*>(root->common_gate->mapped)=permit?1:0;
}
void submit_player_group(VkCommandBuffer command,Recording& r,bool shape,VkQueue submitted_queue) {
    if(!r.bundle||r.bundle->submitted)return;
    const auto root=r.bundle;
    const bool explicit_players=root->pose&&root->pose->player_identity.account&&!root->menu_preview;
    if(root->subjects.empty()&&!explicit_players){submit_recording(command,r,shape,submitted_queue);return;}
    std::vector<NativeOwnerCandidate> candidates;std::vector<NativePlayerIdentity> identities;
    bool ready=shape;
    each_bundle(root,[&](Bundle& b){ready=ready&&b.pose&&player_current(*b.pose)&&!b.menu_preview&&b.pose->pose_owner;
        candidates.push_back(b.cpu_owner);identities.push_back(b.pose?b.pose->player_identity:NativePlayerIdentity{});});
    const auto proposed=candidates; // Revalidation deliberately clears failed output.
    std::string reason;NativeOwnerObservationStats stats;
    if(ready){NativeSubmitStageScope scope(submit_timing,NativeSubmitStage::OwnerVerify);ready=verify_player_owners(candidates,reason,player_work_remaining(),&stats);}
    // A stale census may be refreshed once for the whole cohort; no actor,
    // assignment, source or pose lifetime can change during this recovery.
    if(!ready&&stats.phase==NativeOwnerObservationPhase::submission_verify&&stats.failure_location==NativeOwnerFailureLocation::compare&&stats.guard_capture_phase==NativeOwnerObservationPhase::census){
        auto batch=capture_available_player_owners(identities,reason,player_work_remaining(),&stats);
        ready=batch.valid&&batch.players.size()==proposed.size()&&!batch.proof.empty();
        std::vector<NativeOwnerCandidate> fresh;
        for(std::size_t i=0;ready&&i<batch.players.size();++i){auto& row=batch.players[i];
            ready=same_player(row.identity,identities[i])&&row.status==NativePlayerOwnerStatus::Ready&&
                &row.candidate.guards.view()==&batch.proof.view()&&same_owner_selection(proposed[i],row.candidate);
            if(ready)fresh.push_back(std::move(row.candidate));
        }
        if(ready)ready=verify_player_owners(fresh,reason,player_work_remaining(),&stats);
        if(ready){candidates=std::move(fresh);player_observations.clear();player_observation_valid=false;}
    }
    ready=ready&&player_work_remaining();
    verifying_player_group=true;player_group_verified=ready;verified_players=&candidates;
    struct Reset {Impl& self;~Reset(){self.verifying_player_group=false;self.player_group_verified=false;self.verified_players=nullptr;}} reset{*this};
    each_subject(r,[&](Recording& subject){auto& b=*subject.bundle;
        // Clipping coverage belongs to the one native view, never a duplicated
        // clip operation. Subject-owned geometry/source checks stay independent.
        b.visibility_clips=root->visibility_clips;b.gbuffer_clips=root->gbuffer_clips;
        submit_recording(command,subject,shape&&ready,submitted_queue);
    });
    each_bundle(root,[&](Bundle& b){ready=ready&&b.data.permit[0]&&GetTickCount64()>=b.pose->published_milliseconds&&GetTickCount64()-b.pose->published_milliseconds<=250;});
    // Optional equipment reads share the SAME elapsed work deadline.
    if(ready&&owner_process){
        struct BoundedMemory final:NativeOwnerMemory {const NativeOwnerMemory& source;std::uint64_t end;
            BoundedMemory(const NativeOwnerMemory& s,std::uint64_t remaining):source(s),end(s.work_milliseconds()+remaining){}
            bool read(std::uint64_t a,void* p,std::size_t n)const noexcept override{return source.work_milliseconds()<end&&source.read(a,p,n)&&source.work_milliseconds()<end;}
            std::uint64_t milliseconds()const noexcept override{return source.milliseconds();}
            std::uint64_t work_milliseconds()const noexcept override{return source.work_milliseconds();}
        } bounded(*owner_process,player_work_remaining());
        each_bundle(root,[&](Bundle& b){tool_slots.publish(bounded,b.cpu_owner,b.pose,b.instances.generation);});
    }
    each_bundle(root,[&](Bundle& b){ready=ready&&player_current(*b.pose)&&player_work_remaining()&&GetTickCount64()>=b.pose->published_milliseconds&&GetTickCount64()-b.pose->published_milliseconds<=250;});
    if(!ready){player_observations.clear();player_observation_valid=false;each_bundle(root,[&](Bundle& b){tool_slots.disable_subject(b.pose->subject);});}
    common_cpu_permit(root,ready);
}
