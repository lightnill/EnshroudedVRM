#include "avatar_fixture.hpp"
#include "../src/avatar_service.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
static std::string bytes(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    using namespace evrm;const auto root=std::filesystem::path(argv[1]);
    const auto prepared=prepare_avatar_bytes(avatar_fixture_bytes("Read-only synthetic avatar"),(root/L"owned.vrm").u8string());
    if(!prepared)return 3;
    AvatarPreferences preferences;preferences.selected_path=prepared.model->path;
    const auto path=root/L"avatar-settings.json";if(!write_avatar_preferences(path,preferences))return 4;const auto original=bytes(path);
    AvatarServiceOptions options;options.directory=root;options.catalogue_directory=root/L"independent-models";options.initial=prepared.model;options.save_available=false;
    AvatarService service(options);const auto end=GetTickCount64()+4000;
    while((!service.snapshot() || !service.snapshot()->preferences_loaded) && GetTickCount64()<end)Sleep(3);
    if(!service.ready() || !service.snapshot() || !service.snapshot()->preferences_loaded || !service.snapshot()->save_failed)return 10;
    if(!service.set_whole_preset(prepared.model->fingerprint,AvatarShaderPreset::Matte))return 11;
    const auto live=service.snapshot();if(!live->appearance || live->appearance->whole!=AvatarShaderPreset::Matte)return 12;
    Sleep(400);if(bytes(path)!=original || !service.snapshot()->save_failed)return 13;
    std::cout<<"PASS live avatar material edits with explicit read-only settings and unchanged preferences\n";return 0;
}
