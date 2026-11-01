#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/standalone_startup.hpp"
#include "../src/standalone_settings.hpp"
#include "../src/standalone_package_members.hpp"
#include "../src/native_owner_profile.hpp"
#include "../src/sha256.hpp"
#include "../src/avatar_settings.hpp"
#include "../src/mod_settings.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <thread>

using namespace evrm;
using Json=nlohmann::json;
namespace fs=std::filesystem;
namespace evrm {RuntimeStartupContext diagnostic_fixture;const RuntimeStartupContext& runtime_startup_context()noexcept{return diagnostic_fixture;}}
static int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "startup failed line " << __LINE__ << ": " #x "\n"; return 1; } } while(false)
static void put(const fs::path& path,const std::string& data) { fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<data; }
static std::string bytes(const fs::path& path) { std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}}; }
static std::string hash(const std::string& data) { const auto digest=detail::sha256(data.data(),data.size());const char hex[]="0123456789abcdef";std::string out;for(const auto byte:digest){out+=hex[byte>>4];out+=hex[byte&15];}return out; }
static NativeOwnerImage proof() { NativeOwnerImage image;image.base=0x140000000;image.size=native_owner_profile::image_size;image.timestamp=native_owner_profile::image_timestamp;std::copy(std::begin(native_owner_profile::image_sha256),std::end(native_owner_profile::image_sha256),image.sha256.begin());return image; }
static RuntimeStartupInput package(const fs::path& root) {
    RuntimeStartupInput input;input.executable_path=root/L"game/enshrouded.exe";
    input.module_path=root/L"game/EnshroudedVRM/Runtime/enshrouded_vrm_runtime.dll";
    input.user_data_directory=root/L"private";
    const auto game=input.executable_path.parent_path();
    Json files=Json::array();
    for(const auto member:standalone_package_members) { const std::string content=std::string("owned synthetic ")+member;put(game/fs::u8path(member),content);files.push_back({{"path",member},{"sha256",hash(content)}}); }
    const auto image=proof();std::string digest;const char hex[]="0123456789abcdef";for(auto byte:image.sha256){digest+=hex[byte>>4];digest+=hex[byte&15];}
    const Json manifest={{"schema","enshrouded-vrm-standalone-v1"},{"version","standalone-dev"},{"publication_status","private-development"},{"game_sha256",digest},{"files",files}};
    put(game/L"EnshroudedVRM/release-manifest.json",manifest.dump(2));put(input.executable_path,"synthetic disk path; attestation is an explicit local fixture input");
    return input;
}
static int diagnostic_tests(const fs::path& root,const std::wstring& mode) {
    auto input=package(root);auto trusted=proof();
    const auto state=input.user_data_directory/L"EnshroudedVRM/State",log=state/L"runtime.log";
    const auto source=input.module_path.parent_path().parent_path()/L"State/mod-settings.json";
    ModSettings settings;settings.voice_enabled=true;fs::create_directories(source.parent_path());CHECK(write_mod_settings(source,settings));
    const auto original_settings=bytes(source);const auto poison=root/L"inherited.log";put(poison,"untouched");
    if(mode==L"diagnostic-unsupported")trusted.sha256[0]^=1;
    else if(mode==L"diagnostic-other")input.executable_path=input.executable_path.parent_path()/L"assets.exe";
    const auto member=input.module_path.parent_path()/L"shaders/avatar.frag.spv";
    if(mode==L"diagnostic-mutated")put(member,"changed");else fs::remove(member);
    diagnostic_fixture=evaluate_runtime_startup(input,&trusted);
    CHECK(!diagnostic_fixture.ready());CHECK(!fs::exists(input.user_data_directory));
    const bool eligible=mode!=L"diagnostic-unsupported" && mode!=L"diagnostic-other";
    if(eligible && mode!=L"diagnostic-readonly")put(log,"stale previous session");
    if(mode==L"diagnostic-readonly")put(input.user_data_directory,"not a directory");
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",poison.c_str());
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_DIR",root.c_str());
    initialize_runtime_startup_environment();initialize_runtime_startup_environment();
    CHECK(bytes(poison)=="untouched");CHECK(bytes(source)==original_settings);
    CHECK(!fs::exists(state/L"mod-settings.json"));CHECK(!fs::exists(state/L"avatar-settings.json"));
    CHECK(!fs::exists(state/L"capture-cache"));CHECK(GetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_DIR",nullptr,0)==0);
    if(eligible && mode!=L"diagnostic-readonly") {
        const auto fresh=bytes(log);CHECK(fresh.find("standalone.startup status=declined")!=fresh.npos);
        CHECK(fresh.find("reason="+diagnostic_fixture.reason)!=fresh.npos);CHECK(fresh.find("stale previous session")==fresh.npos);CHECK(fresh.size()<8192);
        const auto history=state/L"startup-history";CHECK(fs::exists(history));CHECK(std::distance(fs::directory_iterator(history),fs::directory_iterator{})==1);
        CHECK(bytes(fs::directory_iterator(history)->path()).find("Previous session tail")!=std::string::npos);
    }else {CHECK(!fs::exists(log));CHECK(GetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",nullptr,0)==0);}
    if(!eligible)CHECK(!fs::exists(input.user_data_directory));
    if(mode==L"diagnostic-readonly")CHECK(bytes(input.user_data_directory)=="not a directory");
    std::cout<<"declined diagnostic eligibility, fresh reason, bounded history, no import/capture and untrusted-owner isolation passed\n";return 0;
}
static int context_tests(const fs::path& root) {
    auto input=package(root);const auto trusted=proof();
    CHECK(std::size(standalone_package_members)>=35);
    auto value=evaluate_runtime_startup(input,&trusted);CHECK(value.ready());CHECK(value.owner.sha256==trusted.sha256);
    CHECK(value.state_directory==input.user_data_directory/L"EnshroudedVRM/State");
    CHECK(value.avatars_directory==input.executable_path.parent_path()/L"EnshroudedVRM/Avatars");
    CHECK(value.state_writable);CHECK(value.verified_files==std::size(standalone_package_members));CHECK(value.hashed_bytes>0);
    CHECK(!fs::exists(value.state_directory/L"mod-settings.json"));
    CHECK(fs::is_empty(value.state_directory));
    const auto locked_member=value.mod_directory/L"Compatibility/profile.json";
    const auto attempt=CreateFileW(locked_member.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    CHECK(attempt==INVALID_HANDLE_VALUE);CHECK(!DeleteFileW(locked_member.c_str()));
    value={};
    CHECK(!evaluate_runtime_startup(input,nullptr).ready());
    auto unsupported=trusted;unsupported.sha256[0]^=1;CHECK(!evaluate_runtime_startup(input,&unsupported).ready());
    unsupported=trusted;unsupported.base=0;CHECK(!evaluate_runtime_startup(input,&unsupported).ready());
    auto changed=input;changed.executable_path=changed.executable_path.parent_path()/L"assets.exe";
    const auto untouched=root/L"dormant-private";changed.user_data_directory=untouched;
    CHECK(evaluate_runtime_startup(changed,&trusted).status==RuntimeStartupStatus::Dormant);CHECK(!fs::exists(untouched));
    changed=input;changed.disabled=true;changed.user_data_directory=untouched;CHECK(evaluate_runtime_startup(changed,&trusted).status==RuntimeStartupStatus::Dormant);CHECK(!fs::exists(untouched));
    changed=input;changed.module_path=root/L"elsewhere/enshrouded_vrm_runtime.dll";CHECK(!evaluate_runtime_startup(changed,&trusted).ready());
    const auto mod=input.module_path.parent_path().parent_path(),manifest=mod/L"release-manifest.json";
    const auto original=bytes(manifest);auto json=Json::parse(original);
    const auto member=mod/L"Compatibility/profile.json";const auto original_member=bytes(member);
    put(member,"mutated");CHECK(!evaluate_runtime_startup(input,&trusted).ready());put(member,original_member);
    fs::remove(member);CHECK(!evaluate_runtime_startup(input,&trusted).ready());put(member,original_member);
    const auto link=root/L"linked-profile.json";fs::create_hard_link(member,link);CHECK(!evaluate_runtime_startup(input,&trusted).ready());fs::remove(link);
    for(const auto bad_path:{"../outside.json","EnshroudedVRM/State/private.json","EnshroudedVRM/Runtime/../bad.json","enshrouded.exe","EnshroudedVRM/Avatars/private.vrm","EnshroudedVRM/Runtime/runtime.dll:stream"}) {
        auto bad=json;bad["files"][0]["path"]=bad_path;put(manifest,bad.dump());CHECK(!evaluate_runtime_startup(input,&trusted).ready());
    }
    auto bad=json;bad["files"].push_back(bad["files"][0]);put(manifest,bad.dump());CHECK(!evaluate_runtime_startup(input,&trusted).ready());
    bad=json;bad["game_sha256"]=std::string(64,'0');put(manifest,bad.dump());CHECK(!evaluate_runtime_startup(input,&trusted).ready());
    bad=json;bad["files"].erase(bad["files"].begin());put(manifest,bad.dump());CHECK(!evaluate_runtime_startup(input,&trusted).ready());
    put(manifest,"{\"schema\":\"a\",\"schema\":\"b\"}");CHECK(!evaluate_runtime_startup(input,&trusted).ready());
    put(manifest,std::string(256*1024,'x'));CHECK(!evaluate_runtime_startup(input,&trusted).ready());
    put(manifest,original);CHECK(evaluate_runtime_startup(input,&trusted).ready());
    const auto import=mod/L"State";fs::create_directory(import);ModSettings preferences;preferences.strength=93;
    CHECK(write_mod_settings(import/L"mod-settings.json",preferences));
    value=evaluate_runtime_startup(input,&trusted);CHECK(value.ready());CHECK(value.imports.mod==SettingsImportStatus::Imported);
    const auto restored=read_mod_settings(value.state_directory/L"mod-settings.json");CHECK(restored.status==ModSettingsStatus::Loaded && restored.settings.strength==93 && !restored.settings.voice_enabled);
    std::cout<<checks<<" native context/path/hash/attestation/dormancy checks passed\n";return 0;
}
static int migration_tests(const fs::path& root) {
    const auto source=root/L"import",destination=root/L"state";fs::create_directories(source);fs::create_directories(destination);
    AvatarPreferences avatar;avatar.selected_path="C:\\owned\\avatar.vrm";avatar.recent.push_back(avatar.selected_path);
    avatar.profiles.push_back({std::string(64,'a'),AvatarShaderPreset::Matte,{{1,"skin",AvatarShaderPreset::Matte}}});
    ModSettings settings;settings.voice_enabled=true;settings.strength=92;
    CHECK(write_avatar_preferences(source/L"avatar-settings.json",avatar));CHECK(write_mod_settings(source/L"mod-settings.json",settings));
    const auto original_avatar=bytes(source/L"avatar-settings.json"),original_mod=bytes(source/L"mod-settings.json");
    auto result=migrate_standalone_settings(source,destination);
    CHECK(result.avatar==SettingsImportStatus::Imported && result.mod==SettingsImportStatus::Imported);
    CHECK(read_avatar_preferences(destination/L"avatar-settings.json").preferences.profiles[0].whole==AvatarShaderPreset::Matte);
    CHECK(read_mod_settings(destination/L"mod-settings.json").settings.strength==92);CHECK(fs::exists(source/L"avatar-settings.json"));
    CHECK(bytes(source/L"avatar-settings.json")==original_avatar && bytes(source/L"mod-settings.json")==original_mod);
    settings.strength=20;CHECK(write_mod_settings(source/L"mod-settings.json",settings));
    result=migrate_standalone_settings(source,destination);CHECK(result.mod==SettingsImportStatus::Existing);
    CHECK(read_mod_settings(destination/L"mod-settings.json").settings.strength==92);
    fs::remove(destination/L"mod-settings.json");put(source/L"mod-settings.json","invalid");
    result=migrate_standalone_settings(source,destination);CHECK(result.avatar==SettingsImportStatus::Existing && result.mod==SettingsImportStatus::Declined);CHECK(!fs::exists(destination/L"mod-settings.json"));
    CHECK(write_mod_settings(source/L"mod-settings.json",settings));result=migrate_standalone_settings(source,destination);CHECK(result.mod==SettingsImportStatus::Imported);
    const auto backup=root/L"mod-link.json";fs::create_hard_link(source/L"mod-settings.json",backup);fs::remove(destination/L"mod-settings.json");
    result=migrate_standalone_settings(source,destination);CHECK(result.mod==SettingsImportStatus::Declined);fs::remove(backup);
    for(int index=0;index<8;++index) {
        const auto target=destination/(L"race-"+std::to_wstring(index)+L".json");bool first=false,second=false;
        ModSettings other=settings;other.strength=99;
        std::thread one([&]{first=create_mod_settings(target,settings);}),two([&]{second=create_mod_settings(target,other);});one.join();two.join();
        CHECK(first!=second);const auto loaded=read_mod_settings(target);CHECK(loaded.status==ModSettingsStatus::Loaded);CHECK(loaded.settings.strength==(first?20u:99u));
    }
    const auto destination_avatar=destination/L"avatar-settings.json";const auto before=bytes(destination_avatar);
    CHECK(!create_avatar_preferences(destination_avatar,AvatarPreferences{}));CHECK(bytes(destination_avatar)==before);
    std::cout<<checks<<" validated/partial/linked import and atomic existing-destination checks passed\n";return 0;
}
static int readonly_tests(const fs::path& root) {
    auto input=package(root);const auto trusted=proof();const auto blocker=input.user_data_directory/L"EnshroudedVRM";put(blocker,"file prevents private directory creation");
    auto result=evaluate_runtime_startup(input,&trusted);CHECK(result.ready());CHECK(!result.state_writable);CHECK(bytes(blocker)=="file prevents private directory creation");
    result={};input.user_data_directory.clear();result=evaluate_runtime_startup(input,&trusted);CHECK(result.ready());CHECK(!result.state_writable);CHECK(result.state_directory.empty());
    CHECK(!fs::exists(root/L"game/State"));std::cout<<checks<<" unavailable private-storage/default-only checks passed\n";return 0;
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(GetErrorMode()|0x8003);if(argc!=3)return 2;
    try {const auto mode=std::wstring(argv[2]);if(mode==L"context")return context_tests(argv[1]);if(mode==L"migration")return migration_tests(argv[1]);if(mode==L"readonly")return readonly_tests(argv[1]);if(mode.rfind(L"diagnostic-",0)==0)return diagnostic_tests(argv[1],mode);}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 3;}
    return 4;
}
