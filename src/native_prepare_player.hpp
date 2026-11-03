#pragma once
#include "native_publication.hpp"
#include "native_pose_owner.hpp"
namespace evrm {
// This is a role selector, not rendering authority. The caller must supply the
// final CPU permit after current account/source/parts/pose validation. The GPU
// still checks all native-use, instance/model/skin, transform and draw witnesses.
// LightingOwner's view anchor remains the local player's position in the game;
// it cannot additionally be the position of an independently bound remote actor.
inline float native_prepare_remote_actor_mode(const NativeAvatarPublication* publication,
    const NativeOwnerCandidate& current,bool final_permit,bool menu_preview)noexcept {
    if(!final_permit||menu_preview||!publication||publication->subject.empty()||
       !publication->pose_owner||publication->player_identity.local)return 0.f;
    const auto& id=publication->player_identity;const auto& proposed=publication->pose_owner->candidate;
    if(!id.account||!id.entity_id||!id.connection)return 0.f;
    return current.account==id.account&&current.entity_id==id.entity_id&&current.connection==id.connection&&
        proposed.account==id.account&&proposed.entity_id==id.entity_id&&proposed.connection==id.connection?1.f:0.f;
}
}
