#pragma once
#include "avatar_service_types.hpp"
#include "avatar_model.hpp"
namespace evrm {
struct AvatarCandidate {std::uint64_t generation=0;std::shared_ptr<const PreparedAvatar> model;};
using AvatarCandidatePtr=std::shared_ptr<const AvatarCandidate>;
struct AvatarServiceOptions {
    std::filesystem::path directory,catalogue_directory;
    std::shared_ptr<const PreparedAvatar> initial;
    AvatarLoadResult (*loader)(const std::string&,void*)=nullptr;
    AvatarPickResult (*picker)(std::uintptr_t,void*)=nullptr;
    AvatarSettingsLoad (*preferences_reader)(const std::filesystem::path&,void*)=nullptr;
    bool (*input_released)(std::uintptr_t,void*)noexcept=nullptr;
    void* context=nullptr;
};
class AvatarService {
public:
    explicit AvatarService(AvatarServiceOptions)noexcept;
    ~AvatarService();
    bool ready()const noexcept;
    std::shared_ptr<const AvatarMenuSnapshot> snapshot()const noexcept;
    std::shared_ptr<const PreparedAvatar> active()const noexcept;
    AvatarCandidatePtr candidate()const noexcept;
    AvatarCandidatePtr selection()const noexcept;
    std::uint64_t desired_generation()const noexcept;
    bool request_model(const std::string&)noexcept;
    bool request_browse(std::uintptr_t)noexcept;
    void cancel_request()noexcept;
    void refresh_catalogue()noexcept;
    bool set_whole_preset(const std::string&,AvatarShaderPreset)noexcept;
    bool set_material_preset(const std::string&,std::uint32_t,std::optional<AvatarShaderPreset>)noexcept;
    bool reset_materials(const std::string&)noexcept;
    bool commit_candidate(const AvatarCandidatePtr&,bool(*)(void*,const AvatarCandidate&)noexcept,void*)noexcept;
    bool adopt_selection(const AvatarCandidatePtr&,bool(*)(void*,const AvatarCandidate&)noexcept,void*)noexcept;
    void fail_candidate(std::uint64_t,const std::string&)noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
