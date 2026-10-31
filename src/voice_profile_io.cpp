#include "voice_profile_io.hpp"
#include "../vendor/tinygltf/json.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <vector>
namespace evrm {
namespace {
struct File {HANDLE handle=INVALID_HANDLE_VALUE;~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
bool shallow_json(const std::vector<char>& text)noexcept {
    unsigned depth=0;bool string=false,escape=false;
    for(char c:text){if(string){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')string=false;continue;}
        if(c=='"')string=true;
        else if(c=='{'||c=='['){if(++depth>32)return false;}
        else if(c=='}'||c==']'){if(!depth)return false;--depth;}}
    return !string&&!depth;
}
}
VoiceProfileLoad load_voice_profiles(const std::filesystem::path& path)noexcept {
    VoiceProfileLoad result{default_voice_profiles(),false,"Voice profiles unavailable; using defaults."};
    try{
        std::array<wchar_t,32768> full{};const auto length=GetFullPathNameW(path.c_str(),DWORD(full.size()),full.data(),nullptr);
        if(!length||length>=full.size()||full[0]=='\\'||full[1]!=':')return result;
        const wchar_t root[]={full[0],L':',L'\\',0};if(GetDriveTypeW(root)==DRIVE_REMOTE)return result;
        // Refuse optional data reached through a reparse point, including a
        // parent directory. This reader is initialization-only, never the relay.
        std::wstring component(full.data(),length);
        for(std::size_t n=3;n<=component.size();++n){if(n!=component.size()&&component[n]!=L'\\'&&component[n]!=L'/')continue;
            const auto prefix=component.substr(0,n);const auto attributes=GetFileAttributesW(prefix.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();
                if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)result.diagnostic=nullptr;
                return result;}
            if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return result;
        }
        File file;file.handle=CreateFileW(full.data(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(file.handle==INVALID_HANDLE_VALUE)return result;
        FILE_ATTRIBUTE_TAG_INFO attributes{};LARGE_INTEGER size{};
        if(GetFileType(file.handle)!=FILE_TYPE_DISK||!GetFileInformationByHandleEx(file.handle,FileAttributeTagInfo,&attributes,sizeof(attributes))||
           attributes.FileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)||!GetFileSizeEx(file.handle,&size)||size.QuadPart<=0||size.QuadPart>65536)return result;
        std::vector<char> bytes(static_cast<std::size_t>(size.QuadPart));DWORD read=0;
        if(!ReadFile(file.handle,bytes.data(),DWORD(bytes.size()),&read,nullptr)||read!=bytes.size()||!shallow_json(bytes))return result;
        const auto json=nlohmann::json::parse(bytes.begin(),bytes.end(),nullptr,false);
        if(!json.is_object()||json.size()!=3||!json.contains("schemaVersion")||!json["schemaVersion"].is_number_integer()||json["schemaVersion"]!=1||
           !json.contains("featureConvention")||json["featureConvention"]!=voice_feature_convention||!json.contains("profiles")||!json["profiles"].is_object()||json["profiles"].size()!=5)return result;
        VoiceProfiles profiles;const char* names[]={"a","i","u","e","o"};
        for(unsigned vowel=0;vowel<5;++vowel){const auto& bank=json["profiles"];
            if(!bank.contains(names[vowel])||!bank[names[vowel]].is_array()||bank[names[vowel]].empty()||bank[names[vowel]].size()>32)return result;
            for(const auto& item:bank[names[vowel]]){if(!item.is_array()||item.size()!=12)return result;
                auto& profile=profiles.data[profiles.count++];profile.vowel=std::uint8_t(vowel);
                for(unsigned c=0;c<12;++c){if(!item[c].is_number())return result;profile.mfcc[c]=item[c].get<double>();
                    if(!std::isfinite(profile.mfcc[c])||std::abs(profile.mfcc[c])>100)return result;}}
        }
        if(!validate_voice_profiles(profiles))return result;
        result.profiles=profiles;result.custom=true;result.diagnostic=nullptr;return result;
    }catch(...){return result;}
}
}
