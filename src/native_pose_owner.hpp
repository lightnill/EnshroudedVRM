#pragma once
#include "native_owner.hpp"
#include "native_state.hpp"
#include "gpu_probe.hpp"
#include <limits>
#include <tuple>
#include <atomic>

namespace evrm {
// Proposed pose selection, never an admission permit or resource lease. The
// backend must freshly revalidate this exact owner identity before submission.
struct NativePoseOwner {
    NativeOwnerCandidate candidate;
    NativeBufferBinding source;
    std::uint64_t source_epoch=0;
};
inline bool native_pose_owner_valid(const NativePoseOwner& selection) noexcept {
    const auto& owner=selection.candidate;const auto& source=selection.source;
    if(!selection.source_epoch||owner.guards.empty()||owner.models.empty()||owner.models.size()>24||
       !owner.instance_capacity||owner.instance_capacity>131072||owner.instance_extra>131072-owner.instance_capacity||
       owner.native_buffer_bytes!=(std::uint64_t(owner.instance_capacity)+owner.instance_extra)*128||
       !source.info.buffer||!source.generation||source.info.offset||source.info.range!=source.size||
       source.size!=owner.native_buffer_bytes||!(source.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||
       owner.native_buffer!=reinterpret_cast<std::uintptr_t>(source.info.buffer))return false;
    for(std::size_t i=0;i<owner.models.size();++i){const auto& model=owner.models[i];
        if(model.equipment_slot>=24||model.index>=owner.instance_capacity||model.generation>255||
           model.handle!=((model.generation<<24)|model.index)||!model.record)return false;
        for(std::size_t j=0;j<i;++j)if(owner.models[j].index==model.index||owner.models[j].equipment_slot==model.equipment_slot)return false;
    }
    return true;
}
inline bool native_pose_owner_contains(const NativePoseOwner& owner,std::uint32_t instance) noexcept {
    for(const auto& model:owner.candidate.models)if(model.index==instance)return true;
    return false;
}
inline bool native_pose_owner_matches_snapshot(const NativePoseOwner& owner,const GpuSnapshot& snapshot) noexcept {
    if(!native_pose_owner_valid(owner)||snapshot.source_epoch!=owner.source_epoch)return false;
    const auto found=snapshot.buffers.find("instances");if(found==snapshot.buffers.end())return false;
    const auto& buffer=found->second;
    return buffer.binding.buffer==owner.source.info.buffer&&!buffer.binding.offset&&
        (buffer.binding.range==owner.source.size||buffer.binding.range==VK_WHOLE_SIZE)&&
        !buffer.bytes.empty()&&buffer.bytes.size()%128==0&&buffer.bytes.size()<=owner.source.size;
}
inline bool same_native_pose_allocation(const NativeBufferBinding& a,const NativeBufferBinding& b) noexcept {
    // Completed captures do not depend on later descriptor-set versions.
    return a.info.buffer&&a.info.buffer==b.info.buffer&&a.generation&&a.generation==b.generation&&
        a.info.offset==b.info.offset&&a.info.range==b.info.range&&a.size==b.size&&a.usage==b.usage;
}
inline bool same_native_owner_selection(const NativeOwnerCandidate& a,const NativeOwnerCandidate& b) noexcept {
    if(a.image.base!=b.image.base||a.image.size!=b.image.size||a.image.timestamp!=b.image.timestamp||a.image.sha256!=b.image.sha256)return false;
    if(std::tie(a.root,a.client,a.controller,a.main,a.execution,a.registry,a.entity_manager,a.local_player_data,
        a.account,a.connection,a.player_infos,a.player_record,a.entity,a.entity_layout,a.entity_storage,a.equipment,a.entity_id,a.entity_generation,a.entity_slot,a.entity_row,
        a.input_registration,a.equipment_registration,a.render_scene,a.model_manager,a.writer,a.resource_object,
        a.native_buffer,a.native_buffer_bytes,a.instance_capacity,a.instance_extra)!=
       std::tie(b.root,b.client,b.controller,b.main,b.execution,b.registry,b.entity_manager,b.local_player_data,
        b.account,b.connection,b.player_infos,b.player_record,b.entity,b.entity_layout,b.entity_storage,b.equipment,b.entity_id,b.entity_generation,b.entity_slot,b.entity_row,
        b.input_registration,b.equipment_registration,b.render_scene,b.model_manager,b.writer,b.resource_object,
        b.native_buffer,b.native_buffer_bytes,b.instance_capacity,b.instance_extra))return false;
    if(a.models.size()!=b.models.size())return false;
    for(std::size_t i=0;i<a.models.size();++i){const auto& x=a.models[i];const auto& y=b.models[i];
        if(std::tie(x.equipment_slot,x.handle,x.index,x.generation,x.record)!=std::tie(y.equipment_slot,y.handle,y.index,y.generation,y.record))return false;}
    return true;
}
inline bool native_pose_owner_matches_candidate(const NativePoseOwner& selection,const NativeOwnerCandidate& current,
    const NativeBufferBinding& source,std::uint64_t epoch) noexcept {
    return native_pose_owner_valid(selection)&&!current.guards.empty()&&selection.source_epoch==epoch&&
        same_native_pose_allocation(selection.source,source)&&same_native_owner_selection(selection.candidate,current);
}
// Externally synchronized. Captures through the existing attested adapter only
// on cold/invalid hints. Reuse proposes selection, never skips submit validation.
class NativePoseOwnerSampler {
    std::uint64_t retry_after_=0;
    std::shared_ptr<const NativePoseOwner> observed_;
    void cooldown(std::uint64_t now)noexcept {retry_after_=now<=std::numeric_limits<std::uint64_t>::max()-250?now+250:std::numeric_limits<std::uint64_t>::max();}
public:
    void invalidate_observation(std::uint64_t now)noexcept {observed_.reset();cooldown(now);}
    // The caller has freshly captured and verified this complete identity.
    // Retaining it only proposes the next pose selection; it grants no permit.
    bool remember_observation(const NativeOwnerCandidate& candidate,const NativeBufferBinding& source,
        std::uint64_t epoch,std::uint64_t now)noexcept {
        try{
            auto next=std::make_shared<NativePoseOwner>();next->candidate=candidate;next->source=source;next->source_epoch=epoch;
            if(!native_pose_owner_valid(*next))return false;
            observed_=std::move(next);cooldown(now);return true;
        }catch(...){return false;}
    }
    template<class Capture,class Source,class Valid>
    std::shared_ptr<const NativePoseOwner> sample(const std::shared_ptr<const NativePoseOwner>& hint,
        std::uint64_t epoch,std::uint64_t now,Capture&& capture,Source&& source,Valid&& valid) noexcept {
        if(!epoch)return {};
        try{
            const auto usable=[&](const auto& value){return value&&native_pose_owner_valid(*value)&&
                same_native_pose_allocation(value->source,source(value->source.info.buffer))&&valid(value->source);};
            const auto rebound=[&](const auto& value)->std::shared_ptr<const NativePoseOwner>{
                if(value->source_epoch==epoch)return value;
                auto next=std::make_shared<NativePoseOwner>(*value);next->source_epoch=epoch;return next;
            };
            if(usable(hint)){observed_=rebound(hint);cooldown(now);return observed_;}
            if(observed_&&!usable(observed_)){observed_.reset();retry_after_=0;}
            if(now<retry_after_){if(observed_)observed_=rebound(observed_);return observed_;}
            cooldown(now);observed_.reset();NativeOwnerCandidate owner;
            if(!capture(owner))return {};
            auto result=std::make_shared<NativePoseOwner>();result->candidate=std::move(owner);
            result->source=source(reinterpret_cast<VkBuffer>(result->candidate.native_buffer));result->source_epoch=epoch;
            if(!native_pose_owner_valid(*result)||!valid(result->source))return {};
            observed_=result;return observed_;
        }catch(...){observed_.reset();cooldown(now);return {};}
    }
};
// Publication/preparation revocation cannot discard a bounded observational
// cache and thereby cause another expensive capture every presentation. Only a
// freshly negative owner check invalidates that observation; neither is a permit.
class NativePoseOwnerState {
    NativePoseOwnerSampler sampler_;
    std::shared_ptr<const NativePoseOwner> prepared_;
public:
    std::shared_ptr<const NativePoseOwner> preparation_hint()const noexcept {return std::atomic_load(&prepared_);}
    void retain_preparation(std::shared_ptr<const NativePoseOwner> value)noexcept {std::atomic_store(&prepared_,std::move(value));}
    void revoke_preparation()noexcept {std::atomic_store(&prepared_,std::shared_ptr<const NativePoseOwner>{});}
    void invalidate_observation(std::uint64_t now)noexcept {revoke_preparation();sampler_.invalidate_observation(now);}
    bool remember_observation(const NativeOwnerCandidate& candidate,const NativeBufferBinding& source,
        std::uint64_t epoch,std::uint64_t now)noexcept {
        revoke_preparation();return sampler_.remember_observation(candidate,source,epoch,now);
    }
    template<class Capture,class Source,class Valid>
    std::shared_ptr<const NativePoseOwner> sample(std::uint64_t epoch,std::uint64_t now,Capture&& capture,Source&& source,Valid&& valid)noexcept {
        return sampler_.sample(preparation_hint(),epoch,now,std::forward<Capture>(capture),std::forward<Source>(source),std::forward<Valid>(valid));
    }
};
}
