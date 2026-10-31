#include "../src/mouth_publication.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
namespace {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
std::shared_ptr<evrm::NativeAvatarPublication> fixture(){
    auto source=std::make_shared<evrm::NativeSkinAsset>();source->vertices.resize(3);
    auto layout=std::make_shared<evrm::NativeAvatarPose>();layout->vertices.resize(3);source->layout=layout;
    auto mouth=std::make_shared<vrm::MouthData>();mouth->available=1;source->mouth=mouth;
    auto skin=std::make_shared<evrm::NativeSkinPose>();skin->asset=source;skin->batches.resize(1);skin->mouth_safe=true;skin->material_plan.mixed=true;
    skin->material_plan.blend_min={-2,-2,-2,0};skin->material_plan.blend_max={2,2,2,0};skin->base_blend_min={-1,-1,-1,0};skin->base_blend_max={1,1,1,0};
    auto publication=std::make_shared<evrm::NativeAvatarPublication>();publication->geometry=layout;publication->skinning=skin;
    publication->serial=1;publication->published_milliseconds=1000;publication->source_buffer_epoch=7;publication->source_instances=1;return publication;
}
}
int main(){
    auto fresh=fixture();const auto original_skin=fresh->skinning;evrm::MouthSnapshot voice;voice.epoch=3;voice.timestamp_us=1000000;voice.activity=evrm::VoiceActivity::Talking;voice.weights={.5f,.5f,0,0,0};
    evrm::apply_native_mouth_weights(*fresh,voice,.8f,true);need(std::abs(fresh->mouth_weights[0]-.4f)<1e-6&&fresh->mouth_weights[1]==0,"publication strength/missing channel mask");
    auto held=std::const_pointer_cast<evrm::NativeAvatarPublication>(evrm::hold_native_publication(fresh,7,1001,2));need(bool(held),"held fixture unavailable");
    voice.weights={1,0,0,0,0};evrm::apply_native_mouth_weights(*held,voice,1,true);
    need(held->mouth_weights[0]==1&&held->skinning==original_skin&&fresh->mouth_weights[0]<.5f,"held mouth copied/replaced immutable body pose or changed predecessor");
    evrm::NativeMaterialPlan plan;need(evrm::native_mouth_material_plan(*held,true,plan)&&plan.blend_max[0]==2,"active mouth did not use enlarged bounds");
    need(evrm::native_mouth_material_plan(*held,false,plan)&&plan.blend_max[0]==1,"unavailable GPU mouth did not use baseline bounds");
    voice.activity=evrm::VoiceActivity::Off;evrm::apply_native_mouth_weights(*held,voice,1,true);need(held->mouth_weights==vrm::MouthWeights{},"Off snapshot animated mouth");
    need(evrm::native_mouth_material_plan(*held,true,plan)&&plan.blend_max[0]==1,"neutral mouth enlarged transparency work");
    voice.activity=evrm::VoiceActivity::Waiting;evrm::apply_native_mouth_weights(*held,voice,1,true);need(held->mouth_weights==vrm::MouthWeights{},"waiting/stale snapshot animated mouth");
    voice.activity=evrm::VoiceActivity::Quiet;voice.weights={.1f,0,0,0,0};evrm::apply_native_mouth_weights(*held,voice,1,true);need(held->mouth_weights[0]>.09f,"quiet release was discarded abruptly");
    evrm::apply_native_mouth_weights(*held,voice,1,false);need(held->mouth_weights==vrm::MouthWeights{},"alternate CPU renderer received mouth animation");
    auto view=fixture();view->source_instances=2;evrm::apply_native_mouth_weights(*view,voice,1,true);evrm::apply_native_mouth_weights(*held,voice,1,true);
    need(view->mouth_weights==held->mouth_weights,"same-presentation views diverged");
    voice.epoch=0;evrm::apply_native_mouth_weights(*held,voice,1,true);need(held->mouth_weights==vrm::MouthWeights{},"invalid voice epoch reached publication");
    std::puts("PASS fresh/held mouth publications, shared body ownership, neutral bounds and alternate renderer");
}
