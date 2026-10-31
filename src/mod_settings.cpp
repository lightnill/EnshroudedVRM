#include "mod_settings.hpp"
#include "../vendor/tinygltf/json.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <set>
#include <string>
namespace evrm {
bool valid_mod_settings(const ModSettings& s)noexcept {
    const auto& id=microphone_device_id(s);
    if(id.size()>1024)return false;
    for(unsigned char c:id)if(c<32||c==127)return false;
    if(!id.empty()&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,id.data(),int(id.size()),nullptr,0))return false;
    return s.strength<=100&&s.sensitivity<=100&&s.smoothing_ms>=20&&s.smoothing_ms<=140&&
        (s.hotkey>=0x70&&s.hotkey<=0x7b)&&(s.voice_source==VoiceInputSource::Game||s.voice_source==VoiceInputSource::Microphone);
}
std::uint32_t pack_mod_settings(const ModSettings& s)noexcept {
    if(!valid_mod_settings(s))return pack_mod_settings({});
    return std::uint32_t(s.voice_enabled)|(s.strength<<1)|(s.sensitivity<<8)|(s.smoothing_ms<<15)|(s.hotkey<<23)|
        (std::uint32_t(s.voice_source==VoiceInputSource::Microphone)<<31);
}
ModSettings unpack_mod_settings(std::uint32_t w)noexcept {
    ModSettings s{bool(w&1),(w>>1)&127,(w>>8)&127,(w>>15)&255,(w>>23)&255};s.voice_source=w>>31?VoiceInputSource::Microphone:VoiceInputSource::Game;
    return valid_mod_settings(s)?s:ModSettings{};
}
bool ModSettingsMailbox::update(const ModSettings& s)noexcept {
    if(!valid_mod_settings(s))return false;
    try{auto old=ticket();for(;;){if(old&&old->generation==UINT64_MAX)return false;
        TicketPtr next=std::make_shared<const Ticket>(Ticket{s,old?old->generation+1:1});
        if(std::atomic_compare_exchange_weak_explicit(&word,&old,next,std::memory_order_acq_rel,std::memory_order_acquire))return true;}}
    catch(...){return false;}
}
bool ModSettingsMailbox::accept_loaded(const ModSettings& s)noexcept {
    if(!valid_mod_settings(s))return false;
    try{auto expected=ticket();if(expected)return false;TicketPtr next=std::make_shared<const Ticket>(Ticket{s,1});
        return std::atomic_compare_exchange_strong_explicit(&word,&expected,next,std::memory_order_acq_rel,std::memory_order_acquire);}
    catch(...){return false;}
}
namespace {
struct File {HANDLE h=INVALID_HANDLE_VALUE;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
bool local_path(const std::filesystem::path& path,std::filesystem::path& out,bool& missing){
    missing=false;std::array<wchar_t,32768> full{};const auto count=GetFullPathNameW(path.c_str(),DWORD(full.size()),full.data(),nullptr);
    if(!count||count>=full.size()||full[0]=='\\'||full[1]!=':'||count<4)return false;
    const wchar_t root[]={full[0],L':',L'\\',0};const auto drive=GetDriveTypeW(root);
    if(drive==DRIVE_REMOTE||drive==DRIVE_UNKNOWN||drive==DRIVE_NO_ROOT_DIR)return false;
    const std::wstring text(full.data(),count);
    for(std::size_t n=3;n<=text.size();++n){if(n!=text.size()&&text[n]!=L'\\'&&text[n]!=L'/')continue;
        const auto attributes=GetFileAttributesW(text.substr(0,n).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();
            if(n==text.size()&&(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)){missing=true;break;}return false;}
        if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return false;
        if(n!=text.size()&&!(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
        if(n==text.size()&&(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    }
    out=text;return true;
}
bool shallow(const char* bytes,std::size_t count){
    unsigned depth=0;bool string=false,escape=false;
    for(std::size_t n=0;n<count;++n){const auto c=bytes[n];if(string){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')string=false;}
        else if(c=='"')string=true;else if(c=='{'||c=='['){if(++depth>4)return false;}
        else if(c=='}'||c==']'){if(!depth)return false;--depth;}}
    return !string&&!depth;
}
bool integer(const nlohmann::json& object,const char* name,unsigned min,unsigned max,unsigned& output){
    if(!object.contains(name))return false;
    const auto& v=object[name];
    if(!v.is_number_integer()||v<min||v>max)return false;
    output=v.get<unsigned>();return true;
}
}
ModSettingsLoad read_mod_settings(const std::filesystem::path& path)noexcept {
    ModSettingsLoad result;try{
        std::filesystem::path full;bool missing=false;if(!local_path(path,full,missing))return result;
        if(missing){result.status=ModSettingsStatus::Missing;return result;}
        File file;file.h=CreateFileW(full.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        FILE_ATTRIBUTE_TAG_INFO attributes{};LARGE_INTEGER size{};
        if(file.h==INVALID_HANDLE_VALUE||GetFileType(file.h)!=FILE_TYPE_DISK||
           !GetFileInformationByHandleEx(file.h,FileAttributeTagInfo,&attributes,sizeof attributes)||
           (attributes.FileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||!GetFileSizeEx(file.h,&size))return result;
        result.status=ModSettingsStatus::Invalid;if(size.QuadPart<=0||size.QuadPart>4096)return result;
        std::array<char,4096> bytes{};DWORD count=0;
        if(!ReadFile(file.h,bytes.data(),DWORD(size.QuadPart),&count,nullptr)||count!=size.QuadPart||!shallow(bytes.data(),count))return result;
        std::set<std::string> keys;bool duplicate=false;
        const auto json=nlohmann::json::parse(bytes.data(),bytes.data()+count,[&](int,nlohmann::json::parse_event_t event,nlohmann::json& value){
            if(event==nlohmann::json::parse_event_t::key&&!keys.insert(value.get<std::string>()).second)duplicate=true;
            return true;},false);
        if(duplicate||!json.is_object()||!json.contains("schemaVersion")||!json["schemaVersion"].is_number_integer())return result;
        const bool legacy=json["schemaVersion"]==1;
        if(!legacy&&json["schemaVersion"]!=2){result.status=ModSettingsStatus::UnsupportedVersion;return result;}
        ModSettings s;if(json.size()!=(legacy?6u:8u)||!json.contains("voiceEnabled")||!json["voiceEnabled"].is_boolean()||
            !integer(json,"mouthStrength",0,100,s.strength)||!integer(json,"sensitivity",0,100,s.sensitivity)||
            !integer(json,"smoothingMs",20,140,s.smoothing_ms)||!integer(json,"menuHotkey",0,255,s.hotkey))return result;
        if(s.hotkey==0x2d)s.hotkey=ModSettings{}.hotkey;
        if(!legacy){
            if(!json.contains("voiceSource")||!json["voiceSource"].is_string()||!json.contains("microphoneDeviceId")||!json["microphoneDeviceId"].is_string())return result;
            const auto source=json["voiceSource"].get<std::string>();
            if(source!="game"&&source!="microphone")return result;
            s.voice_source=source=="microphone"?VoiceInputSource::Microphone:VoiceInputSource::Game;
            const auto id=json["microphoneDeviceId"].get<std::string>();if(!id.empty())s.microphone_device=std::make_shared<const std::string>(id);
        }
        if(!valid_mod_settings(s))return result;
        s.voice_enabled=json["voiceEnabled"].get<bool>();result.settings=s;result.status=ModSettingsStatus::Loaded;return result;
    }catch(...){return result;}
}
bool write_mod_settings(const std::filesystem::path& path,const ModSettings& settings)noexcept {
    std::filesystem::path temporary;bool own_temp=false;
    try{
        if(!valid_mod_settings(settings))return false;
        std::filesystem::path full;bool missing=false;
        if(!local_path(path,full,missing))return false;
        const nlohmann::json json={{"schemaVersion",2},{"voiceEnabled",settings.voice_enabled},{"mouthStrength",settings.strength},
            {"sensitivity",settings.sensitivity},{"smoothingMs",settings.smoothing_ms},{"menuHotkey",settings.hotkey},
            {"voiceSource",settings.voice_source==VoiceInputSource::Microphone?"microphone":"game"},{"microphoneDeviceId",microphone_device_id(settings)}};
        const auto bytes=json.dump(2)+'\n';if(bytes.size()>4096)return false;
        static std::atomic<unsigned> serial{0};
        File file;for(unsigned n=0;n<8;++n){temporary=full;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(serial.fetch_add(1));
            file.h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(file.h!=INVALID_HANDLE_VALUE){own_temp=true;break;}if(GetLastError()!=ERROR_FILE_EXISTS)return false;}
        if(!own_temp)return false;
        DWORD written=0;
        const bool complete=WriteFile(file.h,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file.h);
        CloseHandle(file.h);file.h=INVALID_HANDLE_VALUE;
        if(!complete||!local_path(path,full,missing)||!MoveFileExW(temporary.c_str(),full.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DeleteFileW(temporary.c_str());return false;}
        return true;
    }catch(...){if(own_temp)DeleteFileW(temporary.c_str());return false;}
}
}
