// EnshroudedVRM bootstrap. The Windows loader resolves PE forwarding exports;
// no manual system DLL resolver runs under this or a dependent DLL's DllMain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "standalone_bootstrap_policy.hpp"
#include "standalone_bootstrap_manifest.hpp"

namespace {
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
bool checked_handle(const wchar_t* path, bool directory, HANDLE& output) noexcept {
    const auto handle=CreateFileW(path,directory ? FILE_READ_ATTRIBUTES : GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0),nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    static wchar_t final_path[32768];
    const auto size=GetFinalPathNameByHandleW(handle,final_path,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    auto normalized=std::wstring_view(final_path,size < 32768 ? size : 0);
    if (normalized.substr(0,4) == L"\\\\?\\") normalized.remove_prefix(4);
    auto expected=std::wstring_view(path);
    if (expected.substr(0,4) == L"\\\\?\\") expected.remove_prefix(4);
    const bool valid=GetFileInformationByHandle(handle,&info) && size && size < 32768 &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory && (directory || info.nNumberOfLinks == 1) &&
        normalized.size() == expected.size() && CompareStringOrdinal(normalized.data(),static_cast<int>(normalized.size()),expected.data(),static_cast<int>(expected.size()),TRUE) == CSTR_EQUAL;
    if (!valid) { CloseHandle(handle); return false; }
    output=handle; return true;
}
bool lock_manifest(const enshrouded::vrm::BootstrapPlan& plan, std::size_t root_end) noexcept {
    static wchar_t path[32768];
    for (std::size_t i=0;i<=plan.manifest_length;++i) path[i]=plan.manifest[i];
    std::size_t end=root_end;
    for (std::size_t index=0;index<3;++index) {
        const auto saved=path[end]; path[end]=0;
        const bool valid=checked_handle(path,true,manifest_locks[index]); path[end]=saved;
        if (!valid) { release_manifest(); return false; }
        if (index < 2) {
            end=std::wstring_view(path,plan.manifest_length).find(L'\\',end+1);
            if (end == std::wstring_view::npos) { release_manifest(); return false; }
        }
    }
    if (!checked_handle(path,false,manifest_locks[3])) { release_manifest(); return false; }
    LARGE_INTEGER size{}; static unsigned char bytes[2048]; DWORD read=0;
    bool valid=GetFileSizeEx(manifest_locks[3],&size) && size.QuadPart == sizeof(expected_manifest) &&
        ReadFile(manifest_locks[3],bytes,sizeof(expected_manifest),&read,nullptr) && read == sizeof(expected_manifest);
    for (std::size_t i=0;valid && i<sizeof(expected_manifest);++i) valid=bytes[i] == expected_manifest[i];
    if (!valid) release_manifest();
    return valid;
}
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) { release_manifest(); return TRUE; }
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    const auto saved_error = GetLastError();
    // Fixed storage only. The sole file read is the exact <=2 KiB manifest;
    // full package/build verification remains deferred outside loader lock.
    static wchar_t executable[32768], own_path[32768], inherited[32768];
    const auto exe_length = GetModuleFileNameW(nullptr, executable, 32768);
    const auto own_length = GetModuleFileNameW(module, own_path, 32768);
    const auto existing_length = GetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH", inherited, 32768);
    if (exe_length && exe_length < 32768 && own_length && own_length < 32768 && existing_length < 32768) {
        const bool disabled = GetEnvironmentVariableW(L"ENSHROUDED_VRM_DISABLE", nullptr, 0) > 1;
        SetLastError(ERROR_SUCCESS);
        const auto override_length = GetEnvironmentVariableW(L"VK_IMPLICIT_LAYER_PATH", nullptr, 0);
        const bool search_override = override_length || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        static enshrouded::vrm::BootstrapPlan plan;
        enshrouded::vrm::prepare_bootstrap_paths({own_path,own_length}, {executable,exe_length}, {inherited,existing_length}, disabled, search_override, plan);
        if ((plan.activate || plan.reason == enshrouded::vrm::BootstrapReason::already_present) &&
            lock_manifest(plan,std::wstring_view(own_path,own_length).rfind(L'\\'))) {
            if (plan.activate && !SetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH", plan.paths)) release_manifest();
        }
    }
    SetLastError(saved_error);
    return TRUE;
}
