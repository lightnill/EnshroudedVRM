// Production adapter is separate from the deterministic fixture evaluator.
// Only actual current-process native attestation can publish global readiness.
#include "standalone_startup.hpp"
#include "standalone_files.hpp"
#include "native_owner_process.hpp"
#include "enshrouded_bridge.hpp"
#include <shlobj.h>
#include <array>

namespace evrm {
const RuntimeStartupContext& runtime_startup_context()noexcept {
    static const RuntimeStartupContext context=[]()noexcept {
        RuntimeStartupInput input;
        try {
            std::array<wchar_t,32768> executable{},module{};HMODULE own=nullptr;
            const auto exe_count=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
            if(!exe_count || exe_count>=executable.size() || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&runtime_startup_context),&own))return RuntimeStartupContext{};
            const auto module_count=GetModuleFileNameW(own,module.data(),DWORD(module.size()));
            if(!module_count || module_count>=module.size())return RuntimeStartupContext{};
            input.executable_path=executable.data();input.module_path=module.data();
            input.disabled=GetEnvironmentVariableW(L"ENSHROUDED_VRM_DISABLE",nullptr,0)>1;
            if(input.disabled || !detail::same_standalone_path(input.executable_path.filename(),L"enshrouded.exe"))return evaluate_runtime_startup(input,nullptr);
            const NativeOwnerProcess process;
            if(!process.supported())return evaluate_runtime_startup(input,nullptr);
            // Known-folder resolution is deferred, and only for an attested game.
            // Inherited LOCALAPPDATA/ENSHROUDED_VRM paths cannot redirect writes.
            PWSTR local=nullptr;
            if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)) && local)input.user_data_directory=local;
            CoTaskMemFree(local);
            auto checked=evaluate_runtime_startup(input,&process.image());
            if(checked.ready()) {
                const char hex[]="0123456789abcdef";std::string digest;digest.reserve(64);
                for(const auto byte:checked.owner.sha256){digest+=hex[byte>>4];digest+=hex[byte&15];}
                auto bridge=std::make_shared<const EnshroudedBridge>(checked.profile_path,checked.catalog_path,digest);
                if(!bridge->ready()){checked.status=RuntimeStartupStatus::Declined;checked.reason="compatibility-invalid";checked.owner={};checked.lease.reset();}
                else checked.bridge=std::move(bridge);
            }
            return checked;
        }catch(...){RuntimeStartupContext failed;failed.status=RuntimeStartupStatus::Declined;failed.reason="unavailable";return failed;}
    }();
    return context;
}
}
