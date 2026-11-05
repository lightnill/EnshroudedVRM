#include "input_startup.hpp"
#include "input_proxy.hpp"
#include "input_trigger_profile.hpp"
#include "standalone_bootstrap_view.hpp"
#include "sha256.hpp"
#include <intrin.h>
#include <cstring>

namespace enshrouded::vrm {
namespace {
namespace view=bootstrap_view;
HMODULE own_module=nullptr;
DWORD attach_thread=0;
PVOID volatile* command_slot=nullptr;
PVOID original_command=nullptr;
const unsigned char* expected_return=nullptr;
wchar_t chain_path[32768]{},game_path[32768]{},bootstrap_path[32768]{};
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
bool game_matches(HANDLE file) noexcept {
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<=0||size.QuadPart>128*1024*1024)return false;
    const auto mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);
    if(!mapping)return false;
    const auto bytes=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);
    bool valid=false;
    if(bytes) {
        const auto digest=evrm::detail::sha256_portable(bytes,static_cast<std::size_t>(size.QuadPart));
        valid=true;
        for(unsigned i=0;i<32;++i)if(digest[i]!=input_trigger::game_sha256[i])valid=false;
        UnmapViewOfFile(bytes);
    }
    CloseHandle(mapping);return valid;
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
    if(!lock_parents(game_path,admission_locks,admission_count,125,false)) {status("game_path_refused",GetLastError());return;}
    HANDLE game=nullptr;
    if(!checked_open(game_path,false,false,game)){status("game_open_failed",GetLastError());return;}
    admission_locks[admission_count++]=game;
    if(!game_matches(game)){status("game_hash_mismatch",ERROR_REVISION_MISMATCH);return;}
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
LPWSTR WINAPI command_hook();
PVOID hook_address() noexcept {
    const auto function=&command_hook;PVOID address=nullptr;
    static_assert(sizeof(address)==sizeof(function));std::memcpy(&address,&function,sizeof(address));return address;
}
bool exchange_slot(PVOID expected,PVOID replacement) noexcept {
    if(!command_slot)return false;
    DWORD previous=0;
    if(!VirtualProtect(const_cast<PVOID*>(command_slot),sizeof(PVOID),PAGE_READWRITE,&previous))return false;
    const auto old=InterlockedCompareExchangePointer(command_slot,replacement,expected);
    DWORD ignored=0;VirtualProtect(const_cast<PVOID*>(command_slot),sizeof(PVOID),previous,&ignored);
    return old==expected;
}
__declspec(noinline) LPWSTR WINAPI command_hook() {
    const auto saved=GetLastError();
    if(_ReturnAddress()==expected_return&&GetCurrentThreadId()==attach_thread) {
        // Remove our gate before any downstream DllMain can call this API.
        // Never restore over a hook installed by another component.
        if(exchange_slot(hook_address(),original_command))deferred_chain();
    }
    SetLastError(saved);return GetCommandLineW();
}
bool main_imports_input(const unsigned char* base,const IMAGE_NT_HEADERS64& nt) noexcept {
    const auto size=nt.OptionalHeader.SizeOfImage;
    const auto imports=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!imports.VirtualAddress||imports.Size<sizeof(IMAGE_IMPORT_DESCRIPTOR)||imports.Size>size||imports.VirtualAddress>size-imports.Size)return false;
    const auto entries=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+imports.VirtualAddress);
    for(unsigned i=0;i<imports.Size/sizeof(*entries)&&entries[i].Name;++i) {
        const auto name=entries[i].Name;
        constexpr char target[]="dinput8.dll";
        if(name>size-sizeof(target))continue;
        bool same=true;
        for(unsigned j=0;j<sizeof(target);++j) {
            auto c=base[name+j];if(c>='A'&&c<='Z')c+=32;
            if(c!=static_cast<unsigned char>(target[j]))same=false;
        }
        if(same)return true;
    }
    return false;
}
}

void input_startup_record(InputBootstrapPhase phase,InputBootstrapResult result,DWORD error) noexcept {
    // Called only during process attach under the loader lock. The safe gate
    // runs later on that same thread; no dynamic initialization or I/O here.
    attach_record={phase,result,error};
}
void input_startup_attach(HMODULE own,const wchar_t* module,DWORD module_length,
                          const wchar_t* executable,DWORD executable_length) noexcept {
    if(!input_trigger::enabled)return;
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
    const auto base=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<sizeof(*dos)||dos->e_lfanew>1024*1024)return;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt->FileHeader.TimeDateStamp!=input_trigger::timestamp||
       nt->OptionalHeader.SizeOfImage!=input_trigger::image_size||
       input_trigger::iat_rva>input_trigger::image_size-sizeof(PVOID)||input_trigger::return_rva<6||
       input_trigger::return_rva>=input_trigger::image_size||!main_imports_input(base,*nt))return;
    const auto call=base+input_trigger::return_rva-6;
    std::int32_t displacement=0;std::memcpy(&displacement,call+2,sizeof(displacement));
    if(call[0]!=0xff||call[1]!=0x15||
       static_cast<std::int64_t>(input_trigger::return_rva)+displacement!=input_trigger::iat_rva)return;
    const auto original=&GetCommandLineW;std::memcpy(&original_command,&original,sizeof(original_command));
    command_slot=reinterpret_cast<PVOID volatile*>(const_cast<unsigned char*>(base)+input_trigger::iat_rva);
    if(reinterpret_cast<ULONG_PTR>(command_slot)%alignof(PVOID)) {command_slot=nullptr;return;}
    own_module=own;attach_thread=GetCurrentThreadId();expected_return=base+input_trigger::return_rva;
    for(DWORD i=0;i<=executable_length;++i)game_path[i]=executable[i];
    for(DWORD i=0;i<=module_length;++i)bootstrap_path[i]=module[i];
    if(!exchange_slot(original_command,hook_address()))command_slot=nullptr;
}
void input_startup_detach() noexcept {
    if(command_slot)exchange_slot(hook_address(),original_command);
    // Providers may have installed live hooks in their attach. Do not unload
    // them, free routing TLS or close their backing-file locks in DllMain.
}
}
