#ifndef NOMINMAX
#define NOMINMAX
#endif
#if __has_include("../src/avatar_player_types.hpp")
#include "avatar_fixture.hpp"
#include "../src/avatar_service.hpp"
#include "../src/player_assignments.hpp"
#if defined(EVRM_PLAYER_REPORT_PRIVACY)
#include "../src/debug_report_sources.hpp"
#include "../src/debug_report_policy.hpp"
#include "../src/mod_settings.hpp"
#endif
#if __has_include("../src/avatar_actions.hpp")
#include "../src/avatar_actions.hpp"
#endif
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef EVRM_PLAYER_SAVE_FIXTURE
namespace save_fixture {std::atomic<bool> hold{false},entered{false},release{false};std::atomic<unsigned> writes{0},failures{0},last_error{0};}
namespace evrm {bool fixture_write_player_assignments(const std::filesystem::path& path,const PlayerAssignments& data)noexcept {
    const bool saved=write_player_assignments(path,data);
    ++save_fixture::writes;if(!saved){++save_fixture::failures;save_fixture::last_error=GetLastError();}
    if(save_fixture::hold.exchange(false)){save_fixture::entered=true;while(!save_fixture::release)Sleep(1);}
    return saved;
}}
#define write_player_assignments fixture_write_player_assignments
#include "../src/avatar_service.cpp"
#undef write_player_assignments
#endif
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void until(F test,const char* why){const auto end=GetTickCount64()+6000;do{if(test())return;Sleep(2);}while(GetTickCount64()<end);need(false,why);}
struct Control {std::atomic<bool> hold{false},entered{false},release{false};};
evrm::AvatarLoadResult load(const std::string& path,void* raw){
    auto& c=*static_cast<Control*>(raw);if(path.find("slow")!=std::string::npos){c.entered=true;while(!c.release)Sleep(1);}
    if(path.find("bad")!=std::string::npos)return {{},"Cannot read selected model"};
    return evrm::prepare_avatar_bytes(avatar_fixture_bytes(path.find("B")!=std::string::npos?"B":"A"),path);
}
evrm::AvatarPickResult pick(std::uintptr_t,void* raw){auto& c=*static_cast<Control*>(raw);c.entered=true;while(c.hold&&!c.release)Sleep(1);return {"C:/fixture/B-picked.vrm",{},false};}
bool commit(void*,const evrm::AvatarCandidate&)noexcept{return true;}
std::shared_ptr<evrm::NativePlayerRoster> roster(std::uint64_t remote=0xfedcba9876543210ULL,std::uint32_t connection=65){
    auto value=std::make_shared<evrm::NativePlayerRoster>();value->observed_milliseconds=GetTickCount64();value->players.push_back({0x0123456789abcdefULL,1,64,true});
    if(remote)value->players.push_back({remote,(connection&63)+1,connection,false});
    return value;
}
std::string remote_subject(evrm::AvatarService& service){
    const auto snapshot=service.menu_snapshot();if(snapshot&&snapshot->players)for(const auto& player:*snapshot->players)if(!player.local)return player.subject;return {};
}
void observe(evrm::AvatarService& service,std::shared_ptr<const evrm::NativePlayerRoster> players,bool remote=true){
    service.observe_players(std::move(players));until([&]{return !remote_subject(service).empty()==remote;},"roster update not reflected in menu");
}
void activate(evrm::AvatarService& service,const std::string& subject){until([&]{return bool(service.candidate(subject));},"targeted candidate not prepared");need(service.commit_candidate(service.candidate(subject),commit,nullptr),"targeted candidate not committed");}
void saved(evrm::AvatarService& service,const std::filesystem::path& path){
    until([&]{auto value=evrm::read_player_assignments(path);return value.status==evrm::PlayerAssignmentsStatus::Loaded&&value.preferences.entries.size()==2&&!service.menu_snapshot()->save_failed;},"account choices not saved");
}
}
int wmain(int argc,wchar_t** argv){SetErrorMode(0x8003);try{
    using namespace evrm;need(argc==3,"mode and owned fixture directory required");const std::wstring mode=argv[1];const auto folder=std::filesystem::path(argv[2]);Control control;
    AvatarServiceOptions options;options.directory=folder;options.context=&control;options.loader=load;options.picker=pick;
    options.initial=prepare_avatar_bytes(avatar_fixture_bytes("A"),"C:/fixture/A.vrm").model;need(bool(options.initial),"initial model preparation failed");
    const auto assignment_file=folder/L"player-model-assignments.json";
    if(mode==L"capacity"||mode==L"stale-save"){
#ifdef EVRM_PLAYER_SAVE_FIXTURE
        PlayerAssignments full;need(make_player_assignment_secret(full.secret),"secret");
        const auto local_token=player_assignment_token(full.secret,roster()->players[0].account);
        need(set_player_assignment(full,{local_token,options.initial->path,true}),"local store entry");
        for(unsigned n=1;n<512;++n)need(set_player_assignment(full,{player_assignment_token(full.secret,1000+n),"C:/fixture/A.vrm",true}),"fill store");
        need(write_player_assignments(assignment_file,full),"full store write");
        AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"preferences load");observe(service,roster());const auto remote=remote_subject(service);
        const auto prior_writes=save_fixture::writes.load();
        if(mode==L"stale-save"){
            save_fixture::hold=true;need(service.set_replacement_enabled(false),"existing account edit");
            until([&]{return save_fixture::entered.load();},"save completion not held");
            const bool enabled=service.set_replacement_enabled(true,remote);save_fixture::release=true;
            need(enabled&&service.snapshot(remote)->save_failed,"new capacity failure not visible while old save pending");
        }else{
            need(service.set_replacement_enabled(true,remote)&&service.request_model("C:/fixture/B.vrm",remote),"session-only model request");activate(service,remote);
            need(service.snapshot(remote)->save_failed,"capacity failure absent immediately after commit");
            need(service.set_replacement_enabled(false),"existing account save request");
        }
        until([&]{
            // An open destination reader can deny Windows' atomic replacement.
            if(save_fixture::writes.load()<=prior_writes)return false;
            need(save_fixture::failures.load()==0,"existing account write failed");
            const auto data=read_player_assignments(assignment_file);
            for(const auto& item:data.preferences.entries)if(item.token==local_token)return !item.replacement_enabled&&!service.snapshot()->save_failed;
            return false;
        },"existing account save did not complete");
        Sleep(350);need(service.snapshot(remote)->save_failed,"unrelated or stale save cleared rejected account warning");
        need(service.snapshot(remote)->error.find("session-only")!=std::string::npos,"capacity warning did not explain session-only state");
        const auto data=read_player_assignments(assignment_file);need(data.preferences.entries.size()==512,"full store evicted an entry");
        for(const auto& item:data.preferences.entries)need(item.token!=remote,"capacity test unexpectedly persisted new account");
#else
        throw std::runtime_error("save fixture required");
#endif
    }else if(mode==L"targeting"){
        AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"initial preferences not loaded");observe(service,roster());const auto remote=remote_subject(service);
        need(!service.replacement_enabled(remote)&&!service.active(remote)&&!service.request_model("C:/fixture/B.vrm",remote),"unassigned remote player replaced or loaded by default");
        need(service.set_replacement_enabled(true,remote)&&service.request_model("C:/fixture/B.vrm",remote),"remote model request rejected");
        need(service.select_player(remote)&&service.menu_snapshot()->subject==remote,"Player selector does not target remote");
        need(service.select_player(""),"cannot return to You");activate(service,remote);
        need(service.active(remote)->name=="B"&&service.active()->name=="A","switching UI redirected remote selection to local avatar");
        need(service.set_replacement_enabled(false)&&!service.replacement_enabled()&&service.replacement_enabled(remote),"local None disables assigned remote");
        const auto before=service.active(remote);need(service.request_model("C:/fixture/bad.vrm",remote),"failure request rejected");until([&]{return service.snapshot(remote)->state==AvatarLoadState::Error;},"targeted import failure not visible");
        need(service.active(remote)==before&&service.active()==options.initial,"failed remote import replaced an active model");
        saved(service,assignment_file);
    }else if(mode==L"restart"){
        std::string remote;
        {AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"preferences load");observe(service,roster());remote=remote_subject(service);
            need(service.set_replacement_enabled(true,remote)&&service.request_model("C:/fixture/B.vrm",remote),"remote selection");activate(service,remote);saved(service,assignment_file);}
        {AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"restart preferences load");observe(service,roster(0xfedcba9876543210ULL,193));
            need(remote_subject(service)==remote&&service.replacement_enabled(remote),"same account with new connection lost remembered assignment");activate(service,remote);
            need(service.active(remote)->path=="C:/fixture/B.vrm","restart selected wrong remote model");
            need(service.set_replacement_enabled(false,remote),"remote None");try{until([&]{const auto data=read_player_assignments(assignment_file);for(const auto& entry:data.preferences.entries)if(entry.token==remote)return !entry.replacement_enabled&&entry.path=="C:/fixture/B.vrm";return false;},"remote None/path not persisted");}
            catch(...){const auto data=read_player_assignments(assignment_file);bool present=false,enabled=false,path_matches=false;for(const auto& entry:data.preferences.entries)if(entry.token==remote){present=true;enabled=entry.replacement_enabled;path_matches=entry.path=="C:/fixture/B.vrm";}
                std::cerr<<"DIAG store_loaded="<<(data.status==PlayerAssignmentsStatus::Loaded)<<" entries="<<data.preferences.entries.size()<<" remote_present="<<present<<" enabled="<<enabled<<" path_matches="<<path_matches<<" menu_save_failed="<<service.snapshot(remote)->save_failed;
#ifdef EVRM_PLAYER_SAVE_FIXTURE
                std::cerr<<" writes="<<save_fixture::writes<<" failures="<<save_fixture::failures<<" last_error="<<save_fixture::last_error;
#endif
                std::cerr<<'\n';throw;}}
        {AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"None restart preferences");observe(service,roster());need(!service.replacement_enabled(remote)&&!service.candidate(remote),"explicit None enabled on restart");
            need(service.snapshot(remote)->path=="C:/fixture/B.vrm","None erased remembered model");observe(service,roster(0x8877665544332211ULL,129));const auto other=remote_subject(service);
            need(other!=remote&&!service.replacement_enabled(other)&&!service.active(other),"different account reused another account's slot assignment");}
    }else if(mode==L"pending"){
        AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"preferences load");observe(service,roster());const auto remote=remote_subject(service);
        need(service.set_replacement_enabled(true,remote),"remote enable");control.hold=true;need(service.request_browse(0,remote),"targeted picker");until([&]{return control.entered.load();},"picker not entered");
        need(service.select_player(""),"UI target switch");control.release=true;activate(service,remote);
        need(service.active(remote)->name=="B"&&service.active()->name=="A","picker result followed UI selection instead of captured player");
        control.entered=false;control.release=false;need(service.request_model("C:/fixture/slow-A.vrm",remote),"slow target request");until([&]{return control.entered.load();},"slow import not entered");
        service.observe_players(roster(0x8877665544332211ULL,129));control.release=true;
        until([&]{return remote_subject(service)!=remote;},"reused slot not reconciled");const auto other=remote_subject(service);
        need(!service.candidate(other)&&!service.active(other)&&!service.replacement_enabled(other),"late import activated a replacement on recycled player slot");
    }else if(mode==L"sharing"){
        AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"preferences load");observe(service,roster());const auto remote=remote_subject(service);
        need(service.set_replacement_enabled(true,remote)&&service.request_model("C:/fixture/A-other-path.vrm",remote),"shared model request");activate(service,remote);
        need(service.active(remote)->asset==service.active()->asset&&service.active(remote)->skin==service.active()->skin,"same model allocated duplicate immutable assets");
        const auto& shared_model=*service.active();
        need(shared_model.retained_cpu_buffer_bytes>0&&service.snapshot()->retained_cpu_buffer_bytes==shared_model.retained_cpu_buffer_bytes&&
             service.snapshot(remote)->retained_cpu_buffer_bytes==shared_model.retained_cpu_buffer_bytes&&
             service.snapshot(remote)->decoded_texture_bytes==shared_model.decoded_texture_bytes,
             "shared model menu snapshots lost cached numeric memory usage");
        const auto fingerprint=service.active()->fingerprint;need(service.set_whole_preset(fingerprint,AvatarShaderPreset::Enhanced),"shared model Enhanced edit");
        need(service.snapshot(remote)->appearance->whole==AvatarShaderPreset::Enhanced&&service.snapshot()->appearance->whole==AvatarShaderPreset::Enhanced,"same-model assignments lost shared shader preset");
        need(service.appearance_for(service.active(remote))->whole==AvatarShaderPreset::Enhanced,"render appearance lookup ignores remote model");
        need(service.set_material_preset(fingerprint,0,AvatarShaderPreset::Fast),"shared Fast override edit");
        need(service.snapshot(remote)->appearance->materials[0]==AvatarShaderPreset::Fast&&service.snapshot()->appearance->materials[0]==AvatarShaderPreset::Fast,"same-model assignments lost shared material override");
        const auto appearance_file=folder/L"avatar-settings.json";
        until([&]{const auto disk=read_avatar_preferences(appearance_file);return disk.status==AvatarSettingsStatus::Loaded&&disk.preferences.profiles.size()==1&&disk.preferences.profiles[0].whole==AvatarShaderPreset::Enhanced&&disk.preferences.profiles[0].overrides.size()==1&&disk.preferences.profiles[0].overrides[0].preset==AvatarShaderPreset::Fast;},"shared Enhanced/Fast profile not persisted");
        need(service.set_material_preset(fingerprint,0,std::nullopt),"shared material inheritance edit");
        need(service.snapshot(remote)->appearance->materials[0]==AvatarShaderPreset::Enhanced&&service.snapshot()->appearance->materials[0]==AvatarShaderPreset::Enhanced,"shared inheritance did not restore Enhanced");
        need(service.set_material_preset(fingerprint,0,AvatarShaderPreset::Fast)&&service.reset_materials(fingerprint),"shared material reset edit");
        need(service.snapshot(remote)->appearance->materials[0]==AvatarShaderPreset::Enhanced&&service.snapshot()->appearance->materials[0]==AvatarShaderPreset::Enhanced,"shared reset did not restore Enhanced");
        need(service.set_whole_preset(fingerprint,AvatarShaderPreset::Fast),"shared Fast model edit");
        need(service.snapshot(remote)->appearance->materials[0]==AvatarShaderPreset::Fast&&service.snapshot()->appearance->materials[0]==AvatarShaderPreset::Fast,"shared Fast model not resolved for both players");
    }else if(mode==L"routing"){
#if __has_include("../src/avatar_actions.hpp")
        AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"preferences load");observe(service,roster());const auto remote=remote_subject(service);
        AvatarMenuAction action;action.kind=AvatarActionKind::SetReplacementEnabled;action.subject=remote;action.replacement_enabled=true;apply_avatar_action_to(service,action,0);
        action.kind=AvatarActionKind::SelectPlayer;apply_avatar_action_to(service,action,0);need(service.menu_snapshot()->subject==remote,"runtime action did not select requested player");
        action.kind=AvatarActionKind::SelectModel;action.path="C:/fixture/B.vrm";apply_avatar_action_to(service,action,0);activate(service,remote);
        need(service.active()->name=="A"&&service.active(remote)->name=="B","runtime router applied remote selection locally");
        action.kind=AvatarActionKind::SetReplacementEnabled;action.replacement_enabled=false;apply_avatar_action_to(service,action,0);
        need(service.replacement_enabled()&&!service.replacement_enabled(remote),"runtime None action disabled wrong player");
