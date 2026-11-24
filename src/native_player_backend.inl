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
std::uint64_t player_observed_at=0;
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
// after all checks succeed; the common commit reads each individual control.
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
NativePlayerOwnerBatch capture_independent_player_owners(const std::vector<NativePlayerIdentity>& ids,std::string& reason,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats=nullptr)const {
    if(!limits.max_milliseconds||!limits.max_calls||!limits.max_bytes)return {};
#ifdef EVRM_NATIVE_BACKEND_TESTING
    if(test_owner_source)return test_owner_source->capture_independent_players(ids,reason,limits,stats);
#endif
    return owner_process?owner_process->capture_independent_players(ids,reason,limits,stats):NativePlayerOwnerBatch{};
}
bool verify_player_owners(std::vector<NativeOwnerCandidate>& out,std::string& reason,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats=nullptr)const {
    if(!limits.max_milliseconds||!limits.max_calls||!limits.max_bytes)return false;
#ifdef EVRM_NATIVE_BACKEND_TESTING
    if(test_owner_source)return test_owner_source->revalidate_players(out,reason,limits,stats);
#endif
    return owner_process&&owner_process->revalidate_players(out,reason,limits,stats,&submission_read_workspace);
}
struct PlayerOwnerWork {
    const std::uint64_t started=owner_clock_milliseconds();
    std::size_t calls=0,bytes=0;
    bool invalid=false;
    NativeOwnerLimits remaining()const noexcept {
        NativeOwnerLimits limits;const auto now=owner_clock_milliseconds();
        limits.max_milliseconds=!invalid&&now>=started&&now-started<40?40-(now-started):0;
        limits.max_calls=calls<limits.max_calls?limits.max_calls-calls:0;
        limits.max_bytes=bytes<limits.max_bytes?limits.max_bytes-bytes:0;
        return limits;
    }
    bool available()const noexcept {const auto l=remaining();return l.max_milliseconds&&l.max_calls&&l.max_bytes;}
    void charge(const NativeOwnerObservationStats& stats)noexcept {
        const auto l=remaining();
        if(stats.clock_regressed||stats.attempted_calls>l.max_calls||stats.attempted_bytes>l.max_bytes){invalid=true;return;}
        calls+=stats.attempted_calls;bytes+=stats.attempted_bytes;
    }
};
void discard_player_observation(const NativePlayerIdentity& identity) {
    for(std::size_t i=0;i<player_requested.size();++i)if(same_player(player_requested[i],identity)){
        if(i<player_availability.size())player_availability[i]=NativePlayerOwnerStatus::OwnerUnavailable;
        if(i<player_requested_subjects.size()){const auto& subject=player_requested_subjects[i];
            player_observations.erase(std::remove_if(player_observations.begin(),player_observations.end(),[&](const auto& p){return p.subject==subject;}),player_observations.end());}
        return;
    }
}
void retain_player_observation(const NativePlayerIdentity& identity,const NativeOwnerCandidate& candidate) {
    if(!player_observation_valid||player_observation_source!=player_owner_source()||player_observation_epoch!=metadata.buffer_epoch())return;
    for(std::size_t i=0;i<player_requested.size();++i)if(same_player(player_requested[i],identity)){
        if(i>=player_availability.size()||i>=player_requested_subjects.size())return;
        for(auto& p:player_observations)if(p.subject==player_requested_subjects[i]&&p.owner&&same_owner_selection(p.owner->candidate,candidate)){
            if(&p.owner->candidate.guards.view()==&candidate.guards.view())return;
            auto owner=std::make_shared<NativePoseOwner>(*p.owner);owner->candidate=candidate;
            if(native_pose_owner_valid(*owner)&&hint_source_current(*owner,owner->source)){p.owner=std::move(owner);player_availability[i]=NativePlayerOwnerStatus::Ready;}
            return;
        }
        return;
    }
}
struct PlayerSubmission {
    std::vector<std::shared_ptr<Bundle>> bundles;
    std::vector<std::shared_ptr<const NativePlayerExclusionProof>> exclusions;
    bool shared_uncertain=false;
};
PlayerSubmission* player_submission=nullptr;
struct PlayerSubmissionScope {
    Impl& self;PlayerSubmission current;PlayerSubmission* previous;
    explicit PlayerSubmissionScope(Impl& value):self(value),previous(value.player_submission){self.player_submission=&current;}
    ~PlayerSubmissionScope(){self.player_submission=previous;}
};
void decline_player_submission(Bundle& b,bool discard=false) {
    b.data.permit[0]=b.data.history[0]=0;b.data.config[3]=native_prepare_remote_actor_mode(b.pose.get(),b.cpu_owner,false,b.menu_preview);
    revoke_subject_preparation(b.instances,b.first_pass.extent,b.pose.get());
    std::memcpy(b.control->mapped,&b.data,sizeof b.data);
    if(discard&&b.pose)discard_player_observation(b.pose->player_identity);
}
void reconcile_player_exclusions(const std::shared_ptr<const NativePlayerExclusionProof>& proof) {
    if(!player_submission||!proof)return;
    player_submission->exclusions.push_back(proof);
    for(const auto& b:player_submission->bundles)if(b->submitted&&b->data.permit[0]&&b->cpu_owner.exclusions&&
        !native_player_owner_matches_exclusions(b->cpu_owner,*proof))decline_player_submission(*b,true);
}
void finish_player_submission(bool reconcile=true) {
    if(!player_submission)return;
    if(reconcile)for(const auto& b:player_submission->bundles)if(b->submitted&&b->data.permit[0]){
        bool ready=!player_submission->shared_uncertain;
        if(ready&&b->cpu_owner.exclusions)for(const auto& proof:player_submission->exclusions)
            if(!native_player_owner_matches_exclusions(b->cpu_owner,*proof)){ready=false;break;}
        if(!ready)decline_player_submission(*b,player_submission->shared_uncertain);
    }
    // No potentially expensive reconciliation follows this final age sweep.
    for(const auto& b:player_submission->bundles)if(b->submitted&&b->data.permit[0]&&
        (!b->pose||!native_pose_is_fresh(b->pose->published_milliseconds,GetTickCount64())||
         (!b->menu_preview&&b->pose->player_identity.account&&!player_current(*b->pose))))decline_player_submission(*b);
}
// Private opt-in test. Absent in an ordinary installation; it only withdraws
// preparation for ninety seconds after a two-player proof first succeeds.
struct PlayerFallbackTest {bool checked=false,enabled=false;std::uint64_t started=0;std::array<bool,9> reported{};} player_test;
static unsigned player_test_phase(std::uint64_t elapsed)noexcept{return elapsed<90000?unsigned(elapsed/10000):9u;}
static unsigned player_test_decline(unsigned phase,bool local)noexcept{
    return (phase==1&&!local)||(phase==3&&local)?2u:(phase==5&&!local)||(phase==7&&local)?1u:0u;
}
void configure_player_test(const std::filesystem::path& state)noexcept{
    player_test={};player_test.checked=true;
    try{if(state.empty())return;std::ifstream file(state/"multiplayer-fallback-test.txt",std::ios::binary|std::ios::ate);
        if(!file||file.tellg()<1||file.tellg()>64)return;const auto bytes=std::size_t(file.tellg());std::string text(bytes,'\0');file.seekg(0);file.read(text.data(),bytes);
        player_test.enabled=bool(file)&&text=="evrm-independent-fallback-test-v1\n";
        if(player_test.enabled)log_event("multiplayer-fallback.test-enabled duration-ms=90000 owned-buffer-declines-only=1");
    }catch(...){player_test.enabled=false;}
}
void report_player_test(const std::shared_ptr<Bundle>& root)noexcept{
    if(!player_test.enabled||!root||root->subjects.size()!=1||!root->common_bits||!root->common_gate||!root->player_test_marker[0])return;
    const auto phase=unsigned(root->player_test_marker[0]-1);if(phase>=9||player_test.reported[phase])return;
    try{
        std::array<unsigned,2> cpu{},gpu{},hidden{};bool valid=true;unsigned roles=0;NativeBodyMask expected;
        each_bundle(root,[&](const Bundle& b){if(!b.pose||!b.submitted||b.awaiting_submit_result||b.failed_submit||b.submitted_queue!=root->submitted_queue||!b.frame||b.frame->size<224||b.player_test_marker[0]!=root->player_test_marker[0]){valid=false;return;}
            const bool local=b.pose->player_identity.local||b.pose->subject.empty();const unsigned side=local?0u:1u;if(roles&(1u<<side)){valid=false;return;}roles|=1u<<side;
            cpu[side]=b.data.permit[0];std::memcpy(&gpu[side],static_cast<const unsigned char*>(b.frame->mapped)+192,4);
            const auto fault=player_test_decline(phase,local);valid=valid&&cpu[side]==(fault==1?0u:1u)&&gpu[side]==(fault?0u:1u);
            const auto* mask=static_cast<const unsigned*>(root->common_bits->mapped);
            for(unsigned n=0;n<4096;++n){if(gpu[side]==1)expected.words[n]|=b.body_mask.words[n];hidden[side]|=(mask[n]&b.body_mask.words[n])?1u:0u;}
        });
        if(!valid||roles!=3)return;const auto aggregate=static_cast<const unsigned*>(root->common_gate->mapped)[48];
        const bool match=!std::memcmp(expected.words.data(),root->common_bits->mapped,sizeof expected.words)&&aggregate==unsigned(gpu[0]||gpu[1]);
        player_test.reported[phase]=true;
        log_event("multiplayer-fallback.test phase="+std::to_string(phase)+" elapsed-ms="+std::to_string(root->player_test_marker[1])+" cpu-local="+std::to_string(cpu[0])+" gpu-local="+std::to_string(gpu[0])+" cpu-remote="+std::to_string(cpu[1])+" gpu-remote="+std::to_string(gpu[1])+" hidden-local="+std::to_string(hidden[0])+" hidden-remote="+std::to_string(hidden[1])+" aggregate="+std::to_string(aggregate)+" mask-match="+std::to_string(match)+" words=4096");
    }catch(...){}
}
void initialize_player_commit() {
    if(!player_test.checked)configure_player_test(shader_directory);
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
        initialize_player_commit();
        root->common_gate=lease_control_buffer(context,sizeof(NativePreparedFrame),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        root->common_bits=lease_buffer(context,sizeof united.words,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::vector<VkDescriptorBufferInfo> frames,masks;
        for(const auto& b:included){frames.push_back({b->frame->handle,0,b->frame->size});masks.push_back({b->body_bits->handle,0,sizeof b->body_mask.words});}
        root->common_commit=player_commit->batch(frames,masks,{root->common_bits->handle,0,root->common_bits->size},{root->common_gate->handle,0,root->common_gate->size});
        if(!root->common_commit)return false;
        const VkDescriptorBufferInfo shared[]={{root->common_gate->handle,0,sizeof(NativePreparedFrame)},{root->common_bits->handle,0,root->common_bits->size}};
        std::array<VkWriteDescriptorSet,2> writes{};
        for(unsigned n=0;n<2;++n){auto& w=writes[n];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=root->clip_set;w.dstBinding=n;w.descriptorCount=1;w.descriptorType=n?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&shared[n];}
        function<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(context.device,2,writes.data(),0,nullptr);
        root->subjects.assign(included.begin()+1,included.end());
    }
    r.bundle=root;return true;
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
        // Construction may decline an actor only after the common source
        // contract is proven. Shared uncertainty still rejects the entire view.
        bool individual=false;
        try{auto b=make_bundle(r,command,p,model,&individual);
            if(b)included.push_back(std::move(b));else if(!individual)return {};
        }catch(...){if(!individual||context.uncertain||r.tainted||r.ownership_unknown)throw;}
        if(context.uncertain||r.tainted||r.ownership_unknown)return {};
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
    // Snapshot evidence is shared; source actor validity is not.
}
// Only a completed private snapshot may donate shared lighting resources.
// An actor that declined before capture must not prevent the next actor trying.
template<class Capture>void capture_player_volumes(Recording& r,Capture&& capture) {
    std::shared_ptr<Bundle> volume;
    each_subject(r,[&](Recording& subject){
        auto& b=*subject.bundle;
        if(!b.material_plan.mixed||b.invalid)return;
        if(volume)share_player_volume(b,*volume);
        else {
            capture(subject);
            if(!b.invalid&&b.snapshot_recorded&&b.volume_snapshot)volume=subject.bundle;
        }
    });
}
void common_cpu_permit(const std::shared_ptr<Bundle>& root,bool permit) {
    each_bundle(root,[&](Bundle& b){b.data.permit[0]=permit?1:0;if(!permit)b.data.history[0]=0;
        b.data.config[3]=native_prepare_remote_actor_mode(b.pose.get(),b.cpu_owner,permit,b.menu_preview);
        std::memcpy(b.control->mapped,&b.data,sizeof b.data);});
    if(root->common_gate)static_cast<std::uint32_t*>(root->common_gate->mapped)[48]=0;
}
void submit_player_group(VkCommandBuffer command,Recording& r,bool shape,VkQueue submitted_queue) {
    if(!r.bundle||r.bundle->submitted)return;
    const auto root=r.bundle;
    if(player_submission)each_bundle(root,[&](Bundle& b){
        // Keep only this call's unsubmitted controls. Pending work from any
        // earlier call must never be edited without the existing retry proof.
        if(!b.submitted){if(&b==root.get())player_submission->bundles.push_back(root);
            else for(const auto& child:root->subjects)if(child.get()==&b)player_submission->bundles.push_back(child);}
    });
    const bool explicit_players=root->pose&&root->pose->player_identity.account&&!root->menu_preview;
    if(root->subjects.empty()&&!explicit_players){submit_recording(command,r,shape,submitted_queue);return;}
    bool common=shape&&sources_current(root->sources);
    for(const auto& source:root->excluded_transforms)common=common&&metadata.live(source)&&memory.disjoint(root->instances.info.buffer,0,root->instances.size,source.info.buffer,source.info.offset,source.info.range);
    for(const auto& depth:root->common_shadow_depths)common=common&&same_image(depth,metadata.attachment_view(depth.view));
    verifying_player_group=true;player_group_verified=false;verified_players=nullptr;
    struct Reset {Impl& self;~Reset(){self.verifying_player_group=false;self.player_group_verified=false;self.verified_players=nullptr;}} reset{*this};
    const auto test_elapsed=player_test.started?GetTickCount64()-player_test.started:90000;
    const auto test_phase=player_test_phase(test_elapsed);
    bool clock_invalid=false;unsigned ready_owners=0;
    each_subject(r,[&](Recording& subject){auto& b=*subject.bundle;
        // One absolute budget spans this actor's verify, optional refresh and
        // tool reads. No work or failure in another row consumes this allowance.
        PlayerOwnerWork work;std::string reason;NativeOwnerObservationStats stats;
        std::vector<NativeOwnerCandidate> checked;
        const bool current=common&&b.pose&&!b.menu_preview&&b.pose->pose_owner&&player_current(*b.pose);
        bool verified=false;
        if(current){
            checked.push_back(b.cpu_owner);
            {NativeSubmitStageScope scope(submit_timing,NativeSubmitStage::OwnerVerify);verified=verify_player_owners(checked,reason,work.remaining(),&stats);}
            work.charge(stats);clock_invalid=clock_invalid||stats.clock_regressed;
            if(!verified&&native_player_owner_shared_read_failed(b.cpu_owner,stats)&&player_submission)player_submission->shared_uncertain=true;
            bool unresolved_shared_change=!verified&&native_player_owner_shared_compare_failed(b.cpu_owner,stats);
            // Changes to a different actor's exclusion claim may be refreshed,
            // but a new proof cannot change this actor or renew its pose age.
            if(unresolved_shared_change&&work.available()){
                submit_timing.refresh();NativeSubmitStageScope scope(submit_timing,NativeSubmitStage::Refresh);
                stats={};auto batch=capture_independent_player_owners({b.pose->player_identity},reason,work.remaining(),&stats);
                work.charge(stats);clock_invalid=clock_invalid||stats.clock_regressed;
                if(batch.valid&&batch.exclusions){reconcile_player_exclusions(batch.exclusions);unresolved_shared_change=false;}
                else if(player_submission)player_submission->shared_uncertain=true;
                if(batch.valid&&!batch.proof.empty()&&batch.players.size()==1){auto& row=batch.players.front();
                    if(same_player(row.identity,b.pose->player_identity)&&row.status==NativePlayerOwnerStatus::Ready&&
                       !row.candidate.guards.empty()&&same_owner_selection(b.cpu_owner,row.candidate)&&work.available()){
                        checked={row.candidate};stats={};
                        verified=verify_player_owners(checked,reason,work.remaining(),&stats);work.charge(stats);clock_invalid=clock_invalid||stats.clock_regressed;
                        if(!verified&&(native_player_owner_shared_read_failed(row.candidate,stats)||native_player_owner_shared_compare_failed(row.candidate,stats))&&player_submission)player_submission->shared_uncertain=true;
                    }
                }
            }
            if(unresolved_shared_change&&player_submission)player_submission->shared_uncertain=true;
            verified=verified&&checked.size()==1&&same_owner_selection(b.cpu_owner,checked.front())&&work.available();
            if(verified){b.cpu_owner=checked.front();retain_player_observation(b.pose->player_identity,b.cpu_owner);++ready_owners;}
            else{discard_player_observation(b.pose->player_identity);if(!reason.empty())owner_diagnostic("player-submission",stats);}
        }
        player_group_verified=verified;verified_players=verified?&checked:nullptr;
        b.visibility_clips=root->visibility_clips;b.gbuffer_clips=root->gbuffer_clips;
        submit_recording(command,subject,shape&&common&&verified,submitted_queue);
        if(b.data.permit[0]&&owner_process&&work.available()){
            struct BoundedMemory final:NativeOwnerMemory {const NativeOwnerMemory& source;PlayerOwnerWork& work;
                BoundedMemory(const NativeOwnerMemory& s,PlayerOwnerWork& w):source(s),work(w){}
                bool read(std::uint64_t a,void* p,std::size_t n)const noexcept override {
                    const auto limits=work.remaining();if(!work.available()||!n||n>limits.max_read||n>limits.max_bytes)return false;
                    ++work.calls;work.bytes+=n;const bool ok=source.read(a,p,n);return ok&&work.available();
                }
                std::uint64_t milliseconds()const noexcept override{return source.milliseconds();}
                std::uint64_t work_milliseconds()const noexcept override{return source.work_milliseconds();}
            } bounded(*owner_process,work);
            tool_slots.publish(bounded,b.cpu_owner,b.pose,b.instances.generation);
        }
        const auto fault=player_test.enabled&&player_test.started?player_test_decline(test_phase,b.pose&&(b.pose->player_identity.local||b.pose->subject.empty())):0u;
        const bool ready=fault!=1&&b.data.permit[0]&&b.pose&&player_current(*b.pose)&&
            work.available()&&native_pose_is_fresh(b.pose->published_milliseconds,GetTickCount64());
        b.data.extent[2]=fault==2?1u:0u; // Nonzero reserved input deliberately makes prepare decline.
        b.player_test_marker={player_test.enabled&&player_test.started?test_phase+1u:0u,test_elapsed};
        b.data.permit[0]=ready?1u:0u;if(!ready){b.data.history[0]=0;revoke_subject_preparation(b.instances,b.first_pass.extent,b.pose.get());}
        b.data.config[3]=native_prepare_remote_actor_mode(b.pose.get(),b.cpu_owner,ready,b.menu_preview);
        std::memcpy(b.control->mapped,&b.data,sizeof b.data);verified_players=nullptr;player_group_verified=false;
    });
    if(common&&ready_owners==2&&root->subjects.size()==1&&player_test.enabled&&!player_test.started){
        player_test.started=GetTickCount64();each_bundle(root,[&](Bundle& b){b.player_test_marker={1u,0u};});
    }
    if(clock_invalid&&player_submission)player_submission->shared_uncertain=true;
    // Finalize again after every command/view has finished its independent
    // work. No other actor can spend this actor's completed work allowance.
    each_bundle(root,[&](Bundle& b){if(clock_invalid||(b.pose&&!native_pose_is_fresh(b.pose->published_milliseconds,GetTickCount64())))decline_player_submission(b);});
    if(!common||clock_invalid){player_observations.clear();player_observation_valid=false;revoke_preparation(root->instances);}

}
