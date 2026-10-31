#include "avatar_service.hpp"
#include "runtime_log.hpp"
#include <shobjidl.h>
#include <algorithm>
#include <atomic>
#include <mutex>
namespace evrm {
namespace {
template<class T>struct Com {T* value=nullptr;~Com(){if(value)value->Release();}T** put(){return &value;}T* operator->()const{return value;}};
std::string utf8(const wchar_t* text){
    if(!text)return {};
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,nullptr,0,nullptr,nullptr);if(count<=1||count>4097)return {};
    std::string result(std::size_t(count),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,result.data(),count,nullptr,nullptr))return {};
    result.pop_back();return result;
}
bool same_path(const std::string& a,const std::string& b){
    const auto left=std::filesystem::u8path(a).lexically_normal().wstring(),right=std::filesystem::u8path(b).lexically_normal().wstring();
    return CompareStringOrdinal(left.data(),int(left.size()),right.data(),int(right.size()),TRUE)==CSTR_EQUAL;
}
AvatarPickResult native_picker(std::uintptr_t raw,void*){
    const auto window=reinterpret_cast<HWND>(raw);DWORD process=0;
    if(!window||!IsWindow(window)||!GetWindowThreadProcessId(window,&process)||process!=GetCurrentProcessId())return {{},"Avatar file picker unavailable",false};
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(initialized))return {{},"Avatar file picker unavailable",false};
    struct Cleanup {~Cleanup(){CoUninitialize();}} cleanup;
    Com<IFileOpenDialog> dialog;auto result=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dialog.put()));
    if(FAILED(result))return {{},"Avatar file picker unavailable",false};
    FILEOPENDIALOGOPTIONS options=0;result=dialog->GetOptions(&options);
    if(SUCCEEDED(result))result=dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR|FOS_DONTADDTORECENT);
    const COMDLG_FILTERSPEC filter{L"VRM avatars",L"*.vrm"};
    if(SUCCEEDED(result))result=dialog->SetFileTypes(1,&filter);
    if(SUCCEEDED(result))result=dialog->SetDefaultExtension(L"vrm");
    if(SUCCEEDED(result))result=dialog->SetTitle(L"Choose an avatar");
    if(FAILED(result))return {{},"Avatar file picker unavailable",false};
    result=dialog->Show(window);if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {{},{},true};
    if(FAILED(result))return {{},"Avatar file picker could not open",false};
    Com<IShellItem> item;result=dialog->GetResult(item.put());if(FAILED(result))return {{},"Cannot read selected avatar",false};
    wchar_t* path=nullptr;result=item->GetDisplayName(SIGDN_FILESYSPATH,&path);
    struct Release {wchar_t* value;~Release(){CoTaskMemFree(value);}} release{path};
    auto value=SUCCEEDED(result)?utf8(path):std::string{};
    if(!valid_avatar_path(value))return {{},"Choose a local VRM file",false};
    return {std::move(value),{},false};
}
void remember(AvatarPreferences& prefs,const PreparedAvatar& model){
    prefs.selected_path=model.path;
    prefs.recent.erase(std::remove_if(prefs.recent.begin(),prefs.recent.end(),[&](const auto& path){return same_path(path,model.path);}),prefs.recent.end());
    prefs.recent.insert(prefs.recent.begin(),model.path);if(prefs.recent.size()>16)prefs.recent.resize(16);
    const auto found=std::find_if(prefs.profiles.begin(),prefs.profiles.end(),[&](const auto& profile){return profile.fingerprint==model.fingerprint;});
    AvatarProfile profile=found==prefs.profiles.end()?AvatarProfile{model.fingerprint,AvatarShaderPreset::GameLighting,{}}:*found;
    if(found!=prefs.profiles.end())prefs.profiles.erase(found);
    if(prefs.profiles.size()==16)prefs.profiles.erase(prefs.profiles.begin());
    prefs.profiles.push_back(std::move(profile));
}
}
struct AvatarService::Impl {
    struct Request {std::uint64_t generation=1;std::string path;std::uintptr_t owner=0;bool browse=false;};
    AvatarServiceOptions options;std::mutex changes;
    std::atomic<bool> stopping{false},alive{true};
    std::atomic<std::uint64_t> desired{1};
    std::shared_ptr<const PreparedAvatar> active_;
    AvatarCandidatePtr candidate_;
    AvatarCandidatePtr selection_;
    std::shared_ptr<const AvatarMenuSnapshot> snapshot_;
    std::shared_ptr<const Request> request_=std::make_shared<const Request>();
    AvatarPreferences prefs;
    std::shared_ptr<const std::vector<AvatarChoice>> choices=std::make_shared<const std::vector<AvatarChoice>>();
    std::shared_ptr<const AvatarAppearance> appearance;
    std::shared_ptr<const AvatarProfile> profile;
    std::shared_ptr<const std::vector<std::string>> materials;
    AvatarLoadState state=AvatarLoadState::Ready;std::string error;
    bool edited=false,loaded=false,save_failed=false,refresh=true;
    std::uint64_t revision=0,style_revision=0,prefs_revision=0,saved_revision=0,save_due=0;
    HANDLE event=nullptr,thread=nullptr;
    explicit Impl(AvatarServiceOptions value):options(std::move(value)),active_(options.initial){
        if(options.initial)selection_=std::make_shared<const AvatarCandidate>(AvatarCandidate{1,options.initial});
        update_appearance();publish();event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(event)thread=CreateThread(nullptr,0,[](void* raw)->DWORD{static_cast<Impl*>(raw)->run();return 0;},this,0,nullptr);
    }
    ~Impl(){stopping=true;if(event)SetEvent(event);if(thread){WaitForSingleObject(thread,INFINITE);CloseHandle(thread);}if(event)CloseHandle(event);}
    std::shared_ptr<const AvatarMenuSnapshot> make_snapshot(const std::shared_ptr<const PreparedAvatar>& model,const AvatarPreferences& preferences,AvatarLoadState next,const std::string& message,bool new_style){
        auto result=std::make_shared<AvatarMenuSnapshot>();result->state=next;result->preferences_loaded=loaded;result->save_failed=save_failed;result->revision=revision+1;
        result->error=message.substr(0,256);result->choices=choices;
        if(model){result->name=model->name;result->path=model->path;result->fingerprint=model->fingerprint;
            if(new_style){result->materials=std::make_shared<const std::vector<std::string>>(model->material_names);
                result->appearance=std::make_shared<const AvatarAppearance>(resolve_avatar_appearance(preferences,model->fingerprint,model->material_names,style_revision+1));
                AvatarProfile selected{model->fingerprint,AvatarShaderPreset::GameLighting,{}};
                for(const auto& entry:preferences.profiles)if(entry.fingerprint==model->fingerprint){selected=entry;break;}
                result->profile=std::make_shared<const AvatarProfile>(std::move(selected));
            }else{result->materials=materials;result->appearance=appearance;result->profile=profile;}}
        return result;
    }
    void update_appearance(){
        const auto next=make_snapshot(std::atomic_load(&active_),prefs,state,error,true);++style_revision;
        appearance=next->appearance;profile=next->profile;materials=next->materials;
    }
    void publish(){auto next=make_snapshot(std::atomic_load(&active_),prefs,state,error,false);++revision;std::atomic_store(&snapshot_,std::move(next));}
    void dirty(){++prefs_revision;save_due=GetTickCount64()+300;SetEvent(event);}
    bool current(std::uint64_t generation)const noexcept{return !stopping.load()&&desired.load(std::memory_order_acquire)==generation;}
    bool request_locked(const std::string& path,bool browse,std::uintptr_t owner,bool user){
        const auto generation=desired.load();if(generation==UINT64_MAX)return false;
        auto request=std::make_shared<const Request>(Request{generation+1,path,owner,browse});
        desired.store(generation+1,std::memory_order_release);request_=std::move(request);std::atomic_store(&candidate_,AvatarCandidatePtr{});
        edited|=user;state=browse?AvatarLoadState::Choosing:path.empty()?AvatarLoadState::Ready:AvatarLoadState::Loading;error.clear();publish();SetEvent(event);return true;
    }
    std::shared_ptr<const std::vector<AvatarChoice>> catalogue(const std::shared_ptr<const PreparedAvatar>& model,const AvatarPreferences& preferences){
        auto result=std::make_shared<std::vector<AvatarChoice>>();
        const auto add=[&](const std::string& path,const std::string& name,bool active){
            if(result->size()>=64)return;
            std::string normalized;if(active)normalized=path;else if(!normalize_avatar_path(path,normalized))return;
            if(std::any_of(result->begin(),result->end(),[&](const auto& entry){return same_path(entry.path,normalized);}))return;
            result->push_back({normalized,name.empty()?std::filesystem::u8path(normalized).filename().u8string():name});};
        if(model)add(model->path,model->name,true);
        for(const auto& path:preferences.recent)add(path,{},false);
        std::error_code ignored;unsigned visited=0;
        if(!options.catalogue_directory.empty())for(std::filesystem::directory_iterator it(options.catalogue_directory,ignored),end;!ignored&&it!=end&&visited++<1024;it.increment(ignored)){
            if(result->size()>=64)break;
            add(it->path().u8string(),{},false);
        }
        return result;
    }
    void run()noexcept{
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        try{
            const auto path=options.directory/L"avatar-settings.json";
            const auto disk=options.directory.empty()?AvatarSettingsLoad{}:options.preferences_reader?options.preferences_reader(path,options.context):read_avatar_preferences(path);
            {std::lock_guard<std::mutex> lock(changes);loaded=true;
                if(disk.status==AvatarSettingsStatus::Loaded){prefs=disk.preferences;update_appearance();
                    const auto model=std::atomic_load(&active_);
                    if(!edited&&!prefs.selected_path.empty()&&(!model||!same_path(prefs.selected_path,model->path)))request_locked(prefs.selected_path,false,0,false);}
                if(disk.status==AvatarSettingsStatus::Invalid||disk.status==AvatarSettingsStatus::UnsupportedVersion||disk.status==AvatarSettingsStatus::Unavailable)error="Saved avatar preferences unavailable";
                publish();}
            std::uint64_t handled=0;
            while(!stopping.load()){
                std::shared_ptr<const Request> request;bool refresh_now=false;AvatarPreferences catalogue_prefs;std::shared_ptr<const PreparedAvatar> catalogue_model;
                {std::lock_guard<std::mutex> lock(changes);request=request_;refresh_now=refresh;refresh=false;
                    if(refresh_now){catalogue_prefs=prefs;catalogue_model=std::atomic_load(&active_);}}
                if(refresh_now){const auto list=catalogue(catalogue_model,catalogue_prefs);std::lock_guard<std::mutex> lock(changes);choices=list;publish();}
                if(request->generation!=handled){handled=request->generation;
                    if(request->browse){
                        bool released=!options.input_released;const auto end=GetTickCount64()+2000;
                        while(current(request->generation)&&!released&&GetTickCount64()<end){released=options.input_released(request->owner,options.context);if(!released)Sleep(5);}
                        AvatarPickResult picked;
                        if(!current(request->generation))continue;
                        if(!released)picked.error="Close the menu before choosing an avatar";
                        else try{picked=options.picker?options.picker(request->owner,options.context):native_picker(request->owner,nullptr);}
                            catch(...){picked.error="Avatar file picker unavailable";}
                        std::lock_guard<std::mutex> lock(changes);if(!current(request->generation))continue;
                        if(picked.cancelled){state=AvatarLoadState::Ready;error.clear();publish();}
                        else if(!picked.error.empty()||!valid_avatar_path(picked.path)){state=AvatarLoadState::Error;error=picked.error.empty()?"Choose a local VRM file":picked.error;publish();}
                        else{request_=std::make_shared<const Request>(Request{request->generation,picked.path,0,false});state=AvatarLoadState::Loading;error.clear();publish();handled=0;continue;}
                    }else if(!request->path.empty()){
                        AvatarLoadResult result;
                        try{result=options.loader?options.loader(request->path,options.context):load_prepared_avatar(request->path);}
                        catch(const std::exception& failure){result.error=failure.what();}
                        catch(...){result.error="Avatar loading failed";}
                        std::lock_guard<std::mutex> lock(changes);if(!current(request->generation))continue;
                        if(!result||!valid_avatar_fingerprint(result.model->fingerprint)||!valid_avatar_path(result.model->path)){
                            state=AvatarLoadState::Error;error=result.error.empty()?"Avatar preparation failed":result.error;publish();}
                        else{std::atomic_store(&candidate_,std::make_shared<const AvatarCandidate>(AvatarCandidate{request->generation,std::move(result.model)}));state=AvatarLoadState::Preparing;error.clear();publish();}
                    }
                }
                AvatarPreferences saving;std::uint64_t saving_revision=0;DWORD timeout=INFINITE;
                {std::lock_guard<std::mutex> lock(changes);if(prefs_revision!=saved_revision){const auto now=GetTickCount64();
                    if(now>=save_due){saving=prefs;saving_revision=prefs_revision;}else timeout=DWORD(save_due-now);}}
                if(saving_revision){const auto saved=!options.directory.empty()&&write_avatar_preferences(path,saving);
                    std::lock_guard<std::mutex> lock(changes);saved_revision=saving_revision;save_failed=!saved;publish();continue;}
                // Recheck a newly queued request before sleeping; the event also
                // covers a request that arrives after this check.
                if(desired.load()!=handled)continue;
                if(WaitForSingleObject(event,timeout)==WAIT_FAILED)break;
            }
        }catch(...){
            try{std::lock_guard<std::mutex> lock(changes);state=AvatarLoadState::Error;error="Avatar loader unavailable";publish();}catch(...){}
            log_event("avatar-switch.service-unavailable");
        }
        alive=false;
    }
};
AvatarService::AvatarService(AvatarServiceOptions options)noexcept{try{impl_=std::make_unique<Impl>(std::move(options));}catch(...) {}}
AvatarService::~AvatarService()=default;
bool AvatarService::ready()const noexcept{return impl_&&impl_->thread&&impl_->alive.load(std::memory_order_acquire);}
std::shared_ptr<const AvatarMenuSnapshot> AvatarService::snapshot()const noexcept{return impl_?std::atomic_load(&impl_->snapshot_):nullptr;}
std::shared_ptr<const PreparedAvatar> AvatarService::active()const noexcept{return impl_?std::atomic_load(&impl_->active_):nullptr;}
AvatarCandidatePtr AvatarService::candidate()const noexcept{return ready()?std::atomic_load(&impl_->candidate_):nullptr;}
AvatarCandidatePtr AvatarService::selection()const noexcept{return ready()?std::atomic_load(&impl_->selection_):nullptr;}
std::uint64_t AvatarService::desired_generation()const noexcept{return impl_?impl_->desired.load(std::memory_order_acquire):0;}
bool AvatarService::request_model(const std::string& path)noexcept{if(!ready()||!valid_avatar_path(path))return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);return impl_->request_locked(path,false,0,true);}catch(...){SetEvent(impl_->event);return false;}}
bool AvatarService::request_browse(std::uintptr_t owner)noexcept{if(!ready())return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);return impl_->request_locked({},true,owner,true);}catch(...){SetEvent(impl_->event);return false;}}
void AvatarService::cancel_request()noexcept{if(!ready())return;try{std::lock_guard<std::mutex> lock(impl_->changes);impl_->request_locked({},false,0,true);}catch(...) {}}
void AvatarService::refresh_catalogue()noexcept{if(!ready())return;try{std::lock_guard<std::mutex> lock(impl_->changes);impl_->refresh=true;SetEvent(impl_->event);}catch(...) {}}
bool AvatarService::set_whole_preset(const std::string& fingerprint,AvatarShaderPreset preset)noexcept{if(!ready())return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto model=active();if(!impl_->loaded||!model||model->fingerprint!=fingerprint)return false;
        auto prefs=impl_->prefs;if(!set_avatar_preset(prefs,fingerprint,preset))return false;
        impl_->prefs=std::move(prefs);impl_->edited=true;impl_->update_appearance();impl_->publish();impl_->dirty();return true;}catch(...){return false;}}
