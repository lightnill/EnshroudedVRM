#include "../src/avatar_settings.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void write(const std::filesystem::path& path,const std::string& data){std::ofstream file(path,std::ios::binary);file<<data;}
}
int wmain(int argc,wchar_t** argv){try{
    using namespace evrm;need(argc==2,"fixture folder required");const auto file=std::filesystem::path(argv[1])/L"avatar-settings.json";
    const std::string a(64,'a'),b(64,'b');AvatarPreferences prefs;
    need(valid_avatar_preferences(prefs),"defaults invalid");need(read_avatar_preferences(file).status==AvatarSettingsStatus::Missing,"missing preferences");
    const std::vector<std::string> labels{"Skin","Skin",""};
    auto look=resolve_avatar_appearance(prefs,a,labels,1);need(look.whole==AvatarShaderPreset::GameLighting&&look.materials.size()==3,"default game appearance");
    need(set_avatar_preset(prefs,a,AvatarShaderPreset::Matte),"whole preset edit");
    need(set_avatar_material(prefs,a,1,"Skin",AvatarShaderPreset::GameLighting),"material override");
    look=resolve_avatar_appearance(prefs,a,labels,2);
    need(look.generation==2&&look.materials[0]==AvatarShaderPreset::Matte&&look.materials[1]==AvatarShaderPreset::GameLighting&&look.materials[2]==AvatarShaderPreset::Matte,"duplicate labels mixed material identities");
    need(set_avatar_preset(prefs,a,AvatarShaderPreset::GameLighting)&&prefs.profiles[0].overrides.size()==1,"whole preset erased explicit override");
    need(set_avatar_material(prefs,a,0,"Skin",AvatarShaderPreset::Matte),"skin override");
    need(set_avatar_material(prefs,a,1,"Skin",std::nullopt),"inherit edit");
    look=resolve_avatar_appearance(prefs,a,labels,3);need(look.materials[0]==AvatarShaderPreset::Matte&&look.materials[1]==AvatarShaderPreset::GameLighting,"inherit failed");
    auto different=resolve_avatar_appearance(prefs,b,labels,4);need(different.whole==AvatarShaderPreset::GameLighting&&different.materials[0]==AvatarShaderPreset::GameLighting,"changed model bytes inherited another model profile");
    auto renamed=labels;renamed[0]="Other";look=resolve_avatar_appearance(prefs,a,renamed,5);need(look.materials[0]==AvatarShaderPreset::GameLighting,"mismatched material label reused override");
    prefs.selected_path="C:\\avatars\\model-a.vrm";prefs.recent={prefs.selected_path};
    need(write_avatar_preferences(file,prefs),"atomic preference write");auto loaded=read_avatar_preferences(file);
    need(loaded.status==AvatarSettingsStatus::Loaded&&loaded.preferences.selected_path==prefs.selected_path&&loaded.preferences.profiles[0].overrides.size()==1,"profile round trip");
    need(reset_avatar_materials(prefs,a)&&prefs.profiles[0].overrides.empty(),"reset overrides");
    auto invalid=prefs;invalid.profiles[0].whole=AvatarShaderPreset(255);need(!valid_avatar_preferences(invalid)&&!write_avatar_preferences(file,invalid),"unknown preset accepted");
    need(read_avatar_preferences(file).preferences.profiles[0].overrides.size()==1,"invalid write clobbered old preferences");
    invalid=prefs;invalid.recent={"C:\\a.vrm","c:\\A.vrm"};need(!valid_avatar_preferences(invalid),"case alias recent path accepted");
    invalid=prefs;invalid.selected_path="\\\\server\\avatar.vrm";need(!valid_avatar_preferences(invalid),"remote model path accepted");
    invalid=prefs;invalid.profiles[0].overrides={{0,"Skin",AvatarShaderPreset::Matte},{0,"Skin",AvatarShaderPreset::GameLighting}};need(!valid_avatar_preferences(invalid),"duplicate material index accepted");
    const std::string empty=R"({"schemaVersion":1,"selectedModel":"","recentModels":[],"profiles":[]})";
    for(const auto& bad:{std::string("[]"),std::string("{}"),std::string(131073,' '),
        std::string(R"({"schemaVersion":1,"selectedModel":"","recentModels":[],"profiles":[],"profiles":[]})"),
        std::string(R"({"schemaVersion":1,"selectedModel":"","recentModels":[],"profiles":[{"fingerprint":"bad","preset":"matte","overrides":[]}]})")}){
        write(file,bad);loaded=read_avatar_preferences(file);need(loaded.status==AvatarSettingsStatus::Invalid&&loaded.preferences.profiles.empty(),"malformed schema activated a profile");}
    auto future=empty;future.replace(future.find(":1"),2,":2");write(file,future);need(read_avatar_preferences(file).status==AvatarSettingsStatus::UnsupportedVersion,"future version accepted");
    write(file,empty);need(read_avatar_preferences(file).status==AvatarSettingsStatus::Loaded,"empty valid schema rejected");
    need(!set_avatar_material(prefs,a,4096,"Skin",AvatarShaderPreset::Matte),"out-of-range material accepted");
    std::cout<<"PASS avatar preset inheritance, independent material identities, content profiles, schema bounds and atomic persistence\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
