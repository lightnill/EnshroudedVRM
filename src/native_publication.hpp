#pragma once
#include "native_avatar_pose.hpp"
#include "native_skinning.hpp"
#include "tool_grip.hpp"
#include "secondary_hand_contact.hpp"
#include "avatar_appearance.hpp"
#include "native_preview_subject.hpp"
#include "native_player_identity.hpp"
#include <string>
#include <memory>
#include <array>

namespace evrm {
struct NativePoseOwner;
struct NativeSecondaryContactBaseline {
    vrm::RetargetResult pose;
    std::array<ToolHandCorrection,2> hands;
};
struct NativeAvatarPublication {
    NativePreviewStamp preview_subject;
    std::shared_ptr<const NativePoseOwner> pose_owner;
    std::shared_ptr<const AvatarAppearance> appearance;
    vrm::MouthWeights mouth_weights{};
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
    std::string subject;
    NativePlayerIdentity player_identity;
    std::uint64_t assignment_generation=0;
    // A view contains at most sixteen independently owned actors. Subject
    // children are leaves; native allocations remain additional_views only.
    std::vector<std::shared_ptr<const NativeAvatarPublication>> additional_subjects;
};
inline bool native_publication_subjects_valid(const std::shared_ptr<const NativeAvatarPublication>& view)noexcept {
    if(!view||view->additional_subjects.size()>15)return false;
    std::array<const NativeAvatarPublication*,16> subjects{};subjects[0]=view.get();
    for(std::size_t i=0;i<view->additional_subjects.size();++i){const auto& child=view->additional_subjects[i];
        if(!child||!child->additional_subjects.empty()||!child->additional_views.empty())return false;
        subjects[i+1]=child.get();
    }
    for(std::size_t i=0;i<=view->additional_subjects.size();++i){const auto& p=*subjects[i];
        if(p.source_instances!=view->source_instances||p.source_buffer_epoch!=view->source_buffer_epoch||
            p.serial!=view->serial||p.presentation_serial!=view->presentation_serial||p.parts.size()>64)return false;
        for(std::size_t j=0;j<i;++j){const auto& old=*subjects[j];
            if(p.subject==old.subject||(p.player_identity.account&&p.player_identity.account==old.player_identity.account)||
                (p.player_identity.entity_id&&p.player_identity.entity_id==old.player_identity.entity_id))return false;
            for(const auto& part:p.parts)for(const auto& previous:old.parts)if(part[0]==previous[0])return false;
        }
        for(std::size_t j=0;j<p.parts.size();++j)for(std::size_t k=0;k<j;++k)if(p.parts[j][0]==p.parts[k][0])return false;
    }
    return true;
}
inline std::shared_ptr<const NativeAvatarPublication> native_publication_subject(
    const std::shared_ptr<const NativeAvatarPublication>& view,const std::string& subject)noexcept {
    if(!native_publication_subjects_valid(view))return {};
    if(view->subject==subject)return view;
    for(const auto& child:view->additional_subjects)if(child->subject==subject)return child;
    return {};
}
inline std::shared_ptr<const NativeAvatarPublication> native_publication_view(
    const std::shared_ptr<const NativeAvatarPublication>& root,std::uint64_t instances)noexcept {
    if(!root||root->additional_views.size()>3||!native_publication_subjects_valid(root))return {};
    std::shared_ptr<const NativeAvatarPublication> found;
    if(root->source_instances==instances)found=root;
    for(std::size_t i=0;i<root->additional_views.size();++i){const auto& view=root->additional_views[i];
        if(!view||!view->additional_views.empty()||!native_publication_subjects_valid(view)||view->source_instances==root->source_instances)return {};
        for(std::size_t j=0;j<i;++j)if(view->source_instances==root->additional_views[j]->source_instances)return {};
        if(view->source_instances==instances)found=view;
    }
    return found;
}
// Atomically replace one subject's staged proof while preserving unrelated
// subjects from the same captured cohort. Never mix independent sample serials.
inline std::shared_ptr<const NativeAvatarPublication> replace_native_publication_subject(
    const std::shared_ptr<const NativeAvatarPublication>& active,
    const std::shared_ptr<const NativeAvatarPublication>& staged){
    if(!staged||!native_publication_view(staged,staged->source_instances)||!staged->additional_subjects.empty())return {};
    const auto& subject=staged->subject;
    for(const auto& view:staged->additional_views)if(view->subject!=subject||!view->additional_subjects.empty())return {};
    if(!active)return staged;
    if(!native_publication_view(active,active->source_instances))return {};
    std::vector<std::shared_ptr<const NativeAvatarPublication>> old{active},incoming{staged};
    old.insert(old.end(),active->additional_views.begin(),active->additional_views.end());
    incoming.insert(incoming.end(),staged->additional_views.begin(),staged->additional_views.end());
    std::shared_ptr<NativeAvatarPublication> result;
    const auto append_view=[&](const std::shared_ptr<const NativeAvatarPublication>& view){
        if(!result){result=std::make_shared<NativeAvatarPublication>(*view);result->additional_views.clear();}
        else if(result->additional_views.size()<3)result->additional_views.push_back(view);
        else return false;
        return true;
    };
    for(const auto& view:old){
        const auto replacement=native_publication_view(staged,view->source_instances);
        std::vector<std::shared_ptr<const NativeAvatarPublication>> leaves;
        if(view->subject!=subject)leaves.push_back(view);
        for(const auto& child:view->additional_subjects)if(child->subject!=subject)leaves.push_back(child);
        if(replacement){
            if(!leaves.empty()&&(view->serial!=replacement->serial||view->presentation_serial!=replacement->presentation_serial||
                view->source_buffer_epoch!=replacement->source_buffer_epoch))return {};
            leaves.push_back(replacement);
        }
        if(leaves.empty())continue;
        auto group=std::make_shared<NativeAvatarPublication>(*leaves.front());group->additional_views.clear();group->additional_subjects.clear();
        for(std::size_t i=1;i<leaves.size();++i){auto leaf=std::make_shared<NativeAvatarPublication>(*leaves[i]);leaf->additional_views.clear();leaf->additional_subjects.clear();group->additional_subjects.push_back(std::move(leaf));}
        if(!native_publication_subjects_valid(group)||!append_view(group))return {};
    }
    for(const auto& view:incoming)if(!native_publication_view(active,view->source_instances)&&!append_view(view))return {};
    return result;
}
// Filter retained CPU references without sampling another pose. Promote an
// eligible sibling when a view root leaves; preserve its original cohort time.
template<class Keep> inline std::shared_ptr<const NativeAvatarPublication> retain_native_publication_subjects(
    const std::shared_ptr<const NativeAvatarPublication>& root,Keep&& keep){
    if(!root||!native_publication_view(root,root->source_instances))return {};
    const auto filter=[&](const std::shared_ptr<const NativeAvatarPublication>& view){
        std::shared_ptr<NativeAvatarPublication> group;
        for(std::size_t i=0;i<=view->additional_subjects.size();++i){
            const auto& subject=i?view->additional_subjects[i-1]:view;
            if(!keep(*subject))continue;
            if(!group){group=std::make_shared<NativeAvatarPublication>(*subject);group->additional_subjects.clear();group->additional_views.clear();}
            else group->additional_subjects.push_back(subject);
        }
        return group;
    };
    auto result=filter(root);
    for(const auto& view:root->additional_views)if(auto group=filter(view)){
        if(!result)result=std::move(group);else result->additional_views.push_back(std::move(group));
    }
    return result;
}
inline std::shared_ptr<const NativeAvatarPublication> native_publication_appearance(
    const std::shared_ptr<const NativeAvatarPublication>& root,const std::shared_ptr<const AvatarAppearance>& appearance){
    if(!root||!native_publication_view(root,root->source_instances))return {};
    const auto asset=root->asset;
    const auto unchanged=[&](const std::shared_ptr<const NativeAvatarPublication>& view){
        if(view->asset==asset&&view->appearance!=appearance)return false;
        for(const auto& subject:view->additional_subjects)if(subject->asset==asset&&subject->appearance!=appearance)return false;
        return true;
    };
    bool same=unchanged(root);for(const auto& view:root->additional_views)same=same&&unchanged(view);
    if(same)return root;
    const auto revise=[&](const std::shared_ptr<const NativeAvatarPublication>& view){
        auto next=std::make_shared<NativeAvatarPublication>(*view);next->additional_subjects.clear();
        if(next->asset==asset)next->appearance=appearance;
        for(const auto& subject:view->additional_subjects){auto child=std::make_shared<NativeAvatarPublication>(*subject);
            if(child->asset==asset)child->appearance=appearance;
            next->additional_subjects.push_back(std::move(child));}
        return next;
    };
    auto next=revise(root);next->additional_views.clear();
    for(const auto& view:root->additional_views)next->additional_views.push_back(revise(view));
    return next;
}
inline std::shared_ptr<const NativeAvatarPublication> hold_native_publication(
    const std::shared_ptr<const NativeAvatarPublication>& previous,std::uint64_t epoch,
    std::uint64_t now,std::uint64_t serial) {
    if(!previous||!epoch||previous->source_buffer_epoch!=epoch||!previous->published_milliseconds||
        now<previous->published_milliseconds||now-previous->published_milliseconds>250||serial<=previous->serial||
        !native_publication_view(previous,previous->source_instances))return {};
    auto held=std::make_shared<NativeAvatarPublication>(*previous);held->serial=serial;held->additional_views.clear();held->additional_subjects.clear();
    for(const auto& subject:previous->additional_subjects){
        if(auto child=hold_native_publication(subject,epoch,now,serial))held->additional_subjects.push_back(std::move(child));
    }
    for(const auto& view:previous->additional_views){
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
        auto held=hold(leaf);if(!held||!held->additional_views.empty())continue;
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
