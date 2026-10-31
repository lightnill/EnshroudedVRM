#pragma once
#include "avatar_service.hpp"
namespace evrm {
// Every device can prepare a proposal, then reconcile to the coherent committed
// selection if another device completed it first.
inline AvatarCandidatePtr avatar_device_request(AvatarService& service,const std::shared_ptr<const PreparedAvatar>& current)noexcept {
    if(auto pending=service.candidate())return pending;
    const auto selected=service.selection();return selected&&selected->model!=current?selected:nullptr;
}
inline bool activate_avatar_device(AvatarService& service,const AvatarCandidatePtr& request,
    bool(*activate)(void*,const AvatarCandidate&)noexcept,void* context)noexcept {
    return service.commit_candidate(request,activate,context)||service.adopt_selection(request,activate,context);
}
}