#else
        throw std::runtime_error("targeted runtime action routing is not implemented");
#endif
    }else if(mode==L"privacy"){
#if defined(EVRM_PLAYER_REPORT_PRIVACY)
        AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"preferences load");observe(service,roster());const auto remote=remote_subject(service);
        need(service.set_replacement_enabled(true,remote)&&service.request_model("C:/fixture/PRIVATE_MODEL_PATH_CANARY-B.vrm",remote),"private test selection");activate(service,remote);saved(service,assignment_file);
        need(service.select_player(remote),"private target selection");const auto snapshot=service.menu_snapshot();const auto stored=read_player_assignments(assignment_file);
        need(snapshot&&snapshot->subject==remote&&stored.status==PlayerAssignmentsStatus::Loaded,"actual saved snapshot unavailable");
        debug_report_begin_sources(17,1000);ModSettings settings;debug_report_observe_settings(settings,snapshot.get());debug_report_observe_model(service.active(remote).get());
        debug_report_end_source_frame(1010);debug_report_end_source_frame(1020);const auto report=make_debug_report_contents(debug_report_take_sources(17,1020));
        std::string secret;static constexpr char digits[]="0123456789abcdef";for(const auto byte:stored.preferences.secret){secret.push_back(digits[byte>>4]);secret.push_back(digits[byte&15]);}
        need(!report.empty()&&secret.size()==64&&remote.size()==64,"privacy fixture did not exercise saved account data");
        const std::vector<std::string> forbidden={remote,secret,snapshot->path,"PRIVATE_MODEL_PATH_CANARY","18364758544493064720","fedcba9876543210","81985529216486895","0123456789abcdef"};
        for(const auto& file:report){const std::string text(file.bytes.begin(),file.bytes.end());for(const auto& canary:forbidden)need(text.find(canary)==std::string::npos,"private saved assignment/snapshot data entered report");}
        debug_report_cancel_sources(17);
#else
        throw std::runtime_error("report privacy fixture dependencies missing");
#endif
    }else if(mode==L"corrupt"){
        const std::string bad="{private-invalid-assignment-file}";{std::ofstream file(assignment_file);file<<bad;}
        AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"preferences load");observe(service,roster());const auto remote=remote_subject(service);
        need(!service.replacement_enabled(remote),"corrupt assignment store enabled remote");need(service.set_replacement_enabled(true,remote)&&service.request_model("C:/fixture/B.vrm",remote),"session-only choice unavailable");activate(service,remote);
        need(service.snapshot(remote)->save_failed,"corrupt store did not report session-only/save failure");Sleep(400);std::ifstream file(assignment_file);const std::string after((std::istreambuf_iterator<char>(file)),{});need(after==bad,"session-only selection overwrote corrupt preferences");
    }else throw std::runtime_error("unknown mode");
    std::cout<<"PASS independent player model service behavior\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
#else
#include <iostream>
int main(){std::cerr<<"FAIL targeted player model service is not implemented\n";return 1;}
#endif
