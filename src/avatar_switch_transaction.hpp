#pragma once
#include "avatar_model.hpp"
#include "native_publication.hpp"
namespace evrm {
inline bool avatar_switch_pose_matches(const std::shared_ptr<const NativeAvatarPublication>& root,
    const PreparedAvatar& model,std::uint64_t epoch,std::uint64_t now)noexcept {
    const auto matches=[&](const NativeAvatarPublication& view){return
        model.asset&&model.skin&&view.asset==model.asset&&view.skinning&&view.skinning->asset==model.skin&&
        view.geometry==model.skin->layout&&view.serial&&epoch&&view.source_buffer_epoch==epoch&&
        view.source_instances&&view.published_milliseconds&&now>=view.published_milliseconds&&now-view.published_milliseconds<=250;};
    if(!root||root->additional_views.size()>3||!matches(*root))return false;
    for(const auto& child:root->additional_views)if(!child||!child->additional_views.empty()||!matches(*child)||
        native_publication_view(root,child->source_instances)!=child)return false;
    return native_publication_view(root,root->source_instances)==root;
}
}
