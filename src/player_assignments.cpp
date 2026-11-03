#include "player_assignments.hpp"
#include "avatar_settings.hpp"
#include "../vendor/tinygltf/json.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <set>
namespace evrm { namespace {
constexpr std::size_t maximum_file=4*1024*1024,maximum_entries=512;
bool secret_valid(const std::array<std::uint8_t,32>& value)noexcept{return std::any_of(value.begin(),value.end(),[](auto byte){return byte!=0;});}
std::string hexadecimal(const std::array<std::uint8_t,32>& value){
    constexpr char digits[]="0123456789abcdef";std::string result;result.reserve(64);
    for(auto byte:value){result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}return result;
}
bool valid_entry(const PlayerAssignment& entry)noexcept {
    return valid_avatar_fingerprint(entry.token)&&valid_avatar_path(entry.path,!entry.replacement_enabled);
}
struct Algorithm {BCRYPT_ALG_HANDLE handle=nullptr;~Algorithm(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}};
struct Hash {BCRYPT_HASH_HANDLE handle=nullptr;~Hash(){if(handle)BCryptDestroyHash(handle);}};
struct File {HANDLE handle=INVALID_HANDLE_VALUE;~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
bool local_file(const std::filesystem::path& path,std::filesystem::path& resolved,bool& missing){
    missing=false;std::array<wchar_t,32768> full{};const auto count=GetFullPathNameW(path.c_str(),DWORD(full.size()),full.data(),nullptr);
    if(count<4||count>=full.size()||full[0]=='\\'||full[1]!=':')return false;
    const wchar_t drive[]={full[0],L':',L'\\',0};const auto type=GetDriveTypeW(drive);
    if(type==DRIVE_REMOTE||type==DRIVE_UNKNOWN||type==DRIVE_NO_ROOT_DIR)return false;
    const std::wstring value(full.data(),count);
    for(std::size_t n=3;n<=value.size();++n){
        if(n!=value.size()&&value[n]!=L'\\'&&value[n]!=L'/')continue;
        const auto attributes=GetFileAttributesW(value.substr(0,n).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();
            if(n==value.size()&&(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)){missing=true;break;}return false;}
        if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return false;
        if(n!=value.size()&&!(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
        if(n==value.size()&&(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    }
    resolved=value;return true;
}
bool shallow(const std::vector<char>& bytes){
    unsigned depth=0;bool string=false,escape=false;
    for(char byte:bytes){
        if(string){if(escape)escape=false;else if(byte=='\\')escape=true;else if(byte=='"')string=false;}
        else if(byte=='"')string=true;
        else if(byte=='{'||byte=='['){if(++depth>8)return false;}
        else if(byte=='}'||byte==']'){if(!depth)return false;--depth;}
    }
    return !string&&!depth;
}
bool decode_secret(const std::string& text,std::array<std::uint8_t,32>& result){
    if(!valid_avatar_fingerprint(text))return false;
    const auto digit=[](char c){return unsigned(c<='9'?c-'0':c-'a'+10);};
    for(std::size_t n=0;n<32;++n)result[n]=static_cast<std::uint8_t>((digit(text[n*2])<<4)|digit(text[n*2+1]));
    return secret_valid(result);
}
}
bool make_player_assignment_secret(std::array<std::uint8_t,32>& value)noexcept {
    value={};if(BCryptGenRandom(nullptr,value.data(),ULONG(value.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0||!secret_valid(value)){value={};return false;}return true;
}
std::string player_assignment_token(const std::array<std::uint8_t,32>& secret,std::uint64_t account)noexcept {
    try{
        if(!account||!secret_valid(secret))return {};
        Algorithm algorithm;if(BCryptOpenAlgorithmProvider(&algorithm.handle,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0)return {};
        DWORD object_bytes=0,written=0;
        if(BCryptGetProperty(algorithm.handle,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_bytes),sizeof object_bytes,&written,0)<0||written!=sizeof object_bytes||!object_bytes||object_bytes>65536)return {};
        std::vector<std::uint8_t> object(object_bytes);Hash hash;
        if(BCryptCreateHash(algorithm.handle,&hash.handle,object.data(),ULONG(object.size()),const_cast<PUCHAR>(secret.data()),ULONG(secret.size()),0)<0)return {};
        // Include the domain's trailing NUL and a fixed little-endian value.
        constexpr char domain[]="EnshroudedVRM/player-assignment/v1";
        std::array<std::uint8_t,8> encoded{};for(unsigned n=0;n<8;++n)encoded[n]=static_cast<std::uint8_t>(account>>(n*8));
        if(BCryptHashData(hash.handle,reinterpret_cast<PUCHAR>(const_cast<char*>(domain)),sizeof domain,0)<0||BCryptHashData(hash.handle,encoded.data(),ULONG(encoded.size()),0)<0)return {};
        std::array<std::uint8_t,32> digest{};if(BCryptFinishHash(hash.handle,digest.data(),ULONG(digest.size()),0)<0)return {};
        return hexadecimal(digest);
    }catch(...){return {};}
}
bool valid_player_assignments(const PlayerAssignments& value)noexcept {
    try{
        if(!secret_valid(value.secret)||value.entries.size()>maximum_entries)return false;
        std::set<std::string> seen;for(const auto& entry:value.entries)if(!valid_entry(entry)||!seen.insert(entry.token).second)return false;
        return true;
    }catch(...){return false;}
}
bool set_player_assignment(PlayerAssignments& value,const PlayerAssignment& entry)noexcept {
    try{
        if(!valid_player_assignments(value)||!valid_entry(entry))return false;
        for(auto& current:value.entries)if(current.token==entry.token){current=entry;return true;}
        if(value.entries.size()>=maximum_entries)return false;
        value.entries.push_back(entry);return true;
    }catch(...){return false;}
}
PlayerAssignmentsLoad read_player_assignments(const std::filesystem::path& path)noexcept {
    PlayerAssignmentsLoad result;
    try{
        std::filesystem::path resolved;bool missing=false;if(!local_file(path,resolved,missing))return result;
        if(missing){result.status=PlayerAssignmentsStatus::Missing;return result;}
        File file;file.handle=CreateFileW(resolved.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        FILE_ATTRIBUTE_TAG_INFO attributes{};LARGE_INTEGER length{};
        if(file.handle==INVALID_HANDLE_VALUE||GetFileType(file.handle)!=FILE_TYPE_DISK||!GetFileInformationByHandleEx(file.handle,FileAttributeTagInfo,&attributes,sizeof attributes)||
           (attributes.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!GetFileSizeEx(file.handle,&length))return result;
        result.status=PlayerAssignmentsStatus::Invalid;if(length.QuadPart<=0||length.QuadPart>std::int64_t(maximum_file))return result;
        std::vector<char> bytes(std::size_t(length.QuadPart));DWORD count=0;
        if(!ReadFile(file.handle,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size()||!shallow(bytes))return result;
        std::vector<std::set<std::string>> objects;bool duplicate=false;
        const auto json=nlohmann::json::parse(bytes.begin(),bytes.end(),[&](int,nlohmann::json::parse_event_t event,nlohmann::json& value){
            if(event==nlohmann::json::parse_event_t::object_start)objects.emplace_back();
            else if(event==nlohmann::json::parse_event_t::key){if(objects.empty()||!objects.back().insert(value.get<std::string>()).second)duplicate=true;}
            else if(event==nlohmann::json::parse_event_t::object_end&&!objects.empty())objects.pop_back();
            return true;
        },false);
        if(duplicate||!json.is_object()||!json.contains("schemaVersion")||!json["schemaVersion"].is_number_integer())return result;
        if(json["schemaVersion"].get<std::int64_t>()!=1){result.status=PlayerAssignmentsStatus::UnsupportedVersion;return result;}
        if(json.size()!=3||!json.at("secret").is_string()||!json.at("entries").is_array()||json.at("entries").size()>maximum_entries)return result;
        PlayerAssignments parsed;if(!decode_secret(json.at("secret").get<std::string>(),parsed.secret))return result;
        for(const auto& entry:json.at("entries")){
            if(!entry.is_object()||entry.size()!=3||!entry.at("token").is_string()||!entry.at("model").is_string()||!entry.at("enabled").is_boolean())return result;
            parsed.entries.push_back({entry.at("token").get<std::string>(),entry.at("model").get<std::string>(),entry.at("enabled").get<bool>()});
        }
        if(!valid_player_assignments(parsed))return result;
        result.preferences=std::move(parsed);result.status=PlayerAssignmentsStatus::Loaded;return result;
    }catch(...){return result;}
}
static bool commit_player_assignments(const std::filesystem::path& path,const PlayerAssignments& value,bool replace)noexcept {
    std::filesystem::path temporary;bool own=false;
    try{
        if(!valid_player_assignments(value))return false;
        const auto previous=read_player_assignments(path);
        if(previous.status!=PlayerAssignmentsStatus::Missing&&(!replace||previous.status!=PlayerAssignmentsStatus::Loaded||previous.preferences.secret!=value.secret))return false;
        std::filesystem::path resolved;bool missing=false;if(!local_file(path,resolved,missing))return false;
        auto entries=nlohmann::json::array();for(const auto& entry:value.entries)entries.push_back({{"token",entry.token},{"model",entry.path},{"enabled",entry.replacement_enabled}});
        const nlohmann::json json={{"schemaVersion",1},{"secret",hexadecimal(value.secret)},{"entries",std::move(entries)}};
        const auto bytes=json.dump(2)+'\n';if(bytes.size()>maximum_file)return false;
        static std::atomic<unsigned> sequence{0};File file;
        for(unsigned n=0;n<8;++n){
            temporary=resolved;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence.fetch_add(1));
            file.handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(file.handle!=INVALID_HANDLE_VALUE){own=true;break;}if(GetLastError()!=ERROR_FILE_EXISTS)return false;
        }
        if(!own)return false;
        DWORD count=0;const bool complete=WriteFile(file.handle,bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(file.handle);
        CloseHandle(file.handle);file.handle=INVALID_HANDLE_VALUE;
        if(!complete||!local_file(path,resolved,missing)||!MoveFileExW(temporary.c_str(),resolved.c_str(),(replace?MOVEFILE_REPLACE_EXISTING:0)|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());return false;}
        return true;
    }catch(...){if(own)DeleteFileW(temporary.c_str());return false;}
}
bool create_player_assignments(const std::filesystem::path& path,const PlayerAssignments& value)noexcept{return commit_player_assignments(path,value,false);}
bool write_player_assignments(const std::filesystem::path& path,const PlayerAssignments& value)noexcept{return commit_player_assignments(path,value,true);}
}
