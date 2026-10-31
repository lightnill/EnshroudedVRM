#include "avatar_settings.hpp"
#include "../vendor/tinygltf/json.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <set>
namespace evrm {
namespace {
constexpr std::size_t max_file=128*1024;
bool text(const std::string& value,std::size_t limit)noexcept {
    if(value.size()>limit)return false;
    for(unsigned char c:value)if(c<32||c==127)return false;
    return value.empty()||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0)>0;
}
std::wstring wide(const std::string& value){
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    if(count<=0)return {};
    std::wstring out(std::size_t(count),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),count);return out;
}
bool same_path(const std::string& a,const std::string& b){
    const auto left=wide(a),right=wide(b);
    return CompareStringOrdinal(left.data(),int(left.size()),right.data(),int(right.size()),TRUE)==CSTR_EQUAL;
}
AvatarProfile& profile(AvatarPreferences& prefs,const std::string& fingerprint){
    for(auto& item:prefs.profiles)if(item.fingerprint==fingerprint)return item;
    if(prefs.profiles.size()==16)prefs.profiles.erase(prefs.profiles.begin());
    prefs.profiles.push_back({fingerprint,AvatarShaderPreset::GameLighting,{}});return prefs.profiles.back();
}
struct File {HANDLE value=INVALID_HANDLE_VALUE;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
bool local_file(const std::filesystem::path& path,std::filesystem::path& resolved,bool& missing){
    missing=false;std::array<wchar_t,32768> full{};const auto count=GetFullPathNameW(path.c_str(),DWORD(full.size()),full.data(),nullptr);
    if(count<4||count>=full.size()||full[0]=='\\'||full[1]!=':')return false;
    const wchar_t drive[]={full[0],L':',L'\\',0};const auto type=GetDriveTypeW(drive);
    if(type==DRIVE_REMOTE||type==DRIVE_UNKNOWN||type==DRIVE_NO_ROOT_DIR)return false;
    const std::wstring value(full.data(),count);
    for(std::size_t n=3;n<=value.size();++n){if(n!=value.size()&&value[n]!=L'\\'&&value[n]!=L'/')continue;
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
    unsigned depth=0;bool in_string=false,escape=false;
    for(char c:bytes){if(in_string){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')in_string=false;}
        else if(c=='"')in_string=true;else if(c=='{'||c=='['){if(++depth>8)return false;}
        else if(c=='}'||c==']'){if(!depth)return false;--depth;}}
    return !in_string&&!depth;
}
bool preset(const nlohmann::json& value,AvatarShaderPreset& out){
    if(!value.is_string())return false;
    const auto name=value.get<std::string>();if(name!="game"&&name!="matte")return false;
    out=name=="matte"?AvatarShaderPreset::Matte:AvatarShaderPreset::GameLighting;return true;
}
const char* preset_name(AvatarShaderPreset value){return value==AvatarShaderPreset::Matte?"matte":"game";}
}
bool valid_avatar_fingerprint(const std::string& value)noexcept {
    return value.size()==64&&std::all_of(value.begin(),value.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
bool valid_avatar_path(const std::string& value,bool allow_empty)noexcept {
    if(value.empty())return allow_empty;
    return text(value,4096)&&value.size()>=4&&((value[0]>='a'&&value[0]<='z')||(value[0]>='A'&&value[0]<='Z'))&&value[1]==':'&&(value[2]=='\\'||value[2]=='/');
}
bool valid_avatar_preferences(const AvatarPreferences& prefs)noexcept {try{
    if(!valid_avatar_path(prefs.selected_path,true)||prefs.recent.size()>16||prefs.profiles.size()>16)return false;
    for(std::size_t n=0;n<prefs.recent.size();++n){if(!valid_avatar_path(prefs.recent[n]))return false;
        for(std::size_t j=0;j<n;++j)if(same_path(prefs.recent[j],prefs.recent[n]))return false;}
    std::set<std::string> identities;
    for(const auto& item:prefs.profiles){if(!valid_avatar_fingerprint(item.fingerprint)||!identities.insert(item.fingerprint).second||!valid_avatar_preset(item.whole)||item.overrides.size()>256)return false;
        std::set<std::uint32_t> indices;
        for(const auto& override_:item.overrides)if(override_.index>=4096||!text(override_.name,1024)||!valid_avatar_preset(override_.preset)||!indices.insert(override_.index).second)return false;
    }
    return true;
}catch(...){return false;}}
bool set_avatar_preset(AvatarPreferences& prefs,const std::string& fingerprint,AvatarShaderPreset value){
    if(!valid_avatar_preferences(prefs)||!valid_avatar_fingerprint(fingerprint)||!valid_avatar_preset(value))return false;
    profile(prefs,fingerprint).whole=value;return true;
}
bool set_avatar_material(AvatarPreferences& prefs,const std::string& fingerprint,std::uint32_t index,const std::string& name,std::optional<AvatarShaderPreset> value){
    if(!valid_avatar_preferences(prefs)||!valid_avatar_fingerprint(fingerprint)||index>=4096||!text(name,1024)||(value&&!valid_avatar_preset(*value)))return false;
    auto& overrides=profile(prefs,fingerprint).overrides;
    const auto found=std::find_if(overrides.begin(),overrides.end(),[&](const auto& entry){return entry.index==index;});
    if(!value){if(found!=overrides.end())overrides.erase(found);return true;}
    if(found!=overrides.end())*found={index,name,*value};
    else{if(overrides.size()==256)return false;overrides.push_back({index,name,*value});}
    return true;
}
bool reset_avatar_materials(AvatarPreferences& prefs,const std::string& fingerprint){
    if(!valid_avatar_preferences(prefs)||!valid_avatar_fingerprint(fingerprint))return false;
    profile(prefs,fingerprint).overrides.clear();return true;
}
AvatarAppearance resolve_avatar_appearance(const AvatarPreferences& prefs,const std::string& fingerprint,const std::vector<std::string>& names,std::uint64_t generation){
    AvatarAppearance result;result.fingerprint=fingerprint;result.generation=generation;
    if(names.size()>4096||!valid_avatar_fingerprint(fingerprint)||!valid_avatar_preferences(prefs))return result;
    const auto found=std::find_if(prefs.profiles.begin(),prefs.profiles.end(),[&](const auto& item){return item.fingerprint==fingerprint;});
    if(found!=prefs.profiles.end())result.whole=found->whole;
    result.materials.assign(names.size(),result.whole);
    if(found!=prefs.profiles.end())for(const auto& override_:found->overrides)
        if(override_.index<names.size()&&names[override_.index]==override_.name)result.materials[override_.index]=override_.preset;
    return result;
}
AvatarSettingsLoad read_avatar_preferences(const std::filesystem::path& path)noexcept {
    AvatarSettingsLoad result;try{
        std::filesystem::path resolved;bool missing=false;if(!local_file(path,resolved,missing))return result;
        if(missing){result.status=AvatarSettingsStatus::Missing;return result;}
        File file;file.value=CreateFileW(resolved.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        FILE_ATTRIBUTE_TAG_INFO attributes{};LARGE_INTEGER length{};
        if(file.value==INVALID_HANDLE_VALUE||GetFileType(file.value)!=FILE_TYPE_DISK||!GetFileInformationByHandleEx(file.value,FileAttributeTagInfo,&attributes,sizeof attributes)||
            (attributes.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!GetFileSizeEx(file.value,&length))return result;
        result.status=AvatarSettingsStatus::Invalid;if(length.QuadPart<=0||length.QuadPart>std::int64_t(max_file))return result;
        std::vector<char> bytes(std::size_t(length.QuadPart));DWORD count=0;
        if(!ReadFile(file.value,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size()||!shallow(bytes))return result;
        std::vector<std::set<std::string>> objects;bool duplicate=false;
        const auto json=nlohmann::json::parse(bytes.begin(),bytes.end(),[&](int,nlohmann::json::parse_event_t event,nlohmann::json& value){
            if(event==nlohmann::json::parse_event_t::object_start)objects.emplace_back();
            else if(event==nlohmann::json::parse_event_t::key){if(objects.empty()||!objects.back().insert(value.get<std::string>()).second)duplicate=true;}
            else if(event==nlohmann::json::parse_event_t::object_end&&!objects.empty())objects.pop_back();
            return true;},false);
        if(duplicate||!json.is_object()||!json.contains("schemaVersion")||!json["schemaVersion"].is_number_integer())return result;
        if(json["schemaVersion"]!=1){result.status=AvatarSettingsStatus::UnsupportedVersion;return result;}
        if(json.size()!=4||!json.at("selectedModel").is_string()||!json.at("recentModels").is_array()||!json.at("profiles").is_array()||
            json.at("recentModels").size()>16||json.at("profiles").size()>16)return result;
        AvatarPreferences prefs;prefs.selected_path=json.at("selectedModel").get<std::string>();
        for(const auto& entry:json.at("recentModels")){if(!entry.is_string())return result;prefs.recent.push_back(entry.get<std::string>());}
        for(const auto& entry:json.at("profiles")){
            AvatarProfile item;if(!entry.is_object()||entry.size()!=3||!entry.at("fingerprint").is_string()||!preset(entry.at("preset"),item.whole)||
                !entry.at("overrides").is_array()||entry.at("overrides").size()>256)return result;
            item.fingerprint=entry.at("fingerprint").get<std::string>();
            for(const auto& override_:entry.at("overrides")){
                if(!override_.is_object()||override_.size()!=3||!override_.at("materialIndex").is_number_integer()||override_.at("materialIndex")<0||override_.at("materialIndex")>=4096||!override_.at("materialName").is_string())return result;
                AvatarMaterialOverride value;value.index=override_.at("materialIndex").get<std::uint32_t>();value.name=override_.at("materialName").get<std::string>();
                if(!preset(override_.at("preset"),value.preset))return result;
                item.overrides.push_back(std::move(value));
            }
            prefs.profiles.push_back(std::move(item));
        }
        if(!valid_avatar_preferences(prefs))return result;
        result.preferences=std::move(prefs);result.status=AvatarSettingsStatus::Loaded;return result;
    }catch(...){return result;}
}
bool write_avatar_preferences(const std::filesystem::path& path,const AvatarPreferences& prefs)noexcept {
    std::filesystem::path temporary;bool own=false;try{
        if(!valid_avatar_preferences(prefs))return false;
        std::filesystem::path resolved;bool missing=false;if(!local_file(path,resolved,missing))return false;
        nlohmann::json profiles=nlohmann::json::array();
        for(const auto& item:prefs.profiles){nlohmann::json overrides=nlohmann::json::array();
            for(const auto& value:item.overrides)overrides.push_back({{"materialIndex",value.index},{"materialName",value.name},{"preset",preset_name(value.preset)}});
            profiles.push_back({{"fingerprint",item.fingerprint},{"preset",preset_name(item.whole)},{"overrides",std::move(overrides)}});}
        const nlohmann::json json={{"schemaVersion",1},{"selectedModel",prefs.selected_path},{"recentModels",prefs.recent},{"profiles",std::move(profiles)}};
        const auto bytes=json.dump(2)+'\n';if(bytes.size()>max_file)return false;
        static std::atomic<unsigned> sequence{0};File file;
        for(unsigned n=0;n<8;++n){temporary=resolved;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence.fetch_add(1));
            file.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(file.value!=INVALID_HANDLE_VALUE){own=true;break;}if(GetLastError()!=ERROR_FILE_EXISTS)return false;}
        if(!own)return false;
        DWORD written=0;const bool complete=WriteFile(file.value,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file.value);
        CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;
        if(!complete||!local_file(path,resolved,missing)||!MoveFileExW(temporary.c_str(),resolved.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DeleteFileW(temporary.c_str());return false;}
        return true;
    }catch(...){if(own)DeleteFileW(temporary.c_str());return false;}
}
}
