#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

namespace evrm {
namespace detail {
struct RuntimeLogState {
    std::mutex mutex;
    unsigned lines=0;
    HANDLE file=INVALID_HANDLE_VALUE;
    std::wstring path,final_name;
    ~RuntimeLogState(){close();}
    void close()noexcept {
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        file=INVALID_HANDLE_VALUE;path.clear();final_name.clear();
    }
    bool same_file_name()const noexcept {
        if(final_name.empty())return false;
        FILE_STANDARD_INFO info{};
        if(!GetFileInformationByHandleEx(file,FileStandardInfo,&info,sizeof info)||info.DeletePending)return false;
        wchar_t name[32768];const auto length=GetFinalPathNameByHandleW(file,name,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        return length&&length<32768&&length==final_name.size()&&std::char_traits<wchar_t>::compare(name,final_name.data(),length)==0;
    }
    bool select(const wchar_t* value,DWORD length) {
        const bool absolute=(length>=3&&value[1]==L':'&&(value[2]==L'\\'||value[2]==L'/'))||
            (length>=2&&value[0]==L'\\'&&value[1]==L'\\');
        // Relative paths still follow a changing working directory. Failed
        // metadata queries fall back to reopening, preserving diagnostics.
        if(file!=INVALID_HANDLE_VALUE&&absolute&&path.size()==length&&path.compare(0,length,value,length)==0&&same_file_name())return true;
        close();std::wstring next(value,length);
        file=CreateFileW(next.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        path=std::move(next);wchar_t name[32768];
        const auto count=GetFinalPathNameByHandleW(file,name,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(count&&count<32768)final_name.assign(name,count);
        return true;
    }
};
}
inline void log_event(const std::string& event) noexcept {
    try {
        static detail::RuntimeLogState state;
        std::lock_guard<std::mutex> guard(state.mutex);
        wchar_t path[32768];
        const auto length = GetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG", path, 32768);
        if(!length||length>=32768||state.lines>=10000){state.close();return;}
        if(!state.select(path,length))return;
        const auto row=std::to_string(GetTickCount64())+" pid="+std::to_string(GetCurrentProcessId())+' '+event.substr(0,4096)+'\n';
        ++state.lines; // Bound attempted rows even after a short/failed write.
        DWORD offset=0;
        while(offset<row.size()){
            DWORD written=0;
            if(!WriteFile(state.file,row.data()+offset,DWORD(row.size()-offset),&written,nullptr)||!written){state.close();return;}
            offset+=written;
        }
    } catch (...) {
        // Diagnostics must not change the graphics call's result.
    }
}
inline void log_event(const char* event) noexcept {
    if (!event) return;
    try { log_event(std::string(event)); }
    catch (...) { /* Keep literal conversion inside the diagnostic boundary. */ }
}
// Keep diagnostic formatting/allocation inside the same optional boundary.
template<class Builder> inline void log_event_lazy(Builder&& builder) noexcept {
    try { log_event(builder()); } catch (...) {}
}
}
