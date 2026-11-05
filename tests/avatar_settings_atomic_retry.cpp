#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace fixture {
const auto real_move=&MoveFileExW;
BOOL WINAPI observed_move(LPCWSTR,LPCWSTR,DWORD);
}
// Route only the final rename. All validation, serialization, owned temporary
// creation, writes, flushing and cleanup execute the actual production code.
#define MoveFileExW fixture::observed_move
#include "../src/avatar_settings.cpp"
#undef MoveFileExW

namespace fixture {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
enum class Mode {Off,Real,ReaderThenRelease,Denied,OtherError,DirectoryAfterDenial};
Mode mode=Mode::Off;
HANDLE reader=INVALID_HANDLE_VALUE;
unsigned moves=0;
DWORD injected_error=0,first_error=0,expected_flags=0;
std::wstring source_name,destination_name;
struct Temporary {
    BY_HANDLE_FILE_INFORMATION information{};
    std::vector<unsigned char> bytes;
};
Temporary initial;
std::filesystem::path displaced;
Temporary inspect_temporary(LPCWSTR path){
    const auto file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    need(file!=INVALID_HANDLE_VALUE,"flushed owned temporary cannot be read at rename");
    Temporary result;need(GetFileInformationByHandle(file,&result.information)!=FALSE,"temporary identity unavailable");
    need(!result.information.nFileSizeHigh&&result.information.nFileSizeLow<128*1024,"unexpected temporary size");
    result.bytes.resize(result.information.nFileSizeLow);DWORD count=0;
    need(ReadFile(file,result.bytes.data(),static_cast<DWORD>(result.bytes.size()),&count,nullptr)!=FALSE&&count==result.bytes.size(),"temporary bytes incomplete");
    need(CloseHandle(file)!=FALSE,"temporary inspection handle close");return result;
}
void same_temporary(const Temporary& current){
    const auto& a=initial.information;const auto& b=current.information;
    need(a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow,
        "retry recreated the owned temporary");
    need(std::memcmp(&a.ftCreationTime,&b.ftCreationTime,sizeof(FILETIME))==0&&
         std::memcmp(&a.ftLastWriteTime,&b.ftLastWriteTime,sizeof(FILETIME))==0&&initial.bytes==current.bytes,
         "retry rewrote the already flushed temporary");
}
BOOL WINAPI observed_move(LPCWSTR source,LPCWSTR destination,DWORD flags){
    if(mode==Mode::Off)return real_move(source,destination,flags);
    ++moves;need(moves<=8,"rename attempt budget exceeded");need(flags==expected_flags,"rename changed replace/create-only flags");
    const auto temporary=inspect_temporary(source);
    if(moves==1){source_name=source;destination_name=destination;initial=temporary;}
    else {need(source_name==source&&destination_name==destination,"retry changed the original rename paths");same_temporary(temporary);}
    if(mode==Mode::Denied||mode==Mode::OtherError){SetLastError(mode==Mode::Denied?ERROR_ACCESS_DENIED:injected_error);return FALSE;}
    if(mode==Mode::DirectoryAfterDenial){
        need(moves==1,"retry bypassed changed destination path guard");
        displaced=std::filesystem::path(destination);displaced+=L".retained";
        need(real_move(destination,displaced.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE,"retain original before path-guard probe");
        need(CreateDirectoryW(destination,nullptr)!=FALSE,"destination directory probe");
        SetLastError(ERROR_ACCESS_DENIED);return FALSE;
    }
    const auto result=real_move(source,destination,flags);const auto error=GetLastError();
    if(mode==Mode::ReaderThenRelease&&moves==1){
        first_error=error;need(!result&&error==ERROR_ACCESS_DENIED,"production-flags reader did not cause the real expected rename denial");
        need(reader!=INVALID_HANDLE_VALUE&&CloseHandle(reader)!=FALSE,"release production-flags reader");reader=INVALID_HANDLE_VALUE;
    }
    SetLastError(error);return result;
}
void begin(Mode value,DWORD flags=MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH){
    mode=value;moves=0;first_error=0;expected_flags=flags;source_name.clear();destination_name.clear();initial={};displaced.clear();
}
void no_temporary(const std::filesystem::path& root){
    for(const auto& entry:std::filesystem::directory_iterator(root))
        need(entry.path().filename().wstring().find(L".tmp-")==std::wstring::npos,"failed commit leaked its owned temporary");
}
evrm::AvatarPreferences preferences(const char* name,bool override_){
    evrm::AvatarPreferences value;value.selected_path=std::string("C:\\fixture\\")+name+".vrm";value.recent={value.selected_path};
    evrm::AvatarProfile profile{std::string(64,'a'),evrm::AvatarShaderPreset::Enhanced,{}};
    if(override_)profile.overrides.push_back({0,"Skin",evrm::AvatarShaderPreset::Fast});
    value.profiles.push_back(std::move(profile));return value;
}
void persisted(const std::filesystem::path& file,const evrm::AvatarPreferences& expected){
    const auto actual=evrm::read_avatar_preferences(file);
    need(actual.status==evrm::AvatarSettingsStatus::Loaded&&actual.preferences.selected_path==expected.selected_path&&
        actual.preferences.profiles.size()==1&&actual.preferences.profiles[0].fingerprint==std::string(64,'a')&&
        actual.preferences.profiles[0].whole==evrm::AvatarShaderPreset::Enhanced&&
        actual.preferences.profiles[0].overrides.size()==expected.profiles[0].overrides.size(),"commit did not preserve the expected profile/selection");
    if(!expected.profiles[0].overrides.empty())need(actual.preferences.profiles[0].overrides[0].preset==evrm::AvatarShaderPreset::Fast,"old material override changed");
}
}

int wmain(int argc,wchar_t** argv){
    using namespace fixture;need(argc==3,"case and private folder required");
    const std::wstring which=argv[1];const std::filesystem::path root=argv[2],file=root/L"avatar-settings.json";
    const auto old=preferences("old",true),next=preferences("new",false);
    if(which==L"create-only"){
        begin(Mode::Real,MOVEFILE_WRITE_THROUGH);need(evrm::create_avatar_preferences(file,old),"first create-only commit failed");
        need(moves==1,"first create-only commit repeated rename");persisted(file,old);
        const auto old_bytes=inspect_temporary(file.c_str()).bytes;
        begin(Mode::Real,MOVEFILE_WRITE_THROUGH);need(!evrm::create_avatar_preferences(file,next),"create-only commit replaced an existing file");
        need(moves==1,"existing create-only target was retried");need(inspect_temporary(file.c_str()).bytes==old_bytes,"create-only failure changed old JSON bytes");persisted(file,old);no_temporary(root);
    }else{
        need(evrm::write_avatar_preferences(file,old),"fixture initial commit failed");
        const auto old_bytes=inspect_temporary(file.c_str()).bytes;
        if(which==L"transient-reader"){
            reader=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            need(reader!=INVALID_HANDLE_VALUE,"production-flags reader open");begin(Mode::ReaderThenRelease);
            need(evrm::write_avatar_preferences(file,next),"transient reader denial was not retried");
            need(first_error==ERROR_ACCESS_DENIED&&moves>=2&&moves<=8,"real reader-denial recovery or bounded attempts missing");persisted(file,next);no_temporary(root);
        }else if(which==L"permanent-denial"){
            begin(Mode::Denied);need(!evrm::write_avatar_preferences(file,next),"permanent denial reported success");
            need(moves>=1&&moves<=8,"permanent denial exceeded bounded rename attempts");need(inspect_temporary(file.c_str()).bytes==old_bytes,"permanent denial changed old JSON bytes");persisted(file,old);no_temporary(root);
        }else if(which==L"readonly"){
            const auto attributes=GetFileAttributesW(file.c_str());need(attributes!=INVALID_FILE_ATTRIBUTES,"readonly fixture attributes");
            need(SetFileAttributesW(file.c_str(),attributes|FILE_ATTRIBUTE_READONLY)!=FALSE,"readonly fixture setup");
            begin(Mode::Real);const auto saved=evrm::write_avatar_preferences(file,next);
            need(SetFileAttributesW(file.c_str(),attributes)!=FALSE,"readonly fixture restoration");
            need(!saved&&moves<=1,"readonly target retried or reported saved");need(inspect_temporary(file.c_str()).bytes==old_bytes,"readonly failure changed old JSON bytes");persisted(file,old);no_temporary(root);
        }else if(which==L"other-errors"){
            for(const auto error:{DWORD(ERROR_SHARING_VIOLATION),DWORD(ERROR_LOCK_VIOLATION),DWORD(ERROR_WRITE_PROTECT),DWORD(ERROR_INVALID_PARAMETER)}){
                injected_error=error;begin(Mode::OtherError);need(!evrm::write_avatar_preferences(file,next),"unproven error reported success");
                need(moves==1,"unproven error class was retried");need(inspect_temporary(file.c_str()).bytes==old_bytes,"other error changed old JSON bytes");persisted(file,old);no_temporary(root);
            }
        }else if(which==L"changed-path"){
            begin(Mode::DirectoryAfterDenial);need(!evrm::write_avatar_preferences(file,next),"changed path guard accepted retry");
            need(moves==1,"changed directory target reached another rename");need(inspect_temporary(displaced.c_str()).bytes==old_bytes,"path guard failure changed retained JSON bytes");persisted(displaced,old);no_temporary(root);
            need(RemoveDirectoryW(file.c_str())!=FALSE&&real_move(displaced.c_str(),file.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE,"path-guard fixture restoration");persisted(file,old);
        }else need(false,"unknown case");
    }
    std::printf("PASS atomic avatar settings retry case; rename-attempts=%u\n",moves);return 0;
}
