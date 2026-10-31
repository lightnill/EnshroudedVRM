#include "voice_automatic.hpp"
#include "sha256.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace evrm {
namespace {
// Export ABI is confirmed by the unmodified official OVRLipSync.cs binding.
// The similarly named five-argument native C API is a separate static shim.
struct Library {
    HMODULE module=nullptr;bool initialized=false;const char* diagnostic="sdk-file";
    int (*create)(unsigned*,int,int,bool)=nullptr;
    int (*destroy)(unsigned)=nullptr;
    int (*reset)(unsigned)=nullptr;
    int (*signal)(unsigned,int,int,int)=nullptr;
    int (*process)(unsigned,const void*,unsigned,int,int*,int*,float*,int,float*,float*,int)=nullptr;
};
struct File {HANDLE value=INVALID_HANDLE_VALUE;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
void module_anchor(){}
template<class F>F symbol(HMODULE module,const char* name)noexcept {
    const auto raw=GetProcAddress(module,name);F result{};static_assert(sizeof raw==sizeof result);std::memcpy(&result,&raw,sizeof result);return result;
}
Library load_library()noexcept {
    Library result;
    try{
        HMODULE own=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&module_anchor),&own))return result;
        std::array<wchar_t,32768> path{};const auto count=GetModuleFileNameW(own,path.data(),DWORD(path.size()));
        if(!count||count>=path.size())return result;
        const auto dll=std::filesystem::path(path.data()).parent_path()/L"OVRLipSync.dll";
        File file;file.value=CreateFileW(dll.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        FILE_ATTRIBUTE_TAG_INFO attributes{};LARGE_INTEGER size{};
        if(file.value==INVALID_HANDLE_VALUE||GetFileType(file.value)!=FILE_TYPE_DISK||
            !GetFileInformationByHandleEx(file.value,FileAttributeTagInfo,&attributes,sizeof attributes)||
            (attributes.FileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||
            !GetFileSizeEx(file.value,&size)||size.QuadPart!=40471800)return result;
        std::vector<std::uint8_t> bytes(std::size_t(size.QuadPart));DWORD read=0;
        if(!ReadFile(file.value,bytes.data(),DWORD(bytes.size()),&read,nullptr)||read!=bytes.size())return result;
        static constexpr detail::Sha256Digest expected{{0xf2,0x0e,0x7b,0xcb,0x50,0xf1,0x59,0x1e,0x1c,0x4f,0xdf,0x5f,0xb1,0x6c,0xed,0xa8,
            0x76,0xec,0x59,0x9c,0x17,0x31,0xfd,0x85,0x77,0x79,0xb9,0x77,0xb4,0xe5,0x85,0xb7}};
        if(detail::sha256(bytes.data(),bytes.size())!=expected){result.diagnostic="sdk-hash";return result;}
        const auto final_count=GetFinalPathNameByHandleW(file.value,path.data(),DWORD(path.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!final_count||final_count>=path.size())return result;
        // Full verified path; dependencies resolve only from System32. No PATH,
        // working-directory search, download or process-wide loader change.
        auto module=LoadLibraryExW(path.data(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module){result.diagnostic="sdk-load";return result;}
        const auto loaded_count=GetModuleFileNameW(module,path.data(),DWORD(path.size()));File loaded;
        if(loaded_count&&loaded_count<path.size())loaded.value=CreateFileW(path.data(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION before{},after{};
        if(loaded.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file.value,&before)||!GetFileInformationByHandle(loaded.value,&after)||
            before.dwVolumeSerialNumber!=after.dwVolumeSerialNumber||before.nFileIndexHigh!=after.nFileIndexHigh||before.nFileIndexLow!=after.nFileIndexLow){
            FreeLibrary(module);result.diagnostic="sdk-loaded-file";return result;}
        const auto initialize=symbol<int(*)(int,int)>(module,"ovrLipSyncDll_Initialize");
        const auto version=symbol<const char*(*)(int*,int*,int*)>(module,"ovrLipSyncDll_GetVersion");
        result.create=symbol<decltype(result.create)>(module,"ovrLipSyncDll_CreateContextEx");
        result.destroy=symbol<decltype(result.destroy)>(module,"ovrLipSyncDll_DestroyContext");
        result.reset=symbol<decltype(result.reset)>(module,"ovrLipSyncDll_ResetContext");
        result.signal=symbol<decltype(result.signal)>(module,"ovrLipSyncDll_SendSignal");
        result.process=symbol<decltype(result.process)>(module,"ovrLipSyncDll_ProcessFrameEx");
        if(!initialize||!version||!result.create||!result.destroy||!result.reset||!result.signal||!result.process){FreeLibrary(module);result={};result.diagnostic="sdk-exports";return result;}
        int major=0,minor=0,patch=0;version(&major,&minor,&patch);
        if(major!=1||minor!=61||patch!=0){FreeLibrary(module);result={};result.diagnostic="sdk-version";return result;}
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(result.process),&pinned)){
            FreeLibrary(module);result={};result.diagnostic="sdk-lifetime";return result;}
        result.module=module;
        if(initialize(48000,480)!=0){result.diagnostic="sdk-initialize";return result;}
        result.initialized=true;result.diagnostic="ready";return result;
    }catch(...){return result;}
}
const Library& library()noexcept {static const Library state=load_library();return state;}
}
AutomaticVoiceClassifier::AutomaticVoiceClassifier(bool enabled)noexcept {
    if(!enabled)return;
    const auto& state=library();library_=&state;
    if(!state.initialized)return;
    if(state.create(&context_,1,48000,false)!=0||!context_){failed_=true;return;}
    if(state.signal(context_,3,0,0)!=0)failed_=true;
}
AutomaticVoiceClassifier::~AutomaticVoiceClassifier(){if(library_&&context_)static_cast<const Library*>(library_)->destroy(context_);}
bool AutomaticVoiceClassifier::ready()const noexcept {return library_&&static_cast<const Library*>(library_)->initialized&&context_&&!failed_;}
const char* AutomaticVoiceClassifier::diagnostic()const noexcept {
    if(!library_)return "not-requested";
    const auto& state=*static_cast<const Library*>(library_);return !state.initialized?state.diagnostic:failed_?"sdk-context":"ready";
}
void AutomaticVoiceClassifier::reset()noexcept {
    if(ready()){const auto& state=*static_cast<const Library*>(library_);if(state.reset(context_)!=0||state.signal(context_,3,0,0)!=0)failed_=true;}
}
AutomaticVoicePrediction AutomaticVoiceClassifier::consume(const std::array<float,480>& samples)noexcept {
    AutomaticVoicePrediction result;if(!ready())return result;
    std::array<float,480> bounded{};
    for(unsigned n=0;n<480;++n){const auto value=samples[n];if(!std::isfinite(value)||std::abs(value)>16){reset();return result;}bounded[n]=std::clamp(value,-1.f,1.f);}
    float laughter=0;
    if(static_cast<const Library*>(library_)->process(context_,bounded.data(),480,2,&result.frame_number,&result.delay_ms,result.visemes.data(),15,&laughter,nullptr,0)!=0){failed_=true;return {};}
    for(auto& weight:result.visemes){if(!std::isfinite(weight)||weight<-.001f||weight>1.001f){reset();return {};}weight=std::clamp(weight,0.f,1.f);}
    result.ready=true;return result;
}
}
