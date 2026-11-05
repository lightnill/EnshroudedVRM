#include "standalone_startup.hpp"
#include "standalone_files.hpp"
#include "standalone_package_members.hpp"
#include "native_owner_profile.hpp"
#include "sha256.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>

namespace evrm {
struct RuntimePackageLease {std::vector<std::unique_ptr<detail::StandaloneFile>> files;};
namespace {
struct Declined {const char* reason;};
void need(bool good,const char* reason) {if(!good)throw Declined{reason};}
std::string hex(const detail::Sha256Digest& digest) {
    const char alphabet[]="0123456789abcdef";std::string output;output.reserve(64);
    for(const auto byte:digest){output+=alphabet[byte>>4];output+=alphabet[byte&15];}return output;
}
bool shallow(const std::string& data) {
    unsigned depth=0;bool string=false,escape=false;
    for(const char c:data) {
        if(string){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')string=false;}
        else if(c=='"')string=true;else if(c=='{' || c=='['){if(++depth>8)return false;}
        else if(c=='}' || c==']'){if(!depth)return false;--depth;}
    }
    return !string && !depth;
}
nlohmann::json manifest_json(const std::string& data) {
    need(shallow(data),"manifest-invalid");std::vector<std::set<std::string>> keys;bool duplicate=false;
    const auto parsed=nlohmann::json::parse(data,[&](int,nlohmann::json::parse_event_t event,nlohmann::json& value){
        if(event==nlohmann::json::parse_event_t::object_start)keys.emplace_back();
        else if(event==nlohmann::json::parse_event_t::key){if(keys.empty() || !keys.back().insert(value.get<std::string>()).second)duplicate=true;}
        else if(event==nlohmann::json::parse_event_t::object_end && !keys.empty())keys.pop_back();
        return true;
    },false);
    need(!duplicate && parsed.is_object(),"manifest-invalid");return parsed;
}
bool prepare_state(const std::filesystem::path& directory) {
    if(!detail::local_standalone_path(directory,true))return false;
    std::error_code error;std::filesystem::create_directories(directory,error);if(error || !detail::local_standalone_path(directory))return false;
    detail::StandaloneFile held(directory,true);
    const auto probe=directory/(L".write-check-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto file=CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    CloseHandle(file);return true;
}
}
RuntimeStartupContext evaluate_runtime_startup(const RuntimeStartupInput& input,const NativeOwnerImage* owner)noexcept {
    RuntimeStartupContext result;const auto begin=std::chrono::steady_clock::now();
    try {
        if(input.disabled){result.reason="disabled";return result;}
        if(!detail::same_standalone_path(input.executable_path.filename(),L"enshrouded.exe")){result.reason="other-process";return result;}
        result.status=RuntimeStartupStatus::Declined;
        need(owner && owner->base>=0x10000 && !(owner->base&0xffff) && owner->base<0x800000000000ULL &&
             owner->size==native_owner_profile::image_size && owner->timestamp==native_owner_profile::image_timestamp &&
             std::equal(owner->sha256.begin(),owner->sha256.end(),std::begin(native_owner_profile::image_sha256)),"unsupported-game");
        result.diagnostic_owner_verified=true;
        if(!input.user_data_directory.empty()) {
            result.state_directory=input.user_data_directory/L"EnshroudedVRM/State";
            result.log_path=result.state_directory/L"runtime.log";
        }
        result.game_directory=input.executable_path.parent_path();result.mod_directory=result.game_directory/L"EnshroudedVRM";
        result.runtime_directory=result.mod_directory/L"Runtime";
        need(detail::same_standalone_path(input.module_path,result.runtime_directory/L"enshrouded_vrm_runtime.dll"),"package-layout");
        auto lease=std::make_shared<RuntimePackageLease>();std::set<std::filesystem::path> directories;
        const auto lock_directory=[&](const auto& path){if(directories.insert(path).second)lease->files.push_back(std::make_unique<detail::StandaloneFile>(path,true));};
        lock_directory(result.game_directory);lock_directory(result.mod_directory);
        auto manifest=std::make_unique<detail::StandaloneFile>(result.mod_directory/L"release-manifest.json",false,detail::StandaloneReadPolicy::ImmutablePackage);
        const auto json=manifest_json(manifest->read(64*1024));lease->files.push_back(std::move(manifest));
        need(json.size()==5 && json.value("schema","")=="enshrouded-vrm-standalone-v1" && json.at("game_sha256").is_string() &&
             json.at("game_sha256").get<std::string>()==hex(owner->sha256),"manifest-build");
        need(json.at("version").is_string() && json.at("publication_status").is_string(),"manifest-invalid");
        result.version=json.at("version").get<std::string>();
        need(!result.version.empty() && result.version.size()<=64 && std::all_of(result.version.begin(),result.version.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_';}),"manifest-version");
        const auto publication=json.at("publication_status").get<std::string>();need(publication=="private-development" || publication=="preview" || publication=="release","manifest-invalid");
        const auto& files=json.at("files");need(files.is_array() && files.size()==std::size(standalone_package_members),"manifest-members");
        std::set<std::string> seen;
        for(const auto& entry:files) {
            need(entry.is_object() && entry.size()==2 && entry.at("path").is_string() && entry.at("sha256").is_string(),"manifest-members");
            const auto name=entry.at("path").get<std::string>(),digest=entry.at("sha256").get<std::string>();
            need(std::find(std::begin(standalone_package_members),std::end(standalone_package_members),name)!=std::end(standalone_package_members) && seen.insert(name).second,"manifest-members");
            need(digest.size()==64 && std::all_of(digest.begin(),digest.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"manifest-digest");
            const auto path=result.game_directory/std::filesystem::u8path(name);
            for(auto directory=path.parent_path();!detail::same_standalone_path(directory,result.game_directory);directory=directory.parent_path())lock_directory(directory);
            auto file=std::make_unique<detail::StandaloneFile>(path,false,detail::StandaloneReadPolicy::ImmutablePackage);const auto bytes=file->read(64*1024*1024);
            need(result.hashed_bytes<=128*1024*1024-bytes.size(),"package-size");
            need(hex(detail::sha256(bytes.data(),bytes.size()))==digest,"member-hash-mismatch");
            result.hashed_bytes+=bytes.size();++result.verified_files;lease->files.push_back(std::move(file));
        }
        result.owner=*owner;result.profile_path=result.mod_directory/L"Compatibility/profile.json";
        result.catalog_path=result.mod_directory/L"Compatibility/catalog.json";result.avatars_directory=result.mod_directory/L"Avatars";
        result.lease=std::move(lease);
        if(!result.state_directory.empty()) {
            try {result.state_writable=prepare_state(result.state_directory);}catch(...){result.state_writable=false;}
            if(result.state_writable)result.imports=migrate_standalone_settings(result.mod_directory/L"State",result.state_directory);
            result.capture_directory=result.state_directory/L"capture-cache";
        }
        result.status=RuntimeStartupStatus::Ready;result.reason=result.state_writable?"ready":"ready-settings-unavailable";
    }catch(const Declined& error){result.status=RuntimeStartupStatus::Declined;result.reason=error.reason;result.lease.reset();result.owner={};}
    catch(...){result.status=RuntimeStartupStatus::Declined;result.reason="startup-unavailable";result.lease.reset();result.owner={};}
    result.validation_microseconds=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
    return result;
}
}
