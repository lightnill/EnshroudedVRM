#include "avatar_service.hpp"
#include "player_assignments.hpp"
#include <deque>
#include <map>
#include <set>
#include "runtime_log.hpp"
#include <shobjidl.h>
#include <algorithm>
#include <atomic>
#include <mutex>
namespace evrm {
namespace {
template<class T>struct Com {T* value=nullptr;~Com(){if(value)value->Release();}T** put(){return &value;}T* operator->()const{return value;}};
std::string utf8(const wchar_t* text){
    if(!text)return {};
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,nullptr,0,nullptr,nullptr);if(count<=1||count>4097)return {};
    std::string result(std::size_t(count),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,result.data(),count,nullptr,nullptr))return {};
    result.pop_back();return result;
}
bool same_path(const std::string& a,const std::string& b){
    const auto left=std::filesystem::u8path(a).lexically_normal().wstring(),right=std::filesystem::u8path(b).lexically_normal().wstring();
    return CompareStringOrdinal(left.data(),int(left.size()),right.data(),int(right.size()),TRUE)==CSTR_EQUAL;
}
AvatarPickResult native_picker(std::uintptr_t raw,void*){
    const auto window=reinterpret_cast<HWND>(raw);DWORD process=0;
    if(!window||!IsWindow(window)||!GetWindowThreadProcessId(window,&process)||process!=GetCurrentProcessId())return {{},"Avatar file picker unavailable",false};
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(initialized))return {{},"Avatar file picker unavailable",false};
    struct Cleanup {~Cleanup(){CoUninitialize();}} cleanup;
    Com<IFileOpenDialog> dialog;auto result=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dialog.put()));
    if(FAILED(result))return {{},"Avatar file picker unavailable",false};
    FILEOPENDIALOGOPTIONS options=0;result=dialog->GetOptions(&options);
    if(SUCCEEDED(result))result=dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR|FOS_DONTADDTORECENT);
    const COMDLG_FILTERSPEC filter{L"VRM avatars",L"*.vrm"};
    if(SUCCEEDED(result))result=dialog->SetFileTypes(1,&filter);
    if(SUCCEEDED(result))result=dialog->SetDefaultExtension(L"vrm");
    if(SUCCEEDED(result))result=dialog->SetTitle(L"Choose an avatar");
    if(FAILED(result))return {{},"Avatar file picker unavailable",false};
    result=dialog->Show(window);if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {{},{},true};
    if(FAILED(result))return {{},"Avatar file picker could not open",false};
    Com<IShellItem> item;result=dialog->GetResult(item.put());if(FAILED(result))return {{},"Cannot read selected avatar",false};
    wchar_t* path=nullptr;result=item->GetDisplayName(SIGDN_FILESYSPATH,&path);
    struct Release {wchar_t* value;~Release(){CoTaskMemFree(value);}} release{path};
    auto value=SUCCEEDED(result)?utf8(path):std::string{};
    if(!valid_avatar_path(value))return {{},"Choose a local VRM file",false};
    return {std::move(value),{},false};
}
void remember(AvatarPreferences& prefs,const PreparedAvatar& model){
    prefs.selected_path=model.path;
    prefs.recent.erase(std::remove_if(prefs.recent.begin(),prefs.recent.end(),[&](const auto& path){return same_path(path,model.path);}),prefs.recent.end());
    prefs.recent.insert(prefs.recent.begin(),model.path);if(prefs.recent.size()>16)prefs.recent.resize(16);
    const auto found=std::find_if(prefs.profiles.begin(),prefs.profiles.end(),[&](const auto& profile){return profile.fingerprint==model.fingerprint;});
    AvatarProfile profile=found==prefs.profiles.end()?AvatarProfile{model.fingerprint,AvatarShaderPreset::GameLighting,{}}:*found;
    if(found!=prefs.profiles.end())prefs.profiles.erase(found);
    if(prefs.profiles.size()==16)prefs.profiles.erase(prefs.profiles.begin());
    prefs.profiles.push_back(std::move(profile));
}
}
#include "avatar_player_service.inl"
