#pragma once
#include "avatar_service.hpp"
namespace evrm {
// Every device can prepare a proposal, then reconcile to the coherent committed
// selection if another device completed it first.
inline AvatarCandidatePtr avatar_device_request(AvatarService& service,const std::shared_ptr<const PreparedAvatar>& current,const std::string& subject={},bool device_ready=true)noexcept {
    if(!service.replacement_enabled(subject))return {};
    if(auto pending=service.candidate(subject))return pending;
    const auto selected=service.selection(subject);return selected&&(selected->model!=current||!device_ready)?selected:nullptr;
}
inline bool activate_avatar_device(AvatarService& service,const AvatarCandidatePtr& request,
    bool(*activate)(void*,const AvatarCandidate&)noexcept,void* context)noexcept {
    return service.commit_candidate(request,activate,context)||service.adopt_selection(request,activate,context);
}
}
