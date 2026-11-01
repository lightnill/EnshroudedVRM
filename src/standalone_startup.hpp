#pragma once
#include "native_owner.hpp"
#include "standalone_settings.hpp"
#include <filesystem>
#include <memory>

namespace evrm {
class EnshroudedBridge;
enum class RuntimeStartupStatus {Dormant,Ready,Declined};
struct RuntimeStartupInput {
    std::filesystem::path module_path,executable_path,user_data_directory;
    bool disabled=false;
};
struct RuntimePackageLease;
struct RuntimeStartupContext {
    RuntimeStartupStatus status=RuntimeStartupStatus::Dormant;
    std::string reason="unconfigured",version;
    NativeOwnerImage owner{};
    std::filesystem::path game_directory,mod_directory,runtime_directory,profile_path,catalog_path,avatars_directory;
    std::filesystem::path state_directory,log_path,capture_directory;
    SettingsImportResult imports{};
    // Diagnostic eligibility comes from verified native owner input. It never
    // admits hooks/services; only ready() grants that admission.
    bool diagnostic_owner_verified=false,state_writable=false;
    std::size_t verified_files=0,hashed_bytes=0;
    std::uint64_t validation_microseconds=0;
    std::shared_ptr<const RuntimePackageLease> lease;
    std::shared_ptr<const EnshroudedBridge> bridge;
    bool ready()const noexcept{return status==RuntimeStartupStatus::Ready;}
};
// The supplied proof is a local deterministic evaluator input only. It cannot
// publish process readiness or attest a loaded image. Production obtains proof
// privately through NativeOwnerProcess in runtime_startup_context().
RuntimeStartupContext evaluate_runtime_startup(const RuntimeStartupInput&,const NativeOwnerImage* verified_native_owner)noexcept;
const RuntimeStartupContext& runtime_startup_context()noexcept;
void initialize_runtime_startup_environment()noexcept;
}
