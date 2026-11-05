#include "standalone_files.hpp"
#include <array>
#include <stdexcept>

namespace evrm::detail {
namespace {
void need(bool valid) { if(!valid)throw std::runtime_error("standalone file unavailable or unsafe"); }
std::filesystem::path full_path(const std::filesystem::path& path) {
    need(!path.empty() && path.is_absolute() && path.native().size()<32768);
    std::array<wchar_t,32768> buffer{};const auto count=GetFullPathNameW(path.c_str(),DWORD(buffer.size()),buffer.data(),nullptr);
    need(count>3 && count<buffer.size() && buffer[0]!=L'\\' && buffer[1]==L':');
    const wchar_t drive[]={buffer[0],L':',L'\\',0};const auto type=GetDriveTypeW(drive);
    need(type==DRIVE_FIXED || type==DRIVE_REMOVABLE || type==DRIVE_RAMDISK);
    return std::filesystem::path(std::wstring(buffer.data(),count));
}
}
bool same_standalone_path(const std::filesystem::path& left,const std::filesystem::path& right) {
    const auto a=left.lexically_normal().wstring(),b=right.lexically_normal().wstring();
    return a.size()<32768 && b.size()<32768 && a.size()==b.size() &&
        CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_EQUAL;
}
bool local_standalone_path(const std::filesystem::path& path,bool allow_missing) {
    try {
        const auto full=full_path(path).wstring();
        for(std::size_t end=3;end<=full.size();++end) {
            if(end!=full.size() && full[end]!=L'\\')continue;
            const auto part=full.substr(0,end);const auto attributes=GetFileAttributesW(part.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES) {
                const auto error=GetLastError();return allow_missing && (error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND);
            }
            if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return false;
            if(end!=full.size() && !(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
        }
        return true;
    }catch(...){return false;}
}
StandaloneFile::StandaloneFile(const std::filesystem::path& path,bool directory,StandaloneReadPolicy policy) {
    need(local_standalone_path(path));const auto full=full_path(path);
    handle_=CreateFileW(full.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                        FILE_FLAG_OPEN_REPARSE_POINT | (directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_FLAG_SEQUENTIAL_SCAN),nullptr);
    need(handle_!=INVALID_HANDLE_VALUE);
    try {
        BY_HANDLE_FILE_INFORMATION info{};std::array<wchar_t,32768> final{};
        const auto count=GetFinalPathNameByHandleW(handle_,final.data(),DWORD(final.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        need(count>4 && count<final.size());std::wstring actual(final.data(),count);
        if(actual.substr(0,4)==L"\\\\?\\")actual.erase(0,4);
        need(GetFileInformationByHandle(handle_,&info) && GetFileType(handle_)==FILE_TYPE_DISK &&
             !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory &&
             (directory || policy==StandaloneReadPolicy::ImmutablePackage || info.nNumberOfLinks==1) && same_standalone_path(actual,full));
    }catch(...){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;throw;}
}
StandaloneFile::~StandaloneFile(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
std::string StandaloneFile::read(std::size_t limit)const {
    LARGE_INTEGER length{},zero{};need(GetFileSizeEx(handle_,&length) && length.QuadPart>0 && std::uint64_t(length.QuadPart)<=limit);
    need(SetFilePointerEx(handle_,zero,nullptr,FILE_BEGIN));std::string bytes(std::size_t(length.QuadPart),'\0');
    for(std::size_t at=0;at<bytes.size();) {
        const auto wanted=DWORD(std::min<std::size_t>(65536,bytes.size()-at));DWORD count=0;
        need(ReadFile(handle_,bytes.data()+at,wanted,&count,nullptr) && count==wanted);at+=count;
    }
    char extra=0;DWORD count=0;need(ReadFile(handle_,&extra,1,&count,nullptr) && count==0);return bytes;
}
std::string StandaloneFile::tail(std::size_t limit)const {
    LARGE_INTEGER length{};need(GetFileSizeEx(handle_,&length) && length.QuadPart>=0);
    const auto count=std::min<std::uint64_t>(std::uint64_t(length.QuadPart),limit);
    LARGE_INTEGER start{};start.QuadPart=length.QuadPart-static_cast<LONGLONG>(count);
    need(SetFilePointerEx(handle_,start,nullptr,FILE_BEGIN));std::string bytes(static_cast<std::size_t>(count),'\0');DWORD read=0;
    need(count<=MAXDWORD && ReadFile(handle_,bytes.data(),DWORD(count),&read,nullptr) && read==count);return bytes;
}
}
