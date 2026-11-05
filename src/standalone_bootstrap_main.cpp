// EnshroudedVRM bootstrap. The Windows loader resolves PE forwarding exports;
// no manual system DLL resolver runs under this or a dependent DLL's DllMain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "standalone_bootstrap_policy.hpp"
#include "standalone_bootstrap_view.hpp"
#include "standalone_bootstrap_manifest.hpp"
#ifdef EVRM_INPUT_PROXY
#include "input_startup.hpp"
#endif

namespace {
namespace view=enshrouded::vrm::bootstrap_view;
// Windows invokes this DLL on other components' small worker stacks too.
// Keep the bounded attach workspace out of every callback's stack frame.
// These constant-initialized statics are accessed only under the loader lock;
// thread callbacks return without using them. No heap, TLS or extra lock.
// Deny replacement of the checked manifest and its immediate installation
// directories until detach. No workers, parser, hash, writes or retry loop.
HANDLE manifest_locks[4]{};
void release_manifest() noexcept {
    for (auto& handle : manifest_locks) { if (handle) CloseHandle(handle); handle=nullptr; }
}
bool refuse_manifest(DWORD error) noexcept {
    release_manifest();SetLastError(error);return false;
}
#ifdef EVRM_INPUT_PROXY
void record_policy(enshrouded::vrm::BootstrapReason reason) noexcept {
    using namespace enshrouded::vrm;
    auto result=InputBootstrapResult::wrong_process;
    switch(reason) {
    case BootstrapReason::wrong_process:result=InputBootstrapResult::wrong_process;break;
    case BootstrapReason::ready:result=InputBootstrapResult::ready;break;
    case BootstrapReason::wrong_directory:result=InputBootstrapResult::wrong_directory;break;
    case BootstrapReason::unsafe_path:result=InputBootstrapResult::unsafe_path;break;
    case BootstrapReason::disabled:result=InputBootstrapResult::disabled;break;
    case BootstrapReason::search_override:result=InputBootstrapResult::search_override;break;
    case BootstrapReason::too_long:result=InputBootstrapResult::too_long;break;
    case BootstrapReason::already_present:result=InputBootstrapResult::already_present;break;
    }
    input_startup_record(InputBootstrapPhase::policy,result,ERROR_SUCCESS);
}
#endif
bool checked_handle(const wchar_t* path, bool directory, HANDLE& output,bool immutable=false) noexcept {
    const auto handle=CreateFileW(path,directory ? FILE_READ_ATTRIBUTES : GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0),nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    static wchar_t final_path[32768];
    const auto size=GetFinalPathNameByHandleW(handle,final_path,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    auto normalized=std::wstring_view(final_path,size < 32768 ? size : 0);
    if (normalized.size()>=4 && normalized[0]==L'\\' && normalized[1]==L'\\' && normalized[2]==L'?' && normalized[3]==L'\\') normalized.remove_prefix(4);
    auto expected=view::bounded_z(path,32768);
    if (expected.size()>=4 && expected[0]==L'\\' && expected[1]==L'\\' && expected[2]==L'?' && expected[3]==L'\\') expected.remove_prefix(4);
    const bool valid=GetFileInformationByHandle(handle,&info) && size && size < 32768 &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory && (directory || immutable || info.nNumberOfLinks == 1) &&
        normalized.size() == expected.size() && CompareStringOrdinal(normalized.data(),static_cast<int>(normalized.size()),expected.data(),static_cast<int>(expected.size()),TRUE) == CSTR_EQUAL;
    if (!valid) { CloseHandle(handle);SetLastError(ERROR_ACCESS_DENIED);return false; }
    output=handle; return true;
}
bool lock_manifest(const enshrouded::vrm::BootstrapPlan& plan, std::size_t root_end) noexcept {
    static wchar_t path[32768];
    for (std::size_t i=0;i<=plan.manifest_length;++i) path[i]=plan.manifest[i];
    std::size_t end=root_end;
    for (std::size_t index=0;index<3;++index) {
        const auto saved=path[end]; path[end]=0;
        const bool valid=checked_handle(path,true,manifest_locks[index]); path[end]=saved;
        if (!valid) return refuse_manifest(GetLastError());
        if (index < 2) {
            end=view::find({path,plan.manifest_length},L'\\',end+1);
            if (end == std::wstring_view::npos) return refuse_manifest(ERROR_INVALID_DATA);
        }
    }
#ifdef EVRM_INPUT_PROXY
    constexpr bool immutable_package=true;
#else
    constexpr bool immutable_package=false;
#endif
    if (!checked_handle(path,false,manifest_locks[3],immutable_package)) return refuse_manifest(GetLastError());
    LARGE_INTEGER size{}; static unsigned char bytes[2048]; DWORD read=0;
    if(!GetFileSizeEx(manifest_locks[3],&size))return refuse_manifest(GetLastError());
    if(size.QuadPart!=sizeof(expected_manifest))return refuse_manifest(ERROR_INVALID_DATA);
    if(!ReadFile(manifest_locks[3],bytes,sizeof(expected_manifest),&read,nullptr))return refuse_manifest(GetLastError());
    if(read!=sizeof(expected_manifest))return refuse_manifest(ERROR_INVALID_DATA);
    for(std::size_t i=0;i<sizeof(expected_manifest);++i)
        if(bytes[i]!=expected_manifest[i])return refuse_manifest(ERROR_INVALID_DATA);
    return true;
}
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
#ifdef EVRM_INPUT_PROXY
        enshrouded::vrm::input_startup_detach();
#endif
        release_manifest(); return TRUE;
    }
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    const auto saved_error = GetLastError();
    // Fixed storage only. The sole file read is the exact <=2 KiB manifest;
    // full package/build verification remains deferred outside loader lock.
    static wchar_t executable[32768], own_path[32768], inherited[32768];
    const auto exe_length = GetModuleFileNameW(nullptr, executable, 32768);
#ifdef EVRM_INPUT_PROXY
    const auto exe_error=GetLastError();
#endif
    const auto own_length = GetModuleFileNameW(module, own_path, 32768);
#ifdef EVRM_INPUT_PROXY
    const auto own_error=GetLastError();
#endif
    const auto existing_length = GetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH", inherited, 32768);
#ifdef EVRM_INPUT_PROXY
    enshrouded::vrm::input_startup_record(enshrouded::vrm::InputBootstrapPhase::paths,
        enshrouded::vrm::InputBootstrapResult::path_query_failed,
        !exe_length?exe_error:!own_length?own_error:ERROR_INSUFFICIENT_BUFFER);
#endif
    if (exe_length && exe_length < 32768 && own_length && own_length < 32768 && existing_length < 32768) {
        const bool disabled = GetEnvironmentVariableW(L"ENSHROUDED_VRM_DISABLE", nullptr, 0) > 1;
        SetLastError(ERROR_SUCCESS);
        const auto override_length = GetEnvironmentVariableW(L"VK_IMPLICIT_LAYER_PATH", nullptr, 0);
        const bool search_override = override_length || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        static enshrouded::vrm::BootstrapPlan plan;
        enshrouded::vrm::prepare_bootstrap_paths({own_path,own_length}, {executable,exe_length}, {inherited,existing_length}, disabled, search_override, plan);
#ifdef EVRM_INPUT_PROXY
        record_policy(plan.reason);
#endif
        if(plan.activate || plan.reason==enshrouded::vrm::BootstrapReason::already_present) {
            if(!lock_manifest(plan,view::rfind({own_path,own_length},L'\\'))) {
#ifdef EVRM_INPUT_PROXY
                enshrouded::vrm::input_startup_record(enshrouded::vrm::InputBootstrapPhase::manifest,
                    enshrouded::vrm::InputBootstrapResult::manifest_refused,GetLastError());
#endif
            }else if(plan.activate && !SetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH",plan.paths)) {
#ifdef EVRM_INPUT_PROXY
                enshrouded::vrm::input_startup_record(enshrouded::vrm::InputBootstrapPhase::environment,
                    enshrouded::vrm::InputBootstrapResult::environment_failed,GetLastError());
#endif
                release_manifest();
            }else {
#ifdef EVRM_INPUT_PROXY
                enshrouded::vrm::input_startup_record(enshrouded::vrm::InputBootstrapPhase::ready,
                    plan.activate?enshrouded::vrm::InputBootstrapResult::ready:enshrouded::vrm::InputBootstrapResult::already_present,
                    ERROR_SUCCESS);
#endif
            }
        }
    }
#ifdef EVRM_INPUT_PROXY
    if(exe_length && exe_length<32768 && own_length && own_length<32768)
        enshrouded::vrm::input_startup_attach(module,own_path,own_length,executable,exe_length);
#endif
    SetLastError(saved_error);
    return TRUE;
}
