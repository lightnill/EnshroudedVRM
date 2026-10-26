#pragma once
#include "native_avatar_pose.hpp"
#include "native_skinning.hpp"
#include "tool_grip.hpp"
#include "secondary_hand_contact.hpp"
#include <memory>
#include <array>

namespace evrm {
struct NativeSecondaryContactBaseline {
    vrm::RetargetResult pose;
    std::array<ToolHandCorrection,2> hands;
};
struct NativeAvatarPublication {
    std::shared_ptr<const vrm::Avatar> asset;
    std::shared_ptr<const NativeAvatarPose> geometry;
    std::shared_ptr<const NativeSkinPose> skinning;
    std::vector<std::array<std::uint32_t,4>> parts; // ID, model, skin offset, tag
    // Candidate stamps; every CPU-owned ID must later be qualified on the GPU.
    std::vector<std::array<std::uint32_t,4>> instance_qualifiers;
    std::uint64_t source_instances=0,source_instance_offset=0,source_instance_range=0;
    std::uint64_t source_buffer_epoch=0;
    // Original source sample time, including when an unchanged pose is held.
    std::uint64_t serial=0,published_milliseconds=0;
    std::uint64_t presentation_serial=0;
    float height_scale=1,basis_xz=1,diagnostic_x=.75f;
    std::array<ToolHandCorrection,2> tool_hands;
    std::shared_ptr<const NativeSecondaryContactBaseline> secondary_baseline;
    SecondaryHandContactMode secondary_mode=SecondaryHandContactMode::None;
    std::uint64_t secondary_owner=0;
    std::vector<std::shared_ptr<const NativeAvatarPublication>> additional_views;
};
inline std::shared_ptr<const NativeAvatarPublication> native_publication_view(
    const std::shared_ptr<const NativeAvatarPublication>& root,std::uint64_t instances){
    if(!root||root->additional_views.size()>3)return {};
    std::shared_ptr<const NativeAvatarPublication> found;
    if(root->source_instances==instances)found=root;
    for(const auto& view:root->additional_views){
        if(!view||!view->additional_views.empty()||view->asset!=root->asset)return {};
        if(view->source_instances==instances){if(found)return {};found=view;}
    }
    return found;
}
inline std::shared_ptr<const NativeAvatarPublication> hold_native_publication(
    const std::shared_ptr<const NativeAvatarPublication>& previous,std::uint64_t epoch,
    std::uint64_t now,std::uint64_t serial) {
    if(!previous||!epoch||previous->source_buffer_epoch!=epoch||!previous->published_milliseconds||
        now<previous->published_milliseconds||now-previous->published_milliseconds>250||serial<=previous->serial)return {};
    if(previous->additional_views.size()>3)return {};
    auto held=std::make_shared<NativeAvatarPublication>(*previous);held->serial=serial;held->additional_views.clear();
    for(const auto& view:previous->additional_views){
        if(!view||!view->additional_views.empty()||view->asset!=previous->asset)return {};
        if(auto child=hold_native_publication(view,epoch,now,serial))held->additional_views.push_back(std::move(child));
    }
    return held;
}
template<class Hold> inline std::shared_ptr<NativeAvatarPublication> retain_native_publication_views(
    const std::shared_ptr<const NativeAvatarPublication>& fresh,
    const std::shared_ptr<const NativeAvatarPublication>& previous,Hold&& hold){
    auto result=fresh?std::make_shared<NativeAvatarPublication>(*fresh):nullptr;
    if(fresh&&!native_publication_view(fresh,fresh->source_instances))return {};
    if(!previous||previous->additional_views.size()>3)return result;
    std::vector<std::shared_ptr<const NativeAvatarPublication>> old{previous};
    old.insert(old.end(),previous->additional_views.begin(),previous->additional_views.end());
    for(const auto& view:old){
        if(!view||native_publication_view(previous,view->source_instances)!=view)continue;
        if(result&&(result->additional_views.size()>=3||native_publication_view(result,view->source_instances)))continue;
        auto leaf=std::make_shared<NativeAvatarPublication>(*view);leaf->additional_views.clear();
        auto held=hold(leaf);if(!held||!held->additional_views.empty()||(result&&held->asset!=result->asset))continue;
        if(!result)result=std::make_shared<NativeAvatarPublication>(*held);else result->additional_views.push_back(std::move(held));
    }
    return result;
}
template<class BuildBaseline> inline std::shared_ptr<const NativeAvatarPublication> hold_native_contact_publication(
    const std::shared_ptr<const NativeAvatarPublication>&previous,std::uint64_t epoch,
    std::uint64_t now,std::uint64_t serial,SecondaryHandContactMode mode,std::uint64_t owner,BuildBaseline&&build) {
    auto held=hold_native_publication(previous,epoch,now,serial);
    if(!held||!held->secondary_baseline||(held->secondary_mode==mode&&held->secondary_owner==owner))return held;
    // Rare release/owner transition while a sample is pending: restore the
    // baseline without another readback or fresh age. GPU mode only repacks
    // transforms; it never deforms the mesh on the CPU during a held frame.
    try {
        if(!held->asset||!held->secondary_baseline->pose)return held;
        auto restored=std::make_shared<NativeAvatarPublication>(*held);
        if(held->skinning){
            auto skinning=std::make_shared<NativeSkinPose>(make_native_skin_pose(held->skinning->asset,held->secondary_baseline->pose));
            if(!*skinning)return held;
            restored->geometry=skinning->asset->layout;restored->skinning=std::move(skinning);
        }else{
            auto geometry=build(*held->asset,held->secondary_baseline->pose);
            if(!geometry||!*geometry)return held;
            restored->geometry=std::move(geometry);
        }
        restored->tool_hands=held->secondary_baseline->hands;
        restored->secondary_baseline.reset();restored->secondary_mode=SecondaryHandContactMode::None;restored->secondary_owner=0;
        return restored;
    }catch(...){return held;}
}
}
