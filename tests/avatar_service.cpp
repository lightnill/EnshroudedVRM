#include "avatar_fixture.hpp"
#include "../src/avatar_service.hpp"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void until(F&& fn,const char* reason){const auto end=GetTickCount64()+5000;while(!fn()&&GetTickCount64()<end)Sleep(3);need(fn(),reason);}
struct Control {std::shared_ptr<const evrm::PreparedAvatar> model;std::atomic<bool> entered{false},release{false},hold_preferences{false},preferences_entered{false};std::atomic<unsigned> commits{0},picker_calls{0};};
evrm::AvatarLoadResult load(const std::string& path,void* raw){auto& c=*static_cast<Control*>(raw);
    if(path.find("throw")!=std::string::npos)throw std::runtime_error("Synthetic loader exception");
    if(path.find("slow")!=std::string::npos){c.entered=true;while(!c.release.load())Sleep(1);}
    if(path.find("bad")!=std::string::npos)return {{},"Synthetic import failure"};
    if(path.find("below")!=std::string::npos)return evrm::prepare_avatar_bytes(avatar_fixture_bytes("Below origin",-1.f),path);
    auto result=std::make_shared<evrm::PreparedAvatar>(*c.model);result->path=path;return {result,{}};}
evrm::AvatarPickResult pick(std::uintptr_t,void* raw){++static_cast<Control*>(raw)->picker_calls;return {{},{},true};}
evrm::AvatarSettingsLoad preferences(const std::filesystem::path& path,void* raw){auto& c=*static_cast<Control*>(raw);c.preferences_entered=true;
    while(c.hold_preferences.load())Sleep(1);
    return evrm::read_avatar_preferences(path);}
