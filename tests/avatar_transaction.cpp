#include "../src/avatar_switch_transaction.hpp"
#include <iostream>
#include <stdexcept>
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    evrm::PreparedAvatar model;model.asset=std::make_shared<vrm::Avatar>();
    auto skin=std::make_shared<evrm::NativeSkinAsset>();skin->layout=std::make_shared<evrm::NativeAvatarPose>();model.skin=skin;
    auto pose=std::make_shared<evrm::NativeSkinPose>();pose->asset=skin;
    auto publication=std::make_shared<evrm::NativeAvatarPublication>();publication->asset=model.asset;publication->geometry=skin->layout;publication->skinning=pose;
    publication->serial=8;publication->source_buffer_epoch=3;publication->published_milliseconds=100;publication->source_instances=9;
    require(evrm::avatar_switch_pose_matches(publication,model,3,350),"fresh matching pose rejected");
    require(!evrm::avatar_switch_pose_matches(publication,model,3,351),"stale pose committed");
    require(!evrm::avatar_switch_pose_matches(publication,model,4,120),"wrong epoch committed");
    require(!evrm::avatar_switch_pose_matches(publication,model,3,99),"future pose committed");
    auto wrong=model;wrong.asset=std::make_shared<vrm::Avatar>();require(!evrm::avatar_switch_pose_matches(publication,wrong,3,120),"wrong model committed");
    wrong=model;wrong.skin=std::make_shared<evrm::NativeSkinAsset>();require(!evrm::avatar_switch_pose_matches(publication,wrong,3,120),"wrong skin committed");
    auto child=std::make_shared<evrm::NativeAvatarPublication>(*publication);child->source_instances=10;publication->additional_views.push_back(child);
    require(evrm::avatar_switch_pose_matches(publication,model,3,120),"matching child rejected");child->published_milliseconds=1;
    require(!evrm::avatar_switch_pose_matches(publication,model,3,300),"stale child committed");
    std::cout<<"PASS candidate model/skin/layout/epoch/freshness and child validation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
