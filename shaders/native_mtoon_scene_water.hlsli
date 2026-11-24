#ifndef EVRM_NATIVE_MTOON_SCENE_WATER
#define EVRM_NATIVE_MTOON_SCENE_WATER
#include "native_mtoon_scene_position.hlsli"
#include "native_mtoon_scene_light_inputs.hlsli"
// Factual native set2:13 layout: nine64-byte records, scalar fields592..636.
// This typed numeric view supplies no source/queue/lifetime permission.
struct NativeMToonWaterParameters {uint4 words[40];};
bool native_mtoon_water_relative(NativeMToonSceneLightParameters scene,uint2 pixel,float depth,out float3 relative){
    relative=0;if(!isfinite(depth)||depth<0.f||depth>1.f)return false;
    const float4 row0=asfloat(scene.words[11]),row1=asfloat(scene.words[12]),row2=asfloat(scene.words[13]),row3=asfloat(scene.words[14]);
    if(any(!isfinite(row0))||any(!isfinite(row1))||any(!isfinite(row2))||any(!isfinite(row3)))return false;
    precise float2 ndc=(float2(pixel)*2.f+1.f)*asfloat(scene.words[2].zw)-1.f;
    const float4 screen=float4(ndc.x,-ndc.y,depth,1.f);
    precise float4 homogeneous=float4(dot(row0,screen),dot(row1,screen),dot(row2,screen),dot(row3,screen));
    // Water depth may be zero if that projection still has a finite far point.
    // Infinite/invalid far projections explicitly decline, never invent depth.
    if(any(!isfinite(homogeneous))||homogeneous.w<=1e-20f)return false;
    const float3 candidate=homogeneous.xyz/homogeneous.w;if(any(!isfinite(candidate)))return false;
    relative=candidate;return true;
}
// Reason/fact outputs are bounded scalar classifications for the existing
// owned observation grid, never texture contents or rendering authority.
bool native_mtoon_water_factor_diagnostic(NativeMToonSceneLightParameters scene,NativeMToonScenePosition position,uint2 pixel,
    Texture2D<float4> surface,Texture2D<float> depth,out float factor,out uint detail,out uint facts){
    factor=0;detail=facts=0;uint sw,sh,dw,dh;surface.GetDimensions(sw,sh);depth.GetDimensions(dw,dh);
    if(!sw||!sh||sw!=dw||sh!=dh){detail=2;return false;}
    const float2 scale=asfloat(scene.words[121].xy),mapped=float2(pixel)*scale;
    if(any(!isfinite(scale))||any(scale<=0.f)||any(!isfinite(mapped))||any(mapped<0.f)||any(mapped>=float2(sw,sh))){detail=3;return false;}
    const uint2 at=uint2(mapped);const float alpha=surface.Load(int3(at,0)).a;
    if(!isfinite(alpha)||alpha<0.f||alpha>1.f||!isfinite(position.distance)||position.distance<=0.f){detail=4;return false;}
    const uint classification=uint(alpha*3.f+.5f);const float sampledDepth=depth.Load(int3(at,0));float3 waterRelative;
    facts=uint(sampledDepth==0.f)|((classification&1u)<<1u);
    if(!native_mtoon_water_relative(scene,pixel,sampledDepth,waterRelative)){
        detail=5;
        const float2 ndc=(float2(pixel)*2.f+1.f)*asfloat(scene.words[2].zw)-1.f;
        const float4 screen=float4(ndc.x,-ndc.y,sampledDepth,1.f);
        const float4 homogeneous=float4(dot(asfloat(scene.words[11]),screen),dot(asfloat(scene.words[12]),screen),
            dot(asfloat(scene.words[13]),screen),dot(asfloat(scene.words[14]),screen));
        facts|=isfinite(homogeneous.w)?4u:0u;facts|=homogeneous.w==0.f?8u:0u;
        facts|=all(isfinite(homogeneous.xyz))&&any(homogeneous.xyz!=0.f)?16u:0u;
        return false;
    }
    const float magnitude=max(abs(waterRelative.x),max(abs(waterRelative.y),abs(waterRelative.z)));
    const float distance=magnitude>0.f?magnitude*length(waterRelative/magnitude):0.f;
    if(!isfinite(distance)){detail=6;return false;}
    float candidate=saturate(max(position.distance-distance,0.f)*.5f);
    if((classification&1u)!=0u)candidate=1.f-candidate;
    if(!isfinite(candidate)){detail=6;return false;}factor=candidate;return true;
}
bool native_mtoon_water_factor(NativeMToonSceneLightParameters scene,NativeMToonScenePosition position,uint2 pixel,
    Texture2D<float4> surface,Texture2D<float> depth,out float factor){
    uint detail,facts;return native_mtoon_water_factor_diagnostic(scene,position,pixel,surface,depth,factor,detail,facts);
}
bool native_mtoon_water_coordinates(NativeMToonSceneLightParameters scene,NativeMToonScenePosition position,
    out float2 planar,out float2 causticUv){
    planar=causticUv=0;const float3 row0=asfloat(scene.words[28].xyz),row1=asfloat(scene.words[29].xyz);
    const float2 origin=asfloat(scene.words[18].xy);
    if(any(!isfinite(row0))||any(!isfinite(row1))||any(!isfinite(origin))||any(!isfinite(position.relative))||any(!isfinite(position.world)))return false;
    planar=float2(dot(row0,position.relative),dot(row1,position.relative));
    precise float2 uv=position.world.xz*.1f+position.world.y*.05f+(planar+origin)*.0025f;
    if(any(!isfinite(planar))||any(!isfinite(uv)))return false;causticUv=uv;return true;
}
// For an independently qualified original subgroup/local-index mapping, its
// low-bit swizzle makes lane XOR1/2 taps pixel x/y XOR1. Vulkan does not
// guarantee that subgroup/local-index relationship. The parent must prove it
// for the actual original program/device; this helper never infers it. Only
// finite active peers are admitted, not background/missing/divergent values.
bool native_mtoon_water_peer(NativeMToonSceneLightParameters scene,uint2 pixel,uint2 extent,
    Texture2D<float> mainDepth,Texture2D<float4> surface,Texture2D<float> depth,out float2 causticUv){
    causticUv=0;if(any(pixel>=extent))return false;NativeMToonScenePosition position;
    if(!native_mtoon_scene_position(scene,pixel,extent,mainDepth.Load(int3(pixel,0)),position))return false;
    float factor;if(!native_mtoon_water_factor(scene,position,pixel,surface,depth,factor)||factor<=0.f)return false;
    float2 planar;return native_mtoon_water_coordinates(scene,position,planar,causticUv);
}
// Concrete light modulation from exact supplied native maps and samplers.
// Masks are actual parent admissions, not census availability. Every sampled
// range/quad peer and both samplers must have initialization, use/layout,
// queue and execution lifetime proof; none is granted by this function.
bool native_mtoon_scene_water(NativeMToonSceneLightParameters scene,NativeMToonWaterParameters water,
    NativeMToonScenePosition position,uint2 pixel,uint waterInputs,uint samplers,uint sceneImages,bool quadMappingQualified,
    Texture2D<float> mainDepth,Texture2D<float4> surface,Texture2D<float> depth,
    Texture2DArray<float> shadow,SamplerState shadowSampler,Texture2D<float4> caustic,SamplerState causticSampler,
    out NativeMToonSceneModulation result){
    result=(NativeMToonSceneModulation)0;
    const uint2 extent=scene.words[2].xy;const uint flags=scene.words[118].w;
    const float2 reciprocal=asfloat(scene.words[2].zw),expected=1.f/float2(extent);
    const float4 gains=asfloat(scene.words[0]);const float3 sun=asfloat(scene.words[111].xyz);
    if(any(extent==0u)||any(extent>16384u)||any(pixel>=extent)||any(!isfinite(reciprocal))||any(reciprocal<=0.f)||
       any(abs(reciprocal-expected)>expected*1e-5f)||(flags&~127u)!=0u||(waterInputs&~31u)!=0u||(samplers&~127u)!=0u||(sceneImages&~16383u)!=0u||
       any(!isfinite(gains))||any(gains<0.f)||any(!isfinite(sun))||any(sun<0.f))return false;
    const bool sunlight=gains.x>0.f&&any(sun!=0.f),gi=gains.z>0.f&&(flags&8u)!=0u;
    NativeMToonSceneModulation working;working.sunlight=1;working.gi=1;working.available=true;
    // These units follow actual disabled native branches, never absent maps.
    if((flags&64u)==0u||(!sunlight&&!gi)||scene.words[21].x==0u){result=working;return true;}
    if((waterInputs&6u)!=6u)return false;
    float factor;if(!native_mtoon_water_factor(scene,position,pixel,surface,depth,factor))return false;
    if(factor<=0.f){result=working;return true;}
    if(!quadMappingQualified||waterInputs!=31u||(samplers&96u)!=96u||(sceneImages&(1u<<10))==0u)return false;
    uint width,height;mainDepth.GetDimensions(width,height);if(any(uint2(width,height)!=extent))return false;
    float2 planar,uv,peerX,peerY;
    if(!native_mtoon_water_coordinates(scene,position,planar,uv)||
       !native_mtoon_water_peer(scene,uint2(pixel.x^1u,pixel.y),extent,mainDepth,surface,depth,peerX)||
       !native_mtoon_water_peer(scene,uint2(pixel.x,pixel.y^1u),extent,mainDepth,surface,depth,peerY))return false;
    const float period=asfloat(water.words[37].x),projectionScale=asfloat(water.words[37].y),layerBias=asfloat(water.words[37].z);
    if(!isfinite(period)||period<=0.f||!isfinite(projectionScale)||!isfinite(layerBias))return false;
    const float2 projectedPlanar=planar*projectionScale;const float radius=max(abs(projectedPlanar.x),abs(projectedPlanar.y));
    if(!isfinite(radius))return false;
    const float layerValue=radius>0.f?clamp(ceil(log2(radius)+layerBias),0.f,8.f):0.f;
    const uint layer=uint(layerValue),base=layer*4u;const float layerScale=exp2(layerValue),wrapPeriod=period*layerScale;
    const float4 row0=asfloat(water.words[base]),row1=asfloat(water.words[base+1u]),row2=asfloat(water.words[base+2u]);
    const float2 offset=asfloat(water.words[base+3u].xy),depthMap=asfloat(water.words[38].xy);
    if(any(!isfinite(row0))||any(!isfinite(row1))||any(!isfinite(row2))||any(!isfinite(offset))||any(!isfinite(depthMap))||!isfinite(wrapPeriod)||wrapPeriod<=0.f)return false;
    const float4 relative=float4(position.relative,1.f);const float3 projected=float3(dot(row0,relative),dot(row1,relative),dot(row2,relative));
    const float2 shadowUv=(projected.xy*projectionScale+fmod(offset,wrapPeriod))/layerScale*float2(.5f,-.5f)+.5f;
    if(any(!isfinite(projected))||any(!isfinite(shadowUv)))return false;
    uint shadowWidth,shadowHeight,layers;shadow.GetDimensions(shadowWidth,shadowHeight,layers);if(!shadowWidth||!shadowHeight||layers<9u||layers>16u)return false;
    const float sampledDepth=shadow.SampleLevel(shadowSampler,float3(shadowUv,layerValue),0);
    const float decodedDepth=sampledDepth*depthMap.x+depthMap.y,difference=projected.z-decodedDepth;
    if(!isfinite(sampledDepth)||!isfinite(decodedDepth)||!isfinite(difference))return false;
    const float distance=max(difference,0.f);
    const float derivativeScale=asfloat(scene.words[122].x),depthScale=asfloat(scene.words[122].y);
    const float2 gainBias=asfloat(scene.words[121].zw);
    if(!isfinite(derivativeScale)||!isfinite(depthScale)||any(!isfinite(gainBias)))return false;
    const float2 dx=(peerX-uv)*derivativeScale,dy=(peerY-uv)*derivativeScale;
    const float depthFootprint=distance*depthScale;
    if(any(!isfinite(dx))||any(!isfinite(dy))||!isfinite(depthFootprint))return false;
    const float footprint=max(max(dot(dx,dx),dot(dy,dy)),depthFootprint*depthFootprint);
    if(!isfinite(footprint)||footprint<=0.f)return false;
    const float lod=.5f*log2(footprint);if(!isfinite(lod))return false;
    uint cw,ch,levels;caustic.GetDimensions(0,cw,ch,levels);if(!cw||!ch||!levels)return false;
    const float3 color=caustic.SampleLevel(causticSampler,uv,lod).xyz*gainBias.x+gainBias.y;
    if(gi){if(!isfinite(color.y)||color.y<0.f)return false;working.gi=lerp(1.f,pow(color.y,.3f),factor);if(!isfinite(working.gi)||working.gi<0.f)return false;}
    if(sunlight){
        const float3 absorption=asfloat(water.words[39].xyz);
        if(any(!isfinite(color))||any(color<0.f)||any(!isfinite(absorption)))return false;
        working.sunlight=lerp(float3(1,1,1),color*exp2(absorption*distance),factor);
        if(any(!isfinite(working.sunlight))||any(working.sunlight<0.f))return false;
    }
    result=working;return true;
}
#endif
