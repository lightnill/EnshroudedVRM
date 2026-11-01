#include "../src/standalone_startup.hpp"
#include "../src/avatar_runtime.hpp"
#include "../src/mod_settings_service.hpp"
#include "../src/mod_input_win32.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>
namespace fs=std::filesystem;
namespace evrm {
RuntimeStartupContext fixture;const RuntimeStartupContext& runtime_startup_context()noexcept{return fixture;}
std::atomic<unsigned> avatar_creations{0},voice_requests{0},replacement_requests{0};bool replacement_value=true;AvatarServiceOptions captured;
struct AvatarService::Impl {};
AvatarService::AvatarService(AvatarServiceOptions options)noexcept{captured=std::move(options);++avatar_creations;}
AvatarService::~AvatarService()=default;
bool AvatarService::ready()const noexcept{return true;}
std::shared_ptr<const AvatarMenuSnapshot> AvatarService::snapshot()const noexcept{return {};}
bool AvatarService::request_model(const std::string&)noexcept{return false;}
bool AvatarService::request_browse(std::uintptr_t)noexcept{return false;}
bool AvatarService::set_replacement_enabled(bool enabled)noexcept{++replacement_requests;replacement_value=enabled;return true;}
void AvatarService::cancel_request()noexcept{}
void AvatarService::refresh_catalogue()noexcept{}
bool AvatarService::set_whole_preset(const std::string&,AvatarShaderPreset)noexcept{return false;}
bool AvatarService::set_material_preset(const std::string&,std::uint32_t,std::optional<AvatarShaderPreset>)noexcept{return false;}
bool AvatarService::reset_materials(const std::string&)noexcept{return false;}
bool mod_input_open(HWND)noexcept{return false;}
void request_voice_inputs(const ModSettings&)noexcept{++voice_requests;}
}
static bool until(bool(*condition)()) {const auto end=GetTickCount64()+3000;do{if(condition())return true;Sleep(5);}while(GetTickCount64()<end);return false;}
static std::string read(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    using namespace evrm;const fs::path root=argv[1];const bool writable=std::wstring(argv[2])==L"writable";fs::create_directories(root);
    const auto file=root/L"mod-settings.json";ModSettings preferences;preferences.strength=91;
    if(!write_mod_settings(file,preferences))return 3;
    const auto original=read(file);
    fixture.status=RuntimeStartupStatus::Declined;fixture.diagnostic_owner_verified=true;fixture.state_directory=root;
    AvatarMenuAction original_action;original_action.kind=AvatarActionKind::SetReplacementEnabled;original_action.replacement_enabled=false;
    apply_avatar_action(original_action,0);if(replacement_requests)return 17;
    if(request_mod_settings(preferences))return 10;
    initialize_mod_settings_service(root);initialize_avatar_runtime();Sleep(20);
    if(mod_settings_loaded() || avatar_creations || voice_requests)return 11;
    fixture.status=RuntimeStartupStatus::Ready;fixture.state_directory=root;fixture.avatars_directory=root/L"installed/Avatars";fixture.state_writable=writable;
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_DIR",(root/L"poison/capture").c_str());
    initialize_avatar_runtime();initialize_avatar_runtime();
    if(avatar_creations!=1 || captured.directory!=root || captured.catalogue_directory!=fixture.avatars_directory)return 12;
    apply_avatar_action(original_action,0);if(replacement_requests!=1 || replacement_value || voice_requests)return 18;
    initialize_mod_settings_service();initialize_mod_settings_service(root/L"ignored");
    if(!until([]{return mod_settings_loaded();}) || mod_settings_snapshot().strength!=91 || voice_requests!=1)return 13;
    auto edited=preferences;edited.strength=72;if(!request_mod_settings(edited))return 14;
    if(writable) {
        const auto end=GetTickCount64()+3000;
        while(read_mod_settings(file).settings.strength!=72 && GetTickCount64()<end)Sleep(5);
        if(read_mod_settings(file).settings.strength!=72 || mod_settings_save_failed())return 15;
    }else {
        Sleep(400);if(read(file)!=original || !mod_settings_save_failed())return 16;
    }
    std::cout<<"native admission before once-only avatar/settings workers; independent catalogue and save availability passed\n";return 0;
}
