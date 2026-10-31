#include "avatar_fixture.hpp"
#include "../src/avatar_service.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
namespace {
using Preset=evrm::AvatarShaderPreset;
void need(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
template<class F>void until(F&& fn,const char* why){const auto end=GetTickCount64()+5000;while(!fn()&&GetTickCount64()<end)Sleep(3);need(fn(),why);}
struct Models {std::shared_ptr<const evrm::PreparedAvatar> a,b;};
evrm::AvatarLoadResult load(const std::string& path,void* raw){
    const auto& models=*static_cast<Models*>(raw);
    auto next=std::make_shared<evrm::PreparedAvatar>(path.find("model-b.vrm")!=std::string::npos?*models.b:*models.a);
    next->path=path;return {std::move(next),{}};
}
bool accept(void*,const evrm::AvatarCandidate&)noexcept{return true;}
void choose(evrm::AvatarService& service,const std::string& path){
    need(service.request_model(path),"model request rejected");
    until([&]{const auto c=service.candidate();return c&&c->model->path==path;},"requested model not prepared");
    need(service.commit_candidate(service.candidate(),accept,nullptr),"model commit rejected");
}
const evrm::AvatarProfile* find(const evrm::AvatarPreferences& prefs,const std::string& key){
    for(const auto& p:prefs.profiles)if(p.fingerprint==key)return &p;
    return nullptr;
}
bool matches(const evrm::AvatarProfile* p,Preset whole,std::optional<Preset> override){
    return p&&p->whole==whole&&p->overrides.size()==(override?1u:0u)&&
        (!override||(p->overrides[0].index==0&&p->overrides[0].name=="Skin"&&p->overrides[0].preset==*override));
}
void live(evrm::AvatarService& service,const std::string& key,Preset whole,std::optional<Preset> override){
    const auto value=service.snapshot();need(value&&value->fingerprint==key,"active model identity changed");
    need(matches(value->profile.get(),whole,override),"whole/material preferences crossed model identities");
    need(value->appearance&&value->appearance->materials.size()==1&&value->appearance->materials[0]==override.value_or(whole),"resolved material disagrees with saved profile");
}
void saved(const std::filesystem::path& path,const Models& models,std::optional<Preset> b_override){
    until([&]{const auto disk=evrm::read_avatar_preferences(path);
        return disk.status==evrm::AvatarSettingsStatus::Loaded&&disk.preferences.profiles.size()==2&&
            matches(find(disk.preferences,models.a->fingerprint),Preset::Matte,Preset::GameLighting)&&
            matches(find(disk.preferences,models.b->fingerprint),Preset::GameLighting,b_override);
    },"two independent shader profiles not saved to real settings file");
}
}
int wmain(int argc,wchar_t** argv){try{
    need(argc==2,"fixture folder missing");const auto folder=std::filesystem::path(argv[1]);const auto file=folder/L"avatar-settings.json";
    const auto a=evrm::prepare_avatar_bytes(avatar_fixture_bytes("Model A"),"C:\\fixture\\model-a.vrm");
    const auto b=evrm::prepare_avatar_bytes(avatar_fixture_bytes("Model B"),"C:\\fixture\\model-b.vrm");
    need(bool(a)&&bool(b),"fixture models failed to import");Models models{a.model,b.model};
    need(models.a->fingerprint!=models.b->fingerprint,"fixture models must have different content fingerprints");
    need(models.a->material_names==models.b->material_names,"fixture materials must have matching names to test identity isolation");
    evrm::AvatarServiceOptions options;options.directory=folder;options.initial=models.a;options.loader=load;options.context=&models;
    {
        evrm::AvatarService service(options);need(service.ready(),"background service unavailable");
        until([&]{return service.snapshot()->preferences_loaded;},"initial preferences not ready");
        need(service.set_whole_preset(models.a->fingerprint,Preset::Matte)&&service.set_material_preset(models.a->fingerprint,0,Preset::GameLighting),"model A shader edit failed");
        live(service,models.a->fingerprint,Preset::Matte,Preset::GameLighting);
        choose(service,models.b->path);live(service,models.b->fingerprint,Preset::GameLighting,std::nullopt);
        need(service.set_material_preset(models.b->fingerprint,0,Preset::Matte),"model B material edit failed");
        live(service,models.b->fingerprint,Preset::GameLighting,Preset::Matte);
        choose(service,models.a->path);live(service,models.a->fingerprint,Preset::Matte,Preset::GameLighting);
        choose(service,"C:\\fixture\\same-model-renamed.vrm");live(service,models.a->fingerprint,Preset::Matte,Preset::GameLighting);
        choose(service,models.a->path);saved(file,models,Preset::Matte);
        until([&]{return evrm::read_avatar_preferences(file).preferences.selected_path==models.a->path;},"final selected model not saved");
    }
    {
        evrm::AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"restart preferences not ready");
        live(service,models.a->fingerprint,Preset::Matte,Preset::GameLighting);
        need(service.set_whole_preset(models.a->fingerprint,Preset::GameLighting),"whole preset edit failed");
        live(service,models.a->fingerprint,Preset::GameLighting,Preset::GameLighting);
        need(service.set_whole_preset(models.a->fingerprint,Preset::Matte),"whole preset restore failed");
        choose(service,models.b->path);live(service,models.b->fingerprint,Preset::GameLighting,Preset::Matte);
        need(service.set_material_preset(models.b->fingerprint,0,std::nullopt),"inherit material rejected");
        live(service,models.b->fingerprint,Preset::GameLighting,std::nullopt);saved(file,models,std::nullopt);
        need(service.set_material_preset(models.b->fingerprint,0,Preset::Matte),"override recreation failed");saved(file,models,Preset::Matte);
        need(service.reset_materials(models.b->fingerprint),"reset overrides rejected");
        live(service,models.b->fingerprint,Preset::GameLighting,std::nullopt);saved(file,models,std::nullopt);
        choose(service,models.a->path);live(service,models.a->fingerprint,Preset::Matte,Preset::GameLighting);
        until([&]{return evrm::read_avatar_preferences(file).preferences.selected_path==models.a->path;},"restart selection not saved");
    }
    {
        evrm::AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"second restart preferences not ready");
        live(service,models.a->fingerprint,Preset::Matte,Preset::GameLighting);
        choose(service,models.b->path);live(service,models.b->fingerprint,Preset::GameLighting,std::nullopt);
        saved(file,models,std::nullopt);
    }
    std::cout<<"PASS two distinct model shader profiles survive A/B changes, identical material names, renamed model, two service restarts, whole-preset edits, inheritance and reset; real temporary settings file\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
