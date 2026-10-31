#pragma once
#include "native_publication.hpp"
#include "mouth_deformation.hpp"
#include "voice_stream.hpp"
namespace evrm {
inline void apply_native_mouth_weights(NativeAvatarPublication& publication,const MouthSnapshot& voice,float strength,bool gpu)noexcept {
    publication.mouth_weights={};
    if(!gpu||!voice.epoch||!voice.timestamp_us||(voice.activity!=VoiceActivity::Talking&&voice.activity!=VoiceActivity::Quiet)||
       !publication.skinning||!*publication.skinning||!publication.skinning->mouth_safe||!publication.skinning->asset->mouth)return;
    publication.mouth_weights=vrm::mouth_expression_weights(*publication.skinning->asset->mouth,voice.weights,strength);
}
inline bool native_mouth_material_plan(const NativeAvatarPublication& publication,bool gpu_mouth,NativeMaterialPlan& out)noexcept {
    try{
        if(!publication.skinning)return publication.asset&&publication.geometry&&make_native_material_plan(*publication.asset,*publication.geometry,out);
        const auto& pose=*publication.skinning;if(!pose||publication.geometry!=pose.asset->layout)return false;
        out=pose.material_plan;
        const bool finite=std::all_of(publication.mouth_weights.begin(),publication.mouth_weights.end(),[](float value){return std::isfinite(value)&&value>=0&&value<=1;});
        const bool moving=finite&&gpu_mouth&&pose.mouth_safe&&pose.asset->mouth&&
            std::any_of(publication.mouth_weights.begin(),publication.mouth_weights.end(),[](float value){return value>0;});
        if(!moving){out.blend_min=pose.base_blend_min;out.blend_max=pose.base_blend_max;}
        return out.failure==NativeMaterialPlanFailure::none;
    }catch(...){out={};out.failure=NativeMaterialPlanFailure::allocation;return false;}
}
}
