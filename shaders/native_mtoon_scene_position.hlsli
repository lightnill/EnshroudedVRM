#ifndef EVRM_NATIVE_MTOON_SCENE_POSITION
#define EVRM_NATIVE_MTOON_SCENE_POSITION
#include "native_mtoon_scene_parameters.hlsli"
struct NativeMToonScenePosition {
    float3 relative,world,view;
    float2 uv;
    float distance;
};
// Numeric camera adapter. Source readiness, original view/depth identity and
// native descriptor/use/queue/lifetime permission are caller obligations.
// The main light block stores row vectors at176 and world camera.xyz at240;
// this is factual layout/dataflow adaptation, not a native shader payload.
bool native_mtoon_scene_position(NativeMToonSceneLightParameters scene,uint2 pixel,uint2 extent,float depth,
                                out NativeMToonScenePosition position){
    position=(NativeMToonScenePosition)0;
    if(any(extent==0u)||any(extent>16384u)||any(pixel>=extent)||any(scene.words[2].xy!=extent)||
       !isfinite(depth)||depth<=0.f||depth>1.f)return false;
    const float2 reciprocal=asfloat(scene.words[2].zw),expected=1.f/float2(extent);
    if(any(!isfinite(reciprocal))||any(reciprocal<=0.f)||any(abs(reciprocal-expected)>expected*1e-5f))return false;
    const float4 row0=asfloat(scene.words[11]),row1=asfloat(scene.words[12]),row2=asfloat(scene.words[13]),row3=asfloat(scene.words[14]);
    const float3 camera=asfloat(scene.words[15].xyz);
    if(any(!isfinite(row0))||any(!isfinite(row1))||any(!isfinite(row2))||any(!isfinite(row3))||any(!isfinite(camera)))return false;
    precise float2 ndc=(float2(pixel)*2.f+1.f)*reciprocal-1.f;
    const float4 screen=float4(ndc.x,-ndc.y,depth,1.f);
    precise float4 homogeneous=float4(dot(row0,screen),dot(row1,screen),dot(row2,screen),dot(row3,screen));
    if(any(!isfinite(homogeneous))||homogeneous.w<=1e-20f)return false;
    const float3 relative=homogeneous.xyz/homogeneous.w,world=relative+camera;
    const float scale=max(abs(relative.x),max(abs(relative.y),abs(relative.z)));
    if(any(!isfinite(relative))||any(!isfinite(world))||!isfinite(scale)||scale<=1e-20f)return false;
    const float3 direction=relative/scale;const float lengthUnit=length(direction),distance=scale*lengthUnit;
    if(!isfinite(lengthUnit)||lengthUnit<=0.f||!isfinite(distance)||distance<=0.f)return false;
    position.relative=relative;position.world=world;position.view=-direction/lengthUnit;
    precise float2 pixelUv=(float2(pixel)+.5f)*reciprocal;
    position.distance=distance;position.uv=pixelUv;return true;
}
#endif
