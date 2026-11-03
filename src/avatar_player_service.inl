// Included after the shared picker/catalogue helpers inside namespace evrm.
struct AvatarService::Impl {
    struct Request {std::uint64_t generation=0;std::string subject,path;std::uintptr_t owner=0;bool browse=false;};
    struct Subject {
        std::string id,token,path,label;
        NativePlayerIdentity identity;
        bool enabled=true,edited=false,account_unsaved=false,account_admission_failed=false;
        std::uint64_t account_intent=0;
        std::uint64_t desired=1;
        std::shared_ptr<const PreparedAvatar> active;
        AvatarCandidatePtr candidate,selection;
        AvatarLoadState state=AvatarLoadState::Ready;
        std::string error;
    };
    struct Look {
        std::shared_ptr<const AvatarAppearance> appearance;
        std::shared_ptr<const AvatarProfile> profile;
        std::shared_ptr<const std::vector<std::string>> materials;
    };
    struct Exports {
        std::shared_ptr<const std::vector<AvatarSubjectSelection>> subjects;
        std::shared_ptr<const AvatarMenuSnapshot> menu;
        std::map<std::string,Look> looks;
        std::uint64_t style=0;
    };
    AvatarServiceOptions options;
    std::mutex changes;
    std::atomic<bool> stopping{false},alive{true};
    std::map<std::string,Subject> states;
    std::deque<Request> requests;
    std::string selected;
    std::shared_ptr<const Exports> exports;
    std::shared_ptr<const NativePlayerRoster> early_roster;
    AvatarPreferences prefs;
    PlayerAssignments accounts;
    bool loaded=false,legacy_save_failed=false,account_save_failed=false,account_store_safe=false,refresh=true;
    std::uint64_t revision=0,style=1,sequence=1,prefs_revision=0,saved_prefs=0,account_revision=0,saved_accounts=0,account_intent=0,save_due=0,next_poll=0;
    std::shared_ptr<const std::vector<AvatarChoice>> choices=std::make_shared<const std::vector<AvatarChoice>>();
    HANDLE event=nullptr,thread=nullptr;
    explicit Impl(AvatarServiceOptions value):options(std::move(value)){
        Subject local;local.label="You";local.active=options.initial;
        if(options.initial){local.path=options.initial->path;local.selection=std::make_shared<const AvatarCandidate>(AvatarCandidate{1,options.initial,{}});}
        states.emplace("",std::move(local));legacy_save_failed=!options.save_available;
        publish();event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(event)thread=CreateThread(nullptr,0,[](void* raw)->DWORD{static_cast<Impl*>(raw)->run();return 0;},this,0,nullptr);
    }
    ~Impl(){stopping=true;if(event)SetEvent(event);if(thread){WaitForSingleObject(thread,INFINITE);CloseHandle(thread);}if(event)CloseHandle(event);}
    const AvatarSubjectSelection* lookup(const std::shared_ptr<const Exports>& saved,const std::string& id)const noexcept {
        if(saved&&saved->subjects)for(const auto& value:*saved->subjects)if(value.subject==id)return &value;
        return nullptr;
    }
    bool current(const Request& request)const noexcept {
        const auto saved=std::atomic_load(&exports);const auto* value=lookup(saved,request.subject);
        return !stopping.load()&&value&&value->desired_generation==request.generation;
    }
    std::shared_ptr<const Exports> prepare_exports(const Subject* replacement=nullptr,const AvatarPreferences* configured=nullptr,std::uint64_t next_style=0){
        const auto& preferences=configured?*configured:prefs;const auto style_id=next_style?next_style:style;
        auto result=std::make_shared<Exports>();result->style=style_id;
        const auto old=std::atomic_load(&exports);
        auto players=std::make_shared<std::vector<AvatarPlayerChoice>>();players->push_back({{},"You",true});
        for(const auto& entry:states)if(!entry.first.empty())players->push_back({entry.first,entry.second.label,false});
        auto values=std::make_shared<std::vector<AvatarSubjectSelection>>();values->reserve(states.size());
        for(const auto& entry:states){
            const auto& state=replacement&&replacement->id==entry.first?*replacement:entry.second;
            auto menu=std::make_shared<AvatarMenuSnapshot>();menu->subject=state.id;menu->players=players;menu->choices=choices;
            menu->preferences_loaded=loaded;menu->revision=revision+1;menu->replacement_enabled=state.enabled;
            menu->save_failed=legacy_save_failed||(state.identity.account&&(account_save_failed||state.account_admission_failed));menu->state=state.state;menu->error=state.error.substr(0,256);
            if(state.account_admission_failed&&menu->error.empty())menu->error="Choice is session-only: saved player capacity or storage unavailable";
            menu->path=state.path;
            if(!state.path.empty())menu->name=std::filesystem::u8path(state.path).filename().u8string();
            if(state.active){
                const auto& model=*state.active;menu->name=model.name;menu->path=model.path;menu->fingerprint=model.fingerprint;
                menu->decoded_texture_bytes=model.decoded_texture_bytes;menu->retained_cpu_buffer_bytes=model.retained_cpu_buffer_bytes;
                auto found=result->looks.find(model.fingerprint);
                if(found==result->looks.end()){
                    Look look;const auto previous=old?old->looks.find(model.fingerprint):std::map<std::string,Look>::const_iterator{};
                    if(old&&old->style==style_id&&previous!=old->looks.end()&&previous->second.materials&&*previous->second.materials==model.material_names)look=previous->second;
                    else{
                        look.materials=std::make_shared<const std::vector<std::string>>(model.material_names);
                        look.appearance=std::make_shared<const AvatarAppearance>(resolve_avatar_appearance(preferences,model.fingerprint,model.material_names,style_id));
                        AvatarProfile profile{model.fingerprint,AvatarShaderPreset::GameLighting,{}};
                        for(const auto& item:preferences.profiles)if(item.fingerprint==model.fingerprint){profile=item;break;}
                        look.profile=std::make_shared<const AvatarProfile>(std::move(profile));
                    }
                    found=result->looks.emplace(model.fingerprint,std::move(look)).first;
                }
                menu->materials=found->second.materials;menu->appearance=found->second.appearance;menu->profile=found->second.profile;
            }
            values->push_back({state.id,state.identity,state.desired,loaded&&state.enabled,state.active,state.candidate,state.selection,menu});
            if(state.id==selected)result->menu=menu;
        }
        result->subjects=std::move(values);return result;
    }
    void publish(){auto next=prepare_exports();++revision;std::atomic_store(&exports,std::move(next));}
    void dirty(bool legacy,bool account){if(legacy)++prefs_revision;if(account)++account_revision;save_due=GetTickCount64()+300;if(event)SetEvent(event);}
    bool request_locked(Subject& state,const std::string& path,bool browse,std::uintptr_t owner,bool user){
        if(sequence==UINT64_MAX)return false;
        Request next{++sequence,state.id,path,owner,browse};
        requests.erase(std::remove_if(requests.begin(),requests.end(),[&](const auto& item){return item.subject==state.id;}),requests.end());
        state.desired=next.generation;state.candidate.reset();state.edited|=user;state.error.clear();
        state.state=browse?AvatarLoadState::Choosing:path.empty()?AvatarLoadState::Ready:AvatarLoadState::Loading;
        if(browse||!path.empty())requests.push_back(std::move(next));
        publish();if(event)SetEvent(event);return true;
    }
    bool remember_account(PlayerAssignments& destination,const Subject& state){
        if(state.token.empty())return false;
        return set_player_assignment(destination,{state.token,state.path,state.enabled&&!state.path.empty()});
    }
    void save_account(Subject& state){
        if(!state.identity.account)return;
        state.account_unsaved=true;state.account_intent=++account_intent;
        if(!remember_account(accounts,state)){state.account_admission_failed=true;return;}
        if(!account_store_safe||!options.save_available)account_save_failed=true;
        dirty(false,true);
    }
    void remove_remote(){
        for(auto it=states.begin();it!=states.end();)if(!it->first.empty())it=states.erase(it);else ++it;
        requests.erase(std::remove_if(requests.begin(),requests.end(),[](const auto& item){return !item.subject.empty();}),requests.end());
        selected.clear();states.at("").identity={};
    }
    void apply_roster(const std::shared_ptr<const NativePlayerRoster>& roster){
        if(!loaded){early_roster=roster;return;}
        bool valid=bool(roster)&&roster->players.size()<=16;std::set<std::uint64_t> keys;std::set<std::uint32_t> actors;unsigned locals=0;
        if(valid)for(const auto& player:roster->players){
            valid=valid&&player.account&&player.connection&&player.entity_id&&player.entity_id<=16&&
                (player.connection&63)+1==player.entity_id&&keys.insert(player.account).second&&actors.insert(player.entity_id).second;
            locals+=player.local?1u:0u;
        }
        if(!valid||locals!=1){remove_remote();publish();return;}
        std::set<std::string> present;
        for(const auto& player:roster->players){
            auto token=player_assignment_token(accounts.secret,player.account);
            if(token.empty()){
                // RAM-only session labels grant no persistent identifier fallback.
                for(const auto& item:states)if(item.second.identity.account==player.account){token=item.first;break;}
                if(token.empty())token="session-"+std::to_string(++sequence);
                account_save_failed=true;
            }
            const auto id=player.local?std::string{}:token;present.insert(id);
            auto found=states.find(id);bool created=found==states.end();
            if(created){Subject state;state.id=id;state.label="Player "+std::to_string(player.entity_id);state.enabled=false;found=states.emplace(id,std::move(state)).first;}
            auto& state=found->second;
            const bool local_changed=player.local&&!state.token.empty()&&state.token!=token;
            const bool bind=created||state.token!=token||!state.identity.account;
            if(local_changed){state.active.reset();state.selection.reset();state.candidate.reset();request_locked(state,{},false,0,false);}
            state.identity=player;state.token=valid_avatar_fingerprint(token)?token:std::string{};
            if(!player.local)state.label="Player "+std::to_string(player.entity_id);
            if(bind){
                const PlayerAssignment* saved=nullptr;for(const auto& item:accounts.entries)if(item.token==state.token){saved=&item;break;}
                if(saved){state.path=saved->path;state.enabled=saved->replacement_enabled;}
                else if(player.local){state.path=prefs.selected_path.empty()&&state.active?state.active->path:prefs.selected_path;state.enabled=prefs.replacement_enabled;save_account(state);}
                if(!state.enabled){state.candidate.reset();if(!player.local){state.active.reset();state.selection.reset();}}
                else if(!state.path.empty()&&(!state.active||!same_path(state.path,state.active->path)))request_locked(state,state.path,false,0,false);
            }
        }
        for(auto it=states.begin();it!=states.end();)if(!it->first.empty()&&!present.count(it->first))it=states.erase(it);else ++it;
        requests.erase(std::remove_if(requests.begin(),requests.end(),[&](const auto& item){return !states.count(item.subject);}),requests.end());
        if(!states.count(selected))selected.clear();
        publish();
    }
    std::shared_ptr<const std::vector<AvatarChoice>> catalogue(const std::vector<std::shared_ptr<const PreparedAvatar>>& models,const AvatarPreferences& preferences){
        auto result=std::make_shared<std::vector<AvatarChoice>>();
        const auto add=[&](const std::string& path,const std::string& name,bool active){
            if(result->size()>=64)return;
            std::string normalized;if(active)normalized=path;else if(!normalize_avatar_path(path,normalized))return;
            if(std::any_of(result->begin(),result->end(),[&](const auto& item){return same_path(item.path,normalized);}))return;
            result->push_back({normalized,name.empty()?std::filesystem::u8path(normalized).filename().u8string():name});
        };
        for(const auto& model:models)if(model)add(model->path,model->name,true);
        for(const auto& path:preferences.recent)add(path,{},false);
        std::error_code ignored;unsigned visited=0;
        if(!options.catalogue_directory.empty())for(std::filesystem::directory_iterator it(options.catalogue_directory,ignored),end;!ignored&&it!=end&&visited++<1024;it.increment(ignored)){
            if(result->size()>=64)break;
            add(it->path().u8string(),{},false);
        }
        return result;
    }
    void load_preferences(){
        const auto path=options.directory/L"avatar-settings.json";
        const auto disk=options.directory.empty()?AvatarSettingsLoad{}:options.preferences_reader?options.preferences_reader(path,options.context):read_avatar_preferences(path);
        auto stored=options.directory.empty()?PlayerAssignmentsLoad{}:read_player_assignments(options.directory/L"player-model-assignments.json");
        std::lock_guard<std::mutex> lock(changes);
        if(stored.status==PlayerAssignmentsStatus::Loaded){accounts=std::move(stored.preferences);account_store_safe=true;}
        else{make_player_assignment_secret(accounts.secret);account_store_safe=stored.status==PlayerAssignmentsStatus::Missing;account_save_failed=!account_store_safe||!options.save_available;}
        auto& local=states.at("");
        if(disk.status==AvatarSettingsStatus::Loaded){prefs=disk.preferences;
            if(!local.edited){local.path=prefs.selected_path;local.enabled=prefs.replacement_enabled;}
            if(!local.enabled)request_locked(local,{},false,0,false);
            else if(!local.edited&&!prefs.selected_path.empty()&&(!local.active||!same_path(prefs.selected_path,local.active->path)))request_locked(local,prefs.selected_path,false,0,false);
        }
        if(disk.status==AvatarSettingsStatus::Invalid||disk.status==AvatarSettingsStatus::UnsupportedVersion||disk.status==AvatarSettingsStatus::Unavailable)local.error="Saved avatar preferences unavailable";
        loaded=true;++style;publish();if(early_roster){const auto roster=std::move(early_roster);apply_roster(roster);}
    }
    void process(Request request){
        if(!current(request))return;
        if(request.browse){
            bool released=!options.input_released;const auto end=GetTickCount64()+2000;
            while(current(request)&&!released&&GetTickCount64()<end){released=options.input_released(request.owner,options.context);if(!released)Sleep(5);}
            AvatarPickResult picked;if(!current(request))return;
            if(!released)picked.error="Close the menu before choosing an avatar";
            else try{picked=options.picker?options.picker(request.owner,options.context):native_picker(request.owner,nullptr);}catch(...){picked.error="Avatar file picker unavailable";}
            std::lock_guard<std::mutex> lock(changes);if(!current(request))return;auto& state=states.at(request.subject);
            if(picked.cancelled){state.state=AvatarLoadState::Ready;state.error.clear();publish();}
            else if(!picked.error.empty()||!valid_avatar_path(picked.path)){state.state=AvatarLoadState::Error;state.error=picked.error.empty()?"Choose a local VRM file":picked.error;publish();}
            else request_locked(state,picked.path,false,0,false);
            return;
        }
        AvatarLoadResult result;
        try{result=options.loader?options.loader(request.path,options.context):load_prepared_avatar(request.path);}
        catch(...){result.error="Avatar loading failed";}
        std::lock_guard<std::mutex> lock(changes);if(!current(request))return;auto& state=states.at(request.subject);
        if(!result||!valid_avatar_fingerprint(result.model->fingerprint)||!valid_avatar_path(result.model->path)){
            state.state=AvatarLoadState::Error;state.error=result.error.empty()?"Avatar preparation failed":result.error;publish();return;
        }
        for(const auto& item:states)if(item.second.active&&item.second.active->fingerprint==result.model->fingerprint){
            auto shared=std::make_shared<PreparedAvatar>(*result.model);shared->asset=item.second.active->asset;shared->skin=item.second.active->skin;
            shared->decoded_texture_bytes=item.second.active->decoded_texture_bytes;shared->retained_cpu_buffer_bytes=item.second.active->retained_cpu_buffer_bytes;
            result.model=std::move(shared);break;
        }
        state.candidate=std::make_shared<const AvatarCandidate>(AvatarCandidate{request.generation,std::move(result.model),request.subject});state.state=AvatarLoadState::Preparing;state.error.clear();publish();
    }
    void run()noexcept{
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        try{
            load_preferences();
            while(!stopping.load()){
                const auto now=GetTickCount64();
                if(options.players_reader&&now>=next_poll){const auto roster=options.players_reader(options.context);std::lock_guard<std::mutex> lock(changes);apply_roster(roster);next_poll=GetTickCount64()+500;}
                Request request;bool have_request=false,refresh_now=false;AvatarPreferences catalogue_prefs;std::vector<std::shared_ptr<const PreparedAvatar>> models;
                {std::lock_guard<std::mutex> lock(changes);
                    if(!requests.empty()){request=std::move(requests.front());requests.pop_front();have_request=true;}
                    refresh_now=refresh;refresh=false;if(refresh_now){catalogue_prefs=prefs;for(const auto& item:states)if(item.second.active)models.push_back(item.second.active);}
                }
                if(refresh_now){const auto list=catalogue(models,catalogue_prefs);std::lock_guard<std::mutex> lock(changes);choices=list;publish();}
                if(have_request){process(std::move(request));continue;}
                AvatarPreferences saving;PlayerAssignments saving_accounts;std::uint64_t prefs_version=0,account_version=0,saving_intent=0;DWORD timeout=options.players_reader?500:INFINITE;
                {std::lock_guard<std::mutex> lock(changes);if(prefs_revision!=saved_prefs||account_revision!=saved_accounts){const auto tick=GetTickCount64();
                    if(tick>=save_due){if(prefs_revision!=saved_prefs){saving=prefs;prefs_version=prefs_revision;}if(account_revision!=saved_accounts){saving_accounts=accounts;account_version=account_revision;saving_intent=account_intent;}}
                    else timeout=std::min(timeout,DWORD(save_due-tick));}
                    if(!requests.empty())continue;
                }
                if(prefs_version||account_version){
                    const bool saved_legacy=!prefs_version||(options.save_available&&!options.directory.empty()&&write_avatar_preferences(options.directory/L"avatar-settings.json",saving));
                    const bool saved_private=!account_version||(account_store_safe&&options.save_available&&!options.directory.empty()&&write_player_assignments(options.directory/L"player-model-assignments.json",saving_accounts));
                    std::lock_guard<std::mutex> lock(changes);
                    if(prefs_version){saved_prefs=prefs_version;legacy_save_failed=!saved_legacy;}
                    if(account_version){saved_accounts=account_version;account_save_failed=!saved_private;
                        if(saved_private)for(auto& item:states){auto& state=item.second;
                            if(!state.account_unsaved||state.account_intent>saving_intent)continue;
                            for(const auto& entry:saving_accounts.entries)if(entry.token==state.token&&entry.path==state.path&&entry.replacement_enabled==(state.enabled&&!state.path.empty())){
                                state.account_unsaved=false;state.account_admission_failed=false;break;
                            }
                        }
                    }
                    publish();continue;
                }
                if(WaitForSingleObject(event,timeout)==WAIT_FAILED)break;
            }
        }catch(...){try{std::lock_guard<std::mutex> lock(changes);for(auto& item:states){item.second.state=AvatarLoadState::Error;item.second.error="Avatar loader unavailable";}publish();}catch(...){}log_event("avatar-switch.service-unavailable");}
        alive=false;
    }
};
AvatarService::AvatarService(AvatarServiceOptions options)noexcept{try{impl_=std::make_unique<Impl>(std::move(options));}catch(...) {}}
AvatarService::~AvatarService()=default;
bool AvatarService::ready()const noexcept{return impl_&&impl_->thread&&impl_->alive.load(std::memory_order_acquire);}
std::shared_ptr<const std::vector<AvatarSubjectSelection>> AvatarService::subjects()const noexcept {const auto saved=impl_?std::atomic_load(&impl_->exports):nullptr;return saved?saved->subjects:nullptr;}
std::shared_ptr<const AvatarMenuSnapshot> AvatarService::snapshot(const std::string& subject)const noexcept {
    const auto saved=impl_?std::atomic_load(&impl_->exports):nullptr;const auto* item=impl_?impl_->lookup(saved,subject):nullptr;return item?item->menu:nullptr;
}
std::shared_ptr<const AvatarMenuSnapshot> AvatarService::menu_snapshot()const noexcept {const auto saved=impl_?std::atomic_load(&impl_->exports):nullptr;return saved?saved->menu:nullptr;}
std::shared_ptr<const PreparedAvatar> AvatarService::active(const std::string& subject)const noexcept {const auto saved=subjects();if(saved)for(const auto& item:*saved)if(item.subject==subject)return item.model;return {};}
AvatarCandidatePtr AvatarService::candidate(const std::string& subject)const noexcept {const auto saved=subjects();if(ready()&&saved)for(const auto& item:*saved)if(item.subject==subject)return item.candidate;return {};}
AvatarCandidatePtr AvatarService::selection(const std::string& subject)const noexcept {const auto saved=subjects();if(ready()&&saved)for(const auto& item:*saved)if(item.subject==subject)return item.selection;return {};}
std::uint64_t AvatarService::desired_generation(const std::string& subject)const noexcept {const auto saved=subjects();if(saved)for(const auto& item:*saved)if(item.subject==subject)return item.desired_generation;return 0;}
bool AvatarService::replacement_enabled(const std::string& subject)const noexcept {const auto saved=snapshot(subject);return saved&&saved->preferences_loaded&&saved->replacement_enabled;}
bool AvatarService::any_replacement_enabled()const noexcept {const auto saved=subjects();if(saved)for(const auto& item:*saved)if(item.replacement_enabled)return true;return false;}
std::shared_ptr<const AvatarAppearance> AvatarService::appearance_for(const std::shared_ptr<const PreparedAvatar>& model)const noexcept {
    if(!model)return {};
    const auto saved=impl_?std::atomic_load(&impl_->exports):nullptr;
    if(saved){const auto found=saved->looks.find(model->fingerprint);if(found!=saved->looks.end())return found->second.appearance;}return {};
}
bool AvatarService::request_model(const std::string& path,const std::string& subject)noexcept {
    if(!ready()||!valid_avatar_path(path))return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto found=impl_->states.find(subject);return found!=impl_->states.end()&&found->second.enabled&&impl_->request_locked(found->second,path,false,0,true);}catch(...){return false;}
}
bool AvatarService::request_browse(std::uintptr_t owner,const std::string& subject)noexcept {
    if(!ready())return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto found=impl_->states.find(subject);return found!=impl_->states.end()&&found->second.enabled&&impl_->request_locked(found->second,{},true,owner,true);}catch(...){return false;}
}
void AvatarService::cancel_request(const std::string& subject)noexcept {if(!ready())return;try{std::lock_guard<std::mutex> lock(impl_->changes);const auto found=impl_->states.find(subject);if(found!=impl_->states.end())impl_->request_locked(found->second,{},false,0,true);}catch(...) {}}
bool AvatarService::set_replacement_enabled(bool enabled,const std::string& subject)noexcept {
    if(!ready())return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto found=impl_->states.find(subject);if(!impl_->loaded||found==impl_->states.end())return false;
        auto& state=found->second;if(state.enabled==enabled)return true;state.enabled=enabled;state.edited=true;
        const auto path=enabled&&!state.active?state.path:std::string{};
        if(!enabled&&!subject.empty()){state.active.reset();state.selection.reset();}
        if(subject.empty()){impl_->prefs.replacement_enabled=enabled;impl_->dirty(true,false);}
        impl_->save_account(state);return impl_->request_locked(state,path,false,0,true);
    }catch(...){return false;}
}
bool AvatarService::select_player(const std::string& subject)noexcept {if(!ready())return false;try{std::lock_guard<std::mutex> lock(impl_->changes);if(!impl_->states.count(subject))return false;impl_->selected=subject;impl_->publish();return true;}catch(...){return false;}}
void AvatarService::observe_players(std::shared_ptr<const NativePlayerRoster> roster)noexcept {if(!ready())return;try{std::lock_guard<std::mutex> lock(impl_->changes);impl_->apply_roster(roster);if(impl_->event)SetEvent(impl_->event);}catch(...) {}}
void AvatarService::refresh_catalogue()noexcept {if(!ready())return;try{std::lock_guard<std::mutex> lock(impl_->changes);impl_->refresh=true;SetEvent(impl_->event);}catch(...) {}}
bool AvatarService::set_whole_preset(const std::string& fingerprint,AvatarShaderPreset preset)noexcept {
    if(!ready())return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);bool known=false;for(const auto& item:impl_->states)known|=item.second.active&&item.second.active->fingerprint==fingerprint;
        if(!impl_->loaded||!known)return false;
        auto prefs=impl_->prefs;if(!set_avatar_preset(prefs,fingerprint,preset))return false;
        const auto next=impl_->prepare_exports(nullptr,&prefs,impl_->style+1);impl_->prefs=std::move(prefs);++impl_->style;++impl_->revision;std::atomic_store(&impl_->exports,next);impl_->dirty(true,false);return true;
    }catch(...){return false;}
}
bool AvatarService::set_material_preset(const std::string& fingerprint,std::uint32_t index,std::optional<AvatarShaderPreset> preset)noexcept {
    if(!ready())return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);std::shared_ptr<const PreparedAvatar> model;for(const auto& item:impl_->states)if(item.second.active&&item.second.active->fingerprint==fingerprint){model=item.second.active;break;}
        if(!impl_->loaded||!model||index>=model->material_names.size())return false;
        auto prefs=impl_->prefs;if(!set_avatar_material(prefs,fingerprint,index,model->material_names[index],preset))return false;
        const auto next=impl_->prepare_exports(nullptr,&prefs,impl_->style+1);impl_->prefs=std::move(prefs);++impl_->style;++impl_->revision;std::atomic_store(&impl_->exports,next);impl_->dirty(true,false);return true;
    }catch(...){return false;}
}
bool AvatarService::reset_materials(const std::string& fingerprint)noexcept {
    if(!ready())return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);bool known=false;for(const auto& item:impl_->states)known|=item.second.active&&item.second.active->fingerprint==fingerprint;
        if(!impl_->loaded||!known)return false;
        auto prefs=impl_->prefs;if(!reset_avatar_materials(prefs,fingerprint))return false;
        const auto next=impl_->prepare_exports(nullptr,&prefs,impl_->style+1);impl_->prefs=std::move(prefs);++impl_->style;++impl_->revision;std::atomic_store(&impl_->exports,next);impl_->dirty(true,false);return true;
    }catch(...){return false;}
}
bool AvatarService::commit_candidate(const AvatarCandidatePtr& candidate,bool(*activate)(void*,const AvatarCandidate&)noexcept,void* context)noexcept {
    if(!ready()||!candidate||!activate)return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto found=impl_->states.find(candidate->subject);
        if(found==impl_->states.end()||!found->second.enabled||found->second.desired!=candidate->generation||found->second.candidate!=candidate)return false;
        auto state=found->second;state.active=candidate->model;state.selection=candidate;state.candidate.reset();state.path=candidate->model->path;state.state=AvatarLoadState::Ready;state.error.clear();
        auto prefs=impl_->prefs;const auto local_path=prefs.selected_path;remember(prefs,*candidate->model);if(!candidate->subject.empty())prefs.selected_path=local_path;else prefs.replacement_enabled=state.enabled;
        if(!valid_avatar_preferences(prefs))return false;
        auto accounts=impl_->accounts;const bool account_ok=!state.identity.account||impl_->remember_account(accounts,state);
        if(state.identity.account){state.account_unsaved=true;state.account_intent=impl_->account_intent+1;state.account_admission_failed|=!account_ok;}
        // Preallocate all exported model/menu states before touching device state.
        const auto next=impl_->prepare_exports(&state,&prefs,impl_->style+1);
        if(!activate(context,*candidate))return false;
        found->second=std::move(state);impl_->prefs=std::move(prefs);impl_->accounts=std::move(accounts);++impl_->style;++impl_->revision;
        if(found->second.identity.account)impl_->account_intent=found->second.account_intent;
        if(!impl_->account_store_safe&&found->second.identity.account)impl_->account_save_failed=true;
        std::atomic_store(&impl_->exports,next);impl_->refresh=true;impl_->dirty(true,found->second.identity.account&&account_ok);
        log_event_lazy([&]{return "avatar-switch.committed generation="+std::to_string(candidate->generation);});return true;
    }catch(...){return false;}
}
bool AvatarService::adopt_selection(const AvatarCandidatePtr& selected,bool(*activate)(void*,const AvatarCandidate&)noexcept,void* context)noexcept {
    if(!ready()||!selected||!activate)return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto found=impl_->states.find(selected->subject);if(found==impl_->states.end()||!found->second.enabled||found->second.selection!=selected)return false;
        if(!activate(context,*selected))return false;
        log_event_lazy([&]{return "avatar-switch.adopted generation="+std::to_string(selected->generation);});return true;
    }catch(...){return false;}
}
void AvatarService::fail_candidate(std::uint64_t generation,const std::string& reason,const std::string& subject)noexcept {
    if(!ready())return;
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto found=impl_->states.find(subject);if(found==impl_->states.end()||found->second.desired!=generation||!found->second.candidate)return;
        found->second.candidate.reset();found->second.state=AvatarLoadState::Error;found->second.error=reason.substr(0,256);impl_->publish();
    }catch(...) {}
}
}
