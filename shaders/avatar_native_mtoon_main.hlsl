#include "native_actor_basis.hlsli"
// Owned authored main-material coverage/auxiliary outputs. Native scene
// lighting and final composition remain separately admitted consumers.
#define EVRM_MTOON_DESCRIPTOR_SET 1
#include "native_mtoon_reconstruct.hlsli"
#include "native_mtoon_visibility.hlsli"
struct NativeMainMaterial {float4 base,emissive,surface;uint4 instance;float4 alpha;};
struct NativeMainControl {uint4 facts;float4 time;};
[[vk::binding(0,0)]] ConstantBuffer<NativeMToonPreparedFrame> mainFrame;
[[vk::binding(3,0)]] ConstantBuffer<NativeMainMaterial> mainMaterial;
[[vk::binding(4,0)]] ByteAddressBuffer mainVertices;
[[vk::binding(5,0)]] ByteAddressBuffer mainIndices;
[[vk::binding(11,0)]] ByteAddressBuffer mainSurface;
[[vk::binding(0,2)]] ConstantBuffer<NativeMToonRasterUniform> mainDraw;
[[vk::binding(1,2)]] RWByteAddressBuffer mainIdentity;
[[vk::binding(2,2)]] ByteAddressBuffer mainRawUvs;
[[vk::binding(7,2)]] ConstantBuffer<NativeMainControl> mainControl;
struct NativeMainVertexOut {
    float4 position:SV_Position;
    [[vk::location(0)]] float3 normal:NORMAL0;
    [[vk::location(1)]] float2 baseUv:TEXCOORD0;
    [[vk::location(2)]] float2 maskUv:TEXCOORD1;
    [[vk::location(3)]] float3 previousUvW:TEXCOORD2;
    [[vk::location(4)]] nointerpolation uint valid:TEXCOORD3;
    [[vk::location(5)]] float3 relativePosition:TEXCOORD4;
    [[vk::location(6)]] float4 tangent:TEXCOORD5;
    [[vk::location(7)]] float2 normalUv:TEXCOORD6;
    [[vk::location(8)]] float2 emissionUv:TEXCOORD7;
};
#ifndef EVRM_MTOON_FORWARD_CAPTURE
#define EVRM_MTOON_FORWARD_CAPTURE 0
#endif
bool native_main_active(){
#if EVRM_MTOON_FORWARD_CAPTURE
    return mainControl.facts.w==2u&&mainControl.facts.y!=0u&&mainFrame.control.x==1u&&mainMaterial.instance.w==1u&&mainMaterial.alpha.w==0.f&&
        mainDraw.uv[2].w==1u&&mtoonMaterial.modes.x==1u&&mtoonMaterial.modes.y<=1u&&mtoonMaterial.modes.z==2u&&all(mtoonMaterial.flags.zw==uint2(0,0));
#else
    return mainControl.facts.w==1u&&mainFrame.control.x==1u&&mainMaterial.instance.w==1u&&mainMaterial.alpha.w==0.f&&
        mainDraw.uv[2].w==1u&&mtoonMaterial.modes.x==1u&&mtoonMaterial.modes.y<=1u&&mtoonMaterial.modes.z<=1u;
#endif
}
NativeMainVertexOut vs_native(uint vertexId:SV_VertexID){
    NativeMainVertexOut o=(NativeMainVertexOut)0;o.position=float4(2,2,0,1);o.previousUvW.z=-1.f;
    if(!native_main_active()||mainDraw.geometry.w==0u||mainDraw.geometry.w%3u!=0u||mainDraw.domain.w!=mainDraw.geometry.w/3u||
        any(mainDraw.domain.xy==0u)||any(mainDraw.domain.xy>16384u)||!isfinite(mainDraw.time.x)||
        vertexId<mainDraw.geometry.z||vertexId-mainDraw.geometry.z>=mainDraw.geometry.w)return o;
    uint indexBytes,vertexBytes;mainIndices.GetDimensions(indexBytes);mainVertices.GetDimensions(vertexBytes);
    if(vertexId>=indexBytes/4u)return o;const uint vertex=mainIndices.Load(vertexId*4u);
    if(vertex<mainDraw.geometry.x||vertex-mainDraw.geometry.x>=mainDraw.geometry.y||vertex>=vertexBytes/44u)return o;
    const float3 position=asfloat(mainVertices.Load3(vertex*44u));const float3 sourceNormal=asfloat(mainVertices.Load3(vertex*44u+12u));
    const float scale=asfloat(mainFrame.rootScale.w);
    if(any(!isfinite(position))||any(!isfinite(sourceNormal))||all(sourceNormal==0.f)||!isfinite(scale)||scale<=0.f||any(!isfinite(mainFrame.screen))||
        any(abs(mainFrame.screen.xy-1.f/float2(mainDraw.domain.xy))>(1.f/float2(mainDraw.domain.xy))*1e-5f))return o;
    [unroll]for(uint row=0u;row<4u;++row)if(any(!isfinite(mainFrame.currentRows[row])))return o;
    [unroll]for(uint rowN=0u;rowN<3u;++rowN)if(any(!isfinite(mainFrame.normalRows[rowN])))return o;
    const float4 p=float4(position,1.f);
    precise float4 clipPosition=float4(dot(mainFrame.currentRows[0],p),-dot(mainFrame.currentRows[1],p),dot(mainFrame.currentRows[2],p),dot(mainFrame.currentRows[3],p));
    if(any(!isfinite(clipPosition)))return o;
    const float3x3 rotation=float3x3(mainFrame.normalRows[0].xyz,mainFrame.normalRows[1].xyz,mainFrame.normalRows[2].xyz);
    o.normal=evrm_mtoon_unit(mul(rotation,sourceNormal),0);if(all(o.normal==0.f))return o;
    o.relativePosition=evrm_surface_relative_position(rotation,position,scale,float3(mainFrame.normalRows[0].w,mainFrame.normalRows[1].w,mainFrame.normalRows[2].w));
    if(any(!isfinite(o.relativePosition))||all(o.relativePosition==0.f))return o;
    if(mtoonMaterial.textures[0].info.y!=0u&&!native_mtoon_raster_uv(mainRawUvs,mainDraw,vertex,0u,o.baseUv))return o;
    if(mtoonMaterial.textures[9].info.y!=0u&&!native_mtoon_raster_uv(mainRawUvs,mainDraw,vertex,9u,o.maskUv))return o;
    if(mtoonMaterial.textures[5].info.y!=0u&&!native_mtoon_raster_uv(mainRawUvs,mainDraw,vertex,5u,o.emissionUv))return o;
    if(mtoonMaterial.textures[2].info.y!=0u){
        uint surfaceBytes;mainSurface.GetDimensions(surfaceBytes);if(vertex>=surfaceBytes/32u)return o;
        const float4 tangent=asfloat(mainSurface.Load4(vertex*32u+16u));
        // A finite zero sign is the importer's intentional disabled normal-map
        // basis for degenerate UVs. Keep its original geometry and use N.
        if(any(!isfinite(tangent))||(tangent.w!=0.f&&abs(tangent.w)!=1.f)||!native_mtoon_raster_uv(mainRawUvs,mainDraw,vertex,2u,o.normalUv))return o;
        o.tangent=evrm_actor_tangent(rotation,tangent);if(any(!isfinite(o.tangent)))return o;
    }
    if(mainFrame.control.y==1u){
        const float3 previousPosition=asfloat(mainVertices.Load3(vertex*44u+32u));const float4 old=float4(previousPosition,1.f);
        const float3 uvw=float3(dot(mainFrame.previousUvRows[0],old),dot(mainFrame.previousUvRows[1],old),dot(mainFrame.previousUvRows[3],old));
        if(all(isfinite(previousPosition))&&all(isfinite(uvw))&&uvw.z>0.f)o.previousUvW=uvw;
    }
    o.position=clipPosition;o.valid=1u;return o;
}
float4 native_main_base(NativeMainVertexOut input,out float animation){
    clip(native_main_active()&&input.valid==1u?1.f:-1.f);
    animation=evrm_mtoon_mask(9u,evrm_mtoon_sample_static(9u,input.maskUv));
    const float4 base=evrm_mtoon_sample(0u,input.baseUv,mainDraw.time.x,animation)*mtoonMaterial.base;
    if(!isfinite(animation)||any(!isfinite(base)))discard;
    if(mtoonMaterial.modes.z==1u)clip(base.a-mtoonMaterial.animation.w);
    return base;
}
uint ps_visibility(NativeMainVertexOut input):SV_Target0{
    float animation;native_main_base(input,animation);return 0x08000000u|(mainMaterial.instance.x&255u);
}
// Standard RGB9E5 native emission target; keep authored emission separate from
// exposure and scene gain, which the following native Deferred stage supplies.
uint native_main_rgb9e5(float3 value){
    value=clamp(value,0.f,65408.f);const float largest=max(value.x,max(value.y,value.z));if(largest==0.f)return 0u;
    const uint ieeeExponent=asuint(largest)>>23u;uint exponent=ieeeExponent>111u?ieeeExponent-111u:0u;
    float quantum=asfloat((exponent+103u)<<23u);if(uint(round(largest/quantum))==512u){exponent+=1u;quantum*=2.f;}
    const uint3 mantissa=uint3(round(value/quantum));return mantissa.x|(mantissa.y<<9u)|(mantissa.z<<18u)|(exponent<<27u);
}
struct NativeMainGBuffer {float4 base:SV_Target0;float4 normal:SV_Target1;uint4 params:SV_Target2;uint emissive:SV_Target3;float2 motion:SV_Target4;float instance:SV_Target5;};
[earlydepthstencil]
NativeMainGBuffer ps_gbuffer(NativeMainVertexOut input,bool front:SV_IsFrontFace,uint primitive:SV_PrimitiveID){
    float animation;const float4 base=native_main_base(input,animation);
    float3 normal=evrm_mtoon_unit(mtoonMaterial.modes.y==1u&&!front?-input.normal:input.normal,0);
    if(mtoonMaterial.textures[2].info.y!=0u)normal=evrm_mtoon_normal(normal,input.tangent,evrm_mtoon_sample(2u,input.normalUv,mainDraw.time.x,animation).xyz,mtoonMaterial.indirect.w);
    if(mtoonMaterial.modes.y==0u&&dot(-input.relativePosition,normal)<0.f)normal=-normal;
    const float3 emission=evrm_mtoon_sample(5u,input.emissionUv,mainDraw.time.x,animation).rgb*mtoonMaterial.emission.rgb;
    if(any(!isfinite(normal))||all(normal==0.f)||any(!isfinite(emission)))discard;
    const float3 folded=normal/(abs(normal.x)+abs(normal.y)+abs(normal.z));NativeMainGBuffer output;
    output.base=float4(base.rgb,mainMaterial.surface.w);
    output.normal=float4(.5f*(folded.x+folded.y)+.5f,.5f*(folded.y-folded.x)+.5f,mainMaterial.surface.y,normal.z>0.f?2.f/3.f:0.f);
    output.params=uint4(uint3(saturate(mainMaterial.surface.xyz)*255.f+.5f),(mainMaterial.instance.z&100u)|8u);
    output.emissive=native_main_rgb9e5(emission);
    output.motion=input.previousUvW.z>0.f?input.previousUvW.xy/input.previousUvW.z-(input.position.xy*mainFrame.screen.xy+mainFrame.screen.zw):float2(0,0);
    output.instance=(mainMaterial.instance.y&255u)!=0u?1.f:0.f;
    NativeMToonVisibilityUniform identity={mainDraw.domain.x,mainDraw.domain.y,mainDraw.domain.z,mainDraw.domain.w};
    native_mtoon_record_surface(mainIdentity,identity,input.position,primitive,front,true);return output;
}