bool AvatarService::set_material_preset(const std::string& fingerprint,std::uint32_t index,std::optional<AvatarShaderPreset> preset)noexcept{if(!ready())return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto model=active();if(!impl_->loaded||!model||model->fingerprint!=fingerprint||index>=model->material_names.size())return false;
        auto prefs=impl_->prefs;if(!set_avatar_material(prefs,fingerprint,index,model->material_names[index],preset))return false;
        impl_->prefs=std::move(prefs);impl_->edited=true;impl_->update_appearance();impl_->publish();impl_->dirty();return true;}catch(...){return false;}}
bool AvatarService::reset_materials(const std::string& fingerprint)noexcept{if(!ready())return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);const auto model=active();if(!impl_->loaded||!model||model->fingerprint!=fingerprint)return false;
        auto prefs=impl_->prefs;if(!reset_avatar_materials(prefs,fingerprint))return false;
        impl_->prefs=std::move(prefs);impl_->edited=true;impl_->update_appearance();impl_->publish();impl_->dirty();return true;}catch(...){return false;}}
bool AvatarService::commit_candidate(const AvatarCandidatePtr& candidate,bool(*activate)(void*,const AvatarCandidate&)noexcept,void* context)noexcept{
    if(!ready()||!candidate||!activate)return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);
        if(!impl_->current(candidate->generation)||std::atomic_load(&impl_->candidate_)!=candidate)return false;
        auto prefs=impl_->prefs;remember(prefs,*candidate->model);if(!valid_avatar_preferences(prefs))return false;
        // Allocate every commit snapshot before the renderer/backend handoff.
        const auto next=impl_->make_snapshot(candidate->model,prefs,AvatarLoadState::Ready,{},true);
        if(!activate(context,*candidate))return false;
        impl_->prefs=std::move(prefs);impl_->state=AvatarLoadState::Ready;impl_->error.clear();++impl_->style_revision;++impl_->revision;
        impl_->appearance=next->appearance;impl_->profile=next->profile;impl_->materials=next->materials;
        std::atomic_store(&impl_->selection_,candidate);std::atomic_store(&impl_->active_,candidate->model);std::atomic_store(&impl_->candidate_,AvatarCandidatePtr{});std::atomic_store(&impl_->snapshot_,next);
        impl_->refresh=true;impl_->dirty();log_event_lazy([&]{return "avatar-switch.committed generation="+std::to_string(candidate->generation);});return true;
    }catch(...){return false;}
}
bool AvatarService::adopt_selection(const AvatarCandidatePtr& selected,bool(*activate)(void*,const AvatarCandidate&)noexcept,void* context)noexcept{
    if(!ready()||!selected||!activate)return false;
    try{std::lock_guard<std::mutex> lock(impl_->changes);
        if(std::atomic_load(&impl_->selection_)!=selected)return false;
        // Same lock order and prepared GPU/pose callback as the first device;
        // adopting does not rewrite global selection, preferences or a proposal.
        if(!activate(context,*selected))return false;
        log_event_lazy([&]{return "avatar-switch.adopted generation="+std::to_string(selected->generation);});return true;
    }catch(...){return false;}
}
void AvatarService::fail_candidate(std::uint64_t generation,const std::string& reason)noexcept{if(!ready())return;
    try{std::lock_guard<std::mutex> lock(impl_->changes);if(!impl_->current(generation)||!std::atomic_load(&impl_->candidate_))return;
        std::atomic_store(&impl_->candidate_,AvatarCandidatePtr{});impl_->state=AvatarLoadState::Error;impl_->error=reason.substr(0,256);impl_->publish();}catch(...) {}}
}
