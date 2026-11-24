#pragma once
#include "native_mtoon_resource_plan.hpp"
#include <memory>
#include <utility>

namespace evrm {
using NativeMToonSelection=std::vector<std::uint8_t>;
inline std::uint8_t native_mtoon_choice(AvatarShaderPreset preset,AvatarOutlineColor color=AvatarOutlineColor::Black)noexcept{
    return !avatar_preset_is_toon(preset)?0:std::uint8_t((avatar_preset_is_nilltoon(preset)?3:0)+(avatar_preset_has_outline(preset)?(color==AvatarOutlineColor::Model?3:2):1));
}
// Empty is the canonical ordinary-material key. It retains no source object
// and can live in a weak-source failure latch without pinning a model.
inline bool native_mtoon_selection_matches(const NativeMToonSelection& selection,const vrm::Avatar& avatar,
    const AvatarAppearance* appearance)noexcept{
    if(!selection.empty()&&selection.size()!=avatar.materials.size())return false;
    for(std::size_t i=0;i<avatar.materials.size();++i)
        if((selection.empty()?0:selection[i])!=native_mtoon_choice(avatar_material_preset(appearance,i),avatar_material_outline_color(appearance,i)))return false;
    return true;
}
class NativeMToonRequest;
std::shared_ptr<const NativeMToonRequest> make_native_mtoon_request(
    const std::shared_ptr<const vrm::Avatar>&,const AvatarAppearance*,std::uint32_t image_dimension_limit);
// Immutable, source-bound CPU preparation. It attests neither GPU completion
// nor scene admission. Keep the same request for an unchanged material choice.
class NativeMToonRequest {
    std::shared_ptr<const vrm::Avatar> avatar_;
    NativeMToonResourcePlan plan_;
    NativeMToonSelection selection_;
    NativeMToonRequest(std::shared_ptr<const vrm::Avatar> avatar,NativeMToonResourcePlan plan,const AvatarAppearance* appearance):
        avatar_(std::move(avatar)),plan_(std::move(plan)){
        selection_.reserve(avatar_->materials.size());
        for(std::size_t i=0;i<avatar_->materials.size();++i)selection_.push_back(native_mtoon_choice(avatar_material_preset(appearance,i),avatar_material_outline_color(appearance,i)));
    }
    friend std::shared_ptr<const NativeMToonRequest> make_native_mtoon_request(
        const std::shared_ptr<const vrm::Avatar>&,const AvatarAppearance*,std::uint32_t);
public:
    ~NativeMToonRequest()=default;
    NativeMToonRequest(const NativeMToonRequest&)=delete;
    NativeMToonRequest& operator=(const NativeMToonRequest&)=delete;
    const std::shared_ptr<const vrm::Avatar>& avatar()const noexcept{return avatar_;}
    const NativeMToonResourcePlan& plan()const noexcept{return plan_;}
    const NativeMToonSelection& selection()const noexcept{return selection_;}
};
inline std::shared_ptr<const NativeMToonRequest> make_native_mtoon_request(
    const std::shared_ptr<const vrm::Avatar>& avatar,const AvatarAppearance* appearance,std::uint32_t image_dimension_limit){
    if(!avatar)return {};
    auto plan=make_native_mtoon_resource_plan(*avatar,appearance,image_dimension_limit);
    if(!plan)throw std::invalid_argument(plan.error);
    if(plan.materials.empty())return {};
    return std::shared_ptr<const NativeMToonRequest>(new NativeMToonRequest(avatar,std::move(plan),appearance));
}
// Prepared selected normal maps share the existing immutable/posed tangent
// ABI. No old preset, missing metadata or unused/zero-scale map requests it.
inline bool native_mtoon_request_needs_tangents(const std::shared_ptr<const NativeMToonRequest>& request)noexcept{
    if(!request)return false;
    for(const auto& material:request->plan().materials)
        if(material.status==MToonResourceStatus::prepared_cpu&&(material.textures&(1u<<2u)))return true;
    return false;
}
inline bool native_mtoon_request_matches(const std::shared_ptr<const NativeMToonRequest>& request,
    const std::shared_ptr<const vrm::Avatar>& avatar,const AvatarAppearance* appearance)noexcept{
    if(!avatar)return !request;
    if(request)return request->avatar()==avatar&&native_mtoon_selection_matches(request->selection(),*avatar,appearance);
    const NativeMToonSelection empty;return native_mtoon_selection_matches(empty,*avatar,appearance);
}
}
