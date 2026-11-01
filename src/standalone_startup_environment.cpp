// Small compatibility adapter for the remaining diagnostic/cache consumers.
// These values are outputs of native verification, never evidence of it.
#include "standalone_startup.hpp"
#include "standalone_files.hpp"
#include "runtime_log.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include <vector>

namespace evrm {
namespace {
void clear_legacy_environment()noexcept {
    for(const auto name:{L"MODEL",L"PROFILE",L"CATALOG",L"VERIFIED_BUILD",L"VERIFIED_EXE_SHA256",L"SHADERS",L"LOG",
        L"CAPTURE_DIR",L"CAPTURE_FRAMES",L"CAPTURE_PLAYER_ANCHOR",L"CAPTURE_SKIN_ONLY",L"NATIVE_REPLACEMENT",L"NATIVE_ADDITIVE",
        L"DEFER_RENDERING_PROOF",L"GPU_SKINNING",L"TOOL_SLOTS",L"SPRING_BONES",L"DEBUG_OFFSET",L"TRACE_COMMANDS",L"TRACE_DIR",
        L"ARTIFACT_PROBE_FILE",L"VOICE_ENABLED"}) {
        wchar_t key[96]=L"ENSHROUDED_VRM_";std::size_t at=15;
        for(const wchar_t* p=name;*p && at<95;++p)key[at++]=*p;
        key[at]=0;SetEnvironmentVariableW(key,nullptr);
    }
}
bool history_name(const std::wstring& name) {
    if(name.size()<16 || name.size()>100 || name.compare(0,8,L"startup-") || name.substr(name.size()-4)!=L".log")return false;
    unsigned separators=0;
    for(std::size_t i=8;i<name.size()-4;++i) {
        if(name[i]==L'-')++separators;
        else if(name[i]<L'0' || name[i]>L'9')return false;
    }
    return separators==2;
}
bool write_new(const std::filesystem::path& path,const std::string& bytes,DWORD disposition) {
    // OPEN_ALWAYS obtains an exclusive handle before any truncation. Earlier
    // pathname checks cannot authorize a different file substituted afterward.
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE|FILE_READ_ATTRIBUTES,0,nullptr,disposition==CREATE_ALWAYS?OPEN_ALWAYS:disposition,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};wchar_t actual[32768]{},expected[32768]{};
    const auto actual_size=GetFinalPathNameByHandleW(file,actual,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    const auto expected_size=GetFullPathNameW(path.c_str(),32768,expected,nullptr);
    const auto prefix=actual_size>=4 && std::wstring_view(actual,4)==L"\\\\?\\"?4u:0u;
    bool valid=GetFileInformationByHandle(file,&info) && GetFileType(file)==FILE_TYPE_DISK && info.nNumberOfLinks==1 &&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) && actual_size>prefix && actual_size<32768 && expected_size && expected_size<32768 &&
        actual_size-prefix==expected_size && CompareStringOrdinal(actual+prefix,int(actual_size-prefix),expected,int(expected_size),TRUE)==CSTR_EQUAL;
    if(valid && disposition==CREATE_ALWAYS)valid=SetEndOfFile(file)!=FALSE;
    DWORD written=0;const bool result=valid && bytes.size()<=MAXDWORD && WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size();CloseHandle(file);return result;
}
void rotate_history(const std::filesystem::path& directory) {
    struct Entry {std::filesystem::path path;std::filesystem::file_time_type time;std::uintmax_t size;};std::vector<Entry> files;
    for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        if(!history_name(entry.path().filename().wstring()))continue;
        if(files.size()>=256)throw std::runtime_error("startup history entry limit");
        detail::StandaloneFile checked(entry.path());
        files.push_back({entry.path(),entry.last_write_time(),entry.file_size()});
    }
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.time>b.time;});
    std::uintmax_t total=0;unsigned kept=0;
    for(const auto& file:files) {
        if(kept<5 && file.size<=50*1024 && total+file.size<=256*1024){total+=file.size;++kept;}
        else std::filesystem::remove(file.path);
    }
}
bool fresh_runtime_log(const RuntimeStartupContext& context) {
    if(context.log_path.empty() || !detail::local_standalone_path(context.state_directory))return false;
    detail::StandaloneFile state(context.state_directory,true);
    const auto history=context.state_directory/L"startup-history";
    if(!detail::local_standalone_path(history,true))return false;
    std::filesystem::create_directories(history);detail::StandaloneFile history_directory(history,true);
    const auto attributes=GetFileAttributesW(context.log_path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES) {
        std::string previous;
        {detail::StandaloneFile file(context.log_path);previous=file.tail(49*1024);}
        if(!previous.empty()) {
            FILETIME stamp{};GetSystemTimeAsFileTime(&stamp);ULARGE_INTEGER number{};number.HighPart=stamp.dwHighDateTime;number.LowPart=stamp.dwLowDateTime;
            const auto name=L"startup-"+std::to_wstring(number.QuadPart)+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".log";
            const auto bytes="Previous session tail; this is not evidence of current startup.\n"+previous;
            if(!write_new(history/name,bytes,CREATE_NEW))return false;
        }
    }else if(GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND)return false;
    rotate_history(history);
    return write_new(context.log_path,"",CREATE_ALWAYS);
}
}
void initialize_runtime_startup_environment()noexcept {
    static std::once_flag once;
    try {std::call_once(once,[]{
        clear_legacy_environment();const auto& context=runtime_startup_context();
        if(!context.ready()) {
            if(!context.diagnostic_owner_verified || context.state_directory.empty() || !detail::local_standalone_path(context.state_directory,true))return;
            std::error_code error;std::filesystem::create_directories(context.state_directory,error);
            if(error || !detail::local_standalone_path(context.state_directory))return;
        }else if(!context.state_writable)return;
        if(context.ready() && !context.capture_directory.empty())SetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_DIR",context.capture_directory.c_str());
        if(fresh_runtime_log(context)) {
            SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",context.log_path.c_str());
            log_event_lazy([&]{return std::string("standalone.startup status=")+(context.ready()?"ready":"declined")+" reason="+context.reason+" version="+context.version+" validation-us="+std::to_string(context.validation_microseconds)+
                " files="+std::to_string(context.verified_files)+" hashed-bytes="+std::to_string(context.hashed_bytes)+
                " avatar-import="+std::to_string(static_cast<unsigned>(context.imports.avatar))+" settings-import="+std::to_string(static_cast<unsigned>(context.imports.mod));});
        }
    });}catch(...){SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",nullptr);}
}
}
