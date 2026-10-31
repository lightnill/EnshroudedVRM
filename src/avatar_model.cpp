#include "avatar_model.hpp"
#include <cmath>
#include "avatar_settings.hpp"
#include "sha256.hpp"
#include "retarget.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cwctype>
namespace evrm {
bool normalize_avatar_path(const std::string& input,std::string& output,bool require_file)noexcept {try{
    if(!valid_avatar_path(input))return false;
    const auto path=std::filesystem::u8path(input);auto extension=path.extension().wstring();
    for(auto& c:extension)c=std::towlower(c);
    if(extension!=L".vrm")return false;
    std::array<wchar_t,32768> full{};const auto size=GetFullPathNameW(path.c_str(),DWORD(full.size()),full.data(),nullptr);
    if(size<4||size>=full.size()||full[0]=='\\'||full[1]!=':')return false;
    const wchar_t drive[]={full[0],L':',L'\\',0};const auto type=GetDriveTypeW(drive);
    if(type==DRIVE_REMOTE||type==DRIVE_UNKNOWN||type==DRIVE_NO_ROOT_DIR)return false;
    const std::wstring value(full.data(),size);
    for(std::size_t n=3;n<=value.size();++n){if(n!=value.size()&&value[n]!=L'\\'&&value[n]!=L'/')continue;
        const auto attributes=GetFileAttributesW(value.substr(0,n).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){if(!require_file&&n==value.size())break;return false;}
        if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return false;
        if(n!=value.size()&&!(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
        if(n==value.size()&&(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;}
    auto result=std::filesystem::path(value).u8string();if(!valid_avatar_path(result))return false;
    output=std::move(result);return true;
}catch(...){return false;}}
AvatarLoadResult prepare_avatar_bytes(const std::vector<std::uint8_t>& bytes,const std::string& path)noexcept {try{
    if(!valid_avatar_path(path))return {{},"Choose a local VRM file"};
    auto imported=vrm::load_vrm_bytes(bytes);if(!imported)return {{},imported.error};
    const auto head=imported.avatar.humanoid.find("head");
    if(head==imported.avatar.humanoid.end()||head->second<0||std::size_t(head->second)>=imported.avatar.nodes.size()||
        !std::isfinite(imported.avatar.nodes[head->second].world[13])||imported.avatar.nodes[head->second].world[13]<=0)
        return {{},"Avatar head rest height must be positive and finite for game retargeting"};
    if(imported.avatar.materials.size()>4096||imported.avatar.images.size()>4096)return {{},"Avatar has too many materials or images"};
    auto result=std::make_shared<PreparedAvatar>();result->path=path;
    const auto digest=detail::sha256(bytes.data(),bytes.size());constexpr char digits[]="0123456789abcdef";
    for(auto value:digest){result->fingerprint+=digits[value>>4];result->fingerprint+=digits[value&15];}
    result->asset=std::make_shared<const vrm::Avatar>(std::move(imported.avatar));
    result->name=result->asset->title.empty()?std::filesystem::u8path(path).stem().u8string():result->asset->title;
    if(result->name.size()>1024)result->name=std::filesystem::u8path(path).filename().u8string();
    for(auto& c:result->name)if(static_cast<unsigned char>(c)<32)c=' ';
    for(const auto& material:result->asset->materials)result->material_names.push_back(material.name);
    vrm::SourcePose rest;for(const auto& joint:result->asset->humanoid){const auto& world=result->asset->nodes.at(joint.second).world;rest.rest[joint.first]=world;rest.current[joint.first]=world;}
    const auto pose=vrm::retarget(*result->asset,rest);if(!pose)return {{},"Avatar rest pose: "+pose.error};
    auto skin=std::make_shared<const NativeSkinAsset>(make_native_skin_asset(*result->asset,pose));
    if(!*skin)return {{},"Avatar skin: "+skin->error};
    result->skin=std::move(skin);return {std::move(result),{}};
}catch(const std::exception& error){return {{},error.what()};}catch(...){return {{},"Avatar preparation failed"};}}
AvatarLoadResult load_prepared_avatar(const std::string& path)noexcept {try{
    std::string normalized;if(!normalize_avatar_path(path,normalized))return {{},"Choose an existing local VRM file"};
    struct File {HANDLE value=INVALID_HANDLE_VALUE;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} file;
    file.value=CreateFileW(std::filesystem::u8path(normalized).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    FILE_ATTRIBUTE_TAG_INFO attributes{};LARGE_INTEGER length{};const vrm::ImportLimits limits;
    if(file.value==INVALID_HANDLE_VALUE||GetFileType(file.value)!=FILE_TYPE_DISK||!GetFileInformationByHandleEx(file.value,FileAttributeTagInfo,&attributes,sizeof attributes)||
        (attributes.FileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||!GetFileSizeEx(file.value,&length)||length.QuadPart<=0||std::uint64_t(length.QuadPart)>limits.file_bytes)return {{},"Cannot read this VRM file"};
    std::vector<std::uint8_t> bytes(std::size_t(length.QuadPart));DWORD count=0;
    if(!ReadFile(file.value,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size())return {{},"Cannot read the complete VRM file"};
    return prepare_avatar_bytes(bytes,normalized);
}catch(const std::exception& error){return {{},error.what()};}catch(...){return {{},"Avatar loading failed"};}}
}
