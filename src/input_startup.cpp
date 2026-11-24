#include "input_startup.hpp"
#include "input_proxy.hpp"
#include "standalone_bootstrap_view.hpp"

namespace enshrouded::vrm {
namespace {
namespace view=bootstrap_view;
HMODULE own_module=nullptr;
wchar_t chain_path[32768]{},bootstrap_path[32768]{};
// Retain the admitted files/directories and loaded provider until process exit.
// None of these objects needs a dynamic initializer or TLS callback.
HANDLE admission_locks[128]{};
unsigned admission_count=0;
HMODULE chain_module=nullptr;
wchar_t checked_final[32768]{};
struct AttachRecord {
    InputBootstrapPhase phase;
    InputBootstrapResult result;
    DWORD error;
};
AttachRecord attach_record{};

bool equal(std::wstring_view a,std::wstring_view b) noexcept {
    return a.size()==b.size() && CompareStringOrdinal(a.data(),static_cast<int>(a.size()),
        b.data(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
bool local_path(std::wstring_view path) noexcept {
    if(path.size()<4 || path[1]!=L':' || path[2]!=L'\\' ||
       !((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z')))return false;
    for(std::size_t start=3;start<path.size();) {
        auto end=view::find(path,L'\\',start);if(end==path.npos)end=path.size();
        const std::wstring_view part(path.data()+start,end-start);
        if(part.empty() || part.back()==L'.' || part.back()==L' ')return false;
        for(auto c:part)if(c<32||c==L'/'||c==L':'||c==L'"'||c==L'<'||c==L'>'||c==L'|'||c==L'?'||c==L'*')return false;
        start=end+1;
    }
    return true;
}
bool checked_open(const wchar_t* path,bool directory,bool unique,HANDLE& output) noexcept {
    const auto handle=CreateFileW(path,directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
    if(handle==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};
    const auto count=GetFinalPathNameByHandleW(handle,checked_final,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    auto final=view::bounded_z(checked_final,count<32768?count+1:0);
    if(final.size()>=4 && final[0]==L'\\' && final[1]==L'\\' && final[2]==L'?' && final[3]==L'\\')final.remove_prefix(4);
    const auto expected=view::bounded_z(path,32768);
    const bool valid=count&&count<32768 && GetFileInformationByHandle(handle,&info) &&
        !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
        bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory &&
        (!unique||directory||info.nNumberOfLinks==1) && equal(final,expected);
    if(!valid){CloseHandle(handle);SetLastError(ERROR_ACCESS_DENIED);return false;}
    output=handle;return true;
}
void close_locks(HANDLE* locks,unsigned& count) noexcept {
    while(count) {CloseHandle(locks[--count]);locks[count]=nullptr;}
}
bool lock_parents(wchar_t* path,HANDLE* locks,unsigned& count,unsigned capacity,bool create) noexcept {
    const auto text=view::bounded_z(path,32768);
    if(!local_path(text))return false;
    for(std::size_t i=3;i<text.size();++i)if(path[i]==L'\\') {
        if(count==capacity)return false;
        path[i]=0;
        if(create)CreateDirectoryW(path,nullptr);
        const bool valid=checked_open(path,true,false,locks[count]);
        path[i]=L'\\';
        if(!valid)return false;
        ++count;
    }
    return true;
}
bool valid_chain_image(HANDLE file) noexcept {
    LARGE_INTEGER size{};IMAGE_DOS_HEADER dos{};DWORD read=0;
    if(!GetFileSizeEx(file,&size)||size.QuadPart<sizeof(IMAGE_NT_HEADERS64)||size.QuadPart>64*1024*1024||
       !ReadFile(file,&dos,sizeof(dos),&read,nullptr)||read!=sizeof(dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||
       dos.e_lfanew<sizeof(dos)||dos.e_lfanew>size.QuadPart-static_cast<LONGLONG>(sizeof(IMAGE_NT_HEADERS64)))return false;
    LARGE_INTEGER offset{};offset.QuadPart=dos.e_lfanew;IMAGE_NT_HEADERS64 nt{};
    return SetFilePointerEx(file,offset,nullptr,FILE_BEGIN) && ReadFile(file,&nt,sizeof(nt),&read,nullptr)&&read==sizeof(nt)&&
        nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&
        (nt.FileHeader.Characteristics&IMAGE_FILE_DLL)&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC;
}
const char* phase_name(InputBootstrapPhase phase) noexcept {
    switch(phase) {
    case InputBootstrapPhase::paths:return "paths";
    case InputBootstrapPhase::policy:return "policy";
    case InputBootstrapPhase::manifest:return "manifest";
    case InputBootstrapPhase::environment:return "environment";
    case InputBootstrapPhase::ready:return "ready";
    default:return "not_started";
    }
}
const char* bootstrap_name(InputBootstrapResult result) noexcept {
    switch(result) {
    case InputBootstrapResult::path_query_failed:return "path_query_failed";
    case InputBootstrapResult::wrong_process:return "wrong_process";
    case InputBootstrapResult::ready:return "ready";
    case InputBootstrapResult::wrong_directory:return "wrong_directory";
    case InputBootstrapResult::unsafe_path:return "unsafe_path";
    case InputBootstrapResult::disabled:return "disabled";
    case InputBootstrapResult::search_override:return "search_override";
    case InputBootstrapResult::too_long:return "too_long";
    case InputBootstrapResult::already_present:return "already_present";
    case InputBootstrapResult::manifest_refused:return "manifest_refused";
    case InputBootstrapResult::environment_failed:return "environment_failed";
    default:return "not_started";
    }
}
void status(const char* reason,DWORD error) noexcept {
    // A fresh unique file avoids truncating an existing file or following a
    // writable hardlink. Hold all local parent directories during creation.
    static wchar_t path[32768];
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return;
    const auto count=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
    constexpr auto suffix=view::literal(L"\\EnshroudedVRM\\State\\Logs\\input-bootstrap-");
    if(!count||count+suffix.size()+48>=32768)return;
    std::size_t length=count;
    for(auto c:suffix)path[length++]=c;
    wchar_t digits[10];unsigned used=0;auto pid=GetCurrentProcessId();
    do{digits[used++]=static_cast<wchar_t>(L'0'+pid%10);pid/=10;}while(pid&&used<10);
    while(used)path[length++]=digits[--used];
    // The process creation time separates reused PIDs without overwriting any
    // previous run or relaxing CREATE_NEW / writable-hardlink refusal.
    path[length++]=L'-';
    const DWORD times[]={created.dwHighDateTime,created.dwLowDateTime};
    for(auto value:times)for(int shift=28;shift>=0;shift-=4)
        path[length++]=L"0123456789abcdef"[(value>>shift)&15];
    for(auto c:view::literal(L".log"))path[length++]=c;
    path[length]=0;
    HANDLE parents[128]{};unsigned parents_count=0;
    if(!lock_parents(path,parents,parents_count,128,true)){close_locks(parents,parents_count);return;}
    const auto file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file!=INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info{};
        if(GetFileInformationByHandle(file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) {
            char message[384];unsigned at=0;
            const auto append=[&](const char* text){for(unsigned i=0;text[i]&&at<350;++i)message[at++]=text[i];};
            const auto number=[&](DWORD value){
                char digits[10];unsigned n=0;
                do{digits[n++]=static_cast<char>('0'+value%10);value/=10;}while(value&&n<10);
                while(n)message[at++]=digits[--n];message[at++]='\n';
            };
            append("input-bootstrap v2\nbootstrap_phase=");append(phase_name(attach_record.phase));
            append("\nbootstrap=");append(bootstrap_name(attach_record.result));
            append("\nbootstrap_win32=");number(attach_record.error);
            append(reason);append("\nwin32=");number(error);
            DWORD written=0;WriteFile(file,message,at,&written,nullptr);
        }
        CloseHandle(file);
    }
    close_locks(parents,parents_count);
}
void deferred_chain() noexcept {
    if(!lock_parents(bootstrap_path,admission_locks,admission_count,126,false)) {
        status("bootstrap_path_refused",GetLastError());return;
    }
    HANDLE bootstrap=nullptr;
    if(!checked_open(bootstrap_path,false,false,bootstrap)){status("bootstrap_open_failed",GetLastError());return;}
    admission_locks[admission_count++]=bootstrap;
    if(!checked_open(chain_path,false,false,admission_locks[admission_count])) {
        const auto error=GetLastError();
        status(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?"chain_absent":"chain_open_failed",error);return;
    }
    const auto file=admission_locks[admission_count++];
    BY_HANDLE_FILE_INFORMATION bootstrap_id{},chain_id{};
    if(!GetFileInformationByHandle(bootstrap,&bootstrap_id)||!GetFileInformationByHandle(file,&chain_id)) {
        status("chain_identity_failed",GetLastError());return;
    }
    if(bootstrap_id.dwVolumeSerialNumber==chain_id.dwVolumeSerialNumber&&
       bootstrap_id.nFileIndexHigh==chain_id.nFileIndexHigh&&bootstrap_id.nFileIndexLow==chain_id.nFileIndexLow) {
        status("chain_is_bootstrap",ERROR_INVALID_FUNCTION);return;
    }
    if(!valid_chain_image(file)){status("chain_image_refused",ERROR_BAD_EXE_FORMAT);return;}
    chain_module=LoadLibraryExW(chain_path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!chain_module){status("chain_load_failed",GetLastError());return;}
    static wchar_t mapped_path[32768];
    const auto count=GetModuleFileNameW(chain_module,mapped_path,32768);
    if(!count||count>=32768||!equal({mapped_path,count},view::bounded_z(chain_path,32768))) {
        status("chain_mapped_path_mismatch",ERROR_INVALID_DATA);return;
    }
    const auto result=input_proxy_publish_chain(chain_module,own_module);
    switch(result) {
    case InputChainResult::active:status("chain_active",ERROR_SUCCESS);break;
    case InputChainResult::self_reference:status("chain_self_reference",ERROR_INVALID_FUNCTION);break;
    case InputChainResult::tls_unavailable:status("chain_tls_unavailable",ERROR_NOT_ENOUGH_MEMORY);break;
    }
}
DWORD WINAPI start_chain(void*) noexcept {
    deferred_chain();
    // A provider whose entry point has run may still call the proxy later.
    if(!chain_module)close_locks(admission_locks,admission_count);
    return 0;
}
}

void input_startup_record(InputBootstrapPhase phase,InputBootstrapResult result,DWORD error) noexcept {
    // Written during attach before the one-shot worker starts.
    attach_record={phase,result,error};
}
void input_startup_attach(HMODULE own,const wchar_t* module,DWORD module_length,
                          const wchar_t* executable,DWORD executable_length) noexcept {
    const std::wstring_view module_text(module,module_length),exe_text(executable,executable_length);
    const auto root=view::rfind(module_text,L'\\'),exe_root=view::rfind(exe_text,L'\\');
    constexpr auto suffix=view::literal(L"\\dinput8.chainload.dll");
    if(root==module_text.npos||exe_root!=root||root+suffix.size()>=32768||
       !local_path(module_text)||!local_path(exe_text)||
       !equal({module,root},{executable,root})||
       !equal({module+root+1,module_length-root-1},view::literal(L"dinput8.dll"))||
       !equal({executable+root+1,executable_length-root-1},view::literal(L"enshrouded.exe")))return;
    for(std::size_t i=0;i<root;++i)chain_path[i]=module[i];
    for(std::size_t i=0;i<suffix.size();++i)chain_path[root+i]=suffix[i];
    chain_path[root+suffix.size()]=0;
    for(DWORD i=0;i<=module_length;++i)bootstrap_path[i]=module[i];

    // The provider and its forwarding addresses have process lifetime. Keep the
    // proxy mapped even if a dynamic caller immediately drops its last handle.
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(own),&own_module))return;

    // Windows starts a new thread after process/DLL initialization completes.
    // Never wait here or in an input export: other DLLs may call those exports
    // from TLS or DllMain. Windows input remains available until publication.
    const HANDLE worker=CreateThread(nullptr,0,start_chain,nullptr,0,nullptr);
    if(worker)CloseHandle(worker);
}
}