bool commit(void* raw,const evrm::AvatarCandidate&)noexcept{++static_cast<Control*>(raw)->commits;return true;}
}
int wmain(int argc,wchar_t** argv){try{
    need(argc==2,"fixture folder");const auto folder=std::filesystem::path(argv[1]);
    Control c;auto prepared=evrm::prepare_avatar_bytes(avatar_fixture_bytes(),"C:\\fixture\\active.vrm");need(bool(prepared),prepared.error.c_str());c.model=prepared.model;
    evrm::AvatarServiceOptions options;options.directory=folder;options.catalogue_directory=folder/L"Avatars";options.initial=c.model;
    std::filesystem::create_directory(options.catalogue_directory);
    {std::ofstream file(options.catalogue_directory/L"library.VRM",std::ios::binary);const auto bytes=avatar_fixture_bytes();file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    std::ofstream(options.catalogue_directory/L"ignore.txt")<<"Not an avatar";
    options.loader=load;options.picker=pick;options.preferences_reader=preferences;options.context=&c;
    {evrm::AvatarService service(options);need(service.ready(),"service worker unavailable");until([&]{return service.snapshot()->preferences_loaded;},"preferences not loaded");
        until([&]{const auto value=service.snapshot();return value->choices&&value->choices->size()==2;},"catalogue missed local uppercase VRM or included non-VRM file");
        need(service.request_model("C:\\fixture\\slow.vrm"),"slow request");until([&]{return c.entered.load();},"slow loader not entered");
        need(service.active()==c.model,"current model disappeared during loading");
        need(service.request_model("C:\\fixture\\obsolete.vrm")&&service.request_model("C:\\fixture\\latest.vrm"),"superseding requests");c.release=true;
        until([&]{const auto value=service.candidate();return value&&value->model->path=="C:\\fixture\\latest.vrm";},"late old load replaced latest request");
        const auto candidate=service.candidate();need(service.active()==c.model,"candidate installed before GPU/pose commit");
        need(service.request_model("C:\\fixture\\newest.vrm"),"new request");need(!service.commit_candidate(candidate,commit,&c)&&!c.commits,"stale generation activated");
        until([&]{const auto value=service.candidate();return value&&value->model->path=="C:\\fixture\\newest.vrm";},"newest candidate missing");
        need(service.commit_candidate(service.candidate(),commit,&c)&&c.commits==1,"ready candidate commit");
        const auto active=service.active();need(active->path=="C:\\fixture\\newest.vrm","committed wrong model");
        need(service.set_whole_preset(active->fingerprint,evrm::AvatarShaderPreset::Matte),"whole edit");
        need(service.set_material_preset(active->fingerprint,0,evrm::AvatarShaderPreset::GameLighting),"material edit");
        need(service.snapshot()->appearance->materials[0]==evrm::AvatarShaderPreset::GameLighting,"live override missing");
        need(service.request_model("C:\\fixture\\bad.vrm"),"bad request");until([&]{return service.snapshot()->state==evrm::AvatarLoadState::Error;},"import error not reported");
        need(service.active()==active,"failed import replaced active model");
        until([&]{return evrm::read_avatar_preferences(folder/L"avatar-settings.json").status==evrm::AvatarSettingsStatus::Loaded;},"successful selection not saved");
        const auto disk=evrm::read_avatar_preferences(folder/L"avatar-settings.json");need(disk.preferences.selected_path==active->path,"failed selection persisted as current");
        need(service.request_browse(0),"fake browse request");until([&]{return c.picker_calls.load()==1&&service.snapshot()->state==evrm::AvatarLoadState::Ready;},"cancelled picker left busy state");
        need(service.active()==active,"picker cancellation replaced model");
        need(service.request_model("C:\\fixture\\throw.vrm"),"throwing loader request");until([&]{return service.snapshot()->state==evrm::AvatarLoadState::Error;},"loader exception stranded loading status");
        need(service.ready()&&service.active()==active,"loader exception killed service or replaced current model");
        need(service.request_model("C:\\fixture\\cancelled.vrm"),"cancel request");service.cancel_request();Sleep(30);need(!service.candidate()&&service.active()==active,"cancelled request published");
        need(service.request_model("C:\\fixture\\recover.vrm"),"recovery request");until([&]{return bool(service.candidate());},"service failed to recover");
        need(service.commit_candidate(service.candidate(),commit,&c),"recovery commit failed");
    }
    const auto late=folder/L"late";std::filesystem::create_directory(late);evrm::AvatarPreferences file_prefs;file_prefs.selected_path="C:\\fixture\\disk.vrm";
    file_prefs.recent={file_prefs.selected_path};file_prefs.profiles={{c.model->fingerprint,evrm::AvatarShaderPreset::Matte,{{0,"Skin",evrm::AvatarShaderPreset::GameLighting}}},{std::string(64,'b'),evrm::AvatarShaderPreset::Matte,{}}};
    need(evrm::write_avatar_preferences(late/L"avatar-settings.json",file_prefs),"late-load file");options.directory=late;c.hold_preferences=true;c.preferences_entered=false;
    {evrm::AvatarService service(options);until([&]{return c.preferences_entered.load();},"late file gate not entered");need(service.request_model("C:\\fixture\\live.vrm"),"early live choice");c.hold_preferences=false;
        until([&]{auto value=service.candidate();return value&&value->model->path=="C:\\fixture\\live.vrm";},"late file overwrote live model choice");
        need(service.commit_candidate(service.candidate(),commit,&c),"early live choice commit failed");
        until([&]{const auto disk=evrm::read_avatar_preferences(late/L"avatar-settings.json");return disk.status==evrm::AvatarSettingsStatus::Loaded&&disk.preferences.selected_path=="C:\\fixture\\live.vrm";},"live choice not saved");
        const auto saved=evrm::read_avatar_preferences(late/L"avatar-settings.json").preferences;
        need(saved.profiles.size()==2&&saved.recent.size()==2,"early live selection erased unrelated saved profiles/history");
        need(service.snapshot()->profile->whole==evrm::AvatarShaderPreset::Matte&&service.snapshot()->profile->overrides.size()==1,"late preferences lost untouched current-model overrides");}
    options.directory=folder/L"gated";std::filesystem::create_directory(options.directory);need(evrm::write_avatar_preferences(options.directory/L"avatar-settings.json",file_prefs),"gated preferences file");c.hold_preferences=true;c.preferences_entered=false;
    {evrm::AvatarService service(options);until([&]{return c.preferences_entered.load();},"appearance preference gate not entered");
        const bool accepted=service.set_whole_preset(c.model->fingerprint,evrm::AvatarShaderPreset::GameLighting);c.hold_preferences=false;
        need(!accepted,"appearance edit accepted before saved profiles were available");until([&]{return service.snapshot()->preferences_loaded;},"appearance preference gate did not finish");
        need(service.set_whole_preset(c.model->fingerprint,evrm::AvatarShaderPreset::GameLighting)&&service.snapshot()->profile->overrides.size()==1,"loaded whole edit failed or erased material override");}
    options.directory=folder/L"empty";std::filesystem::create_directory(options.directory);options.initial.reset();
    {evrm::AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"empty preference load");
        need(service.request_model("C:\\fixture\\bad.vrm"),"initial failed choice");until([&]{return service.snapshot()->state==evrm::AvatarLoadState::Error;},"initial failure not reported");
        need(service.request_model("C:\\fixture\\below.vrm"),"incompatible choice");until([&]{return service.snapshot()->state==evrm::AvatarLoadState::Error;},"incompatible choice stayed preparing without current model");
        need(!service.active()&&!service.candidate(),"incompatible empty-state choice activated");
        need(service.request_model("C:\\fixture\\recovery.vrm"),"empty recovery choice");until([&]{return bool(service.candidate());},"valid empty-state recovery missing");need(service.commit_candidate(service.candidate(),commit,&c)&&service.active(),"empty-state recovery commit");}
    std::cout<<"PASS background model latest-wins/commit/cancel/error recovery, live appearance, successful-only persistence and late-file isolation; no dialog opened\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
