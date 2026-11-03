// Original native-format output contract for the independent offscreen proof.
// Row vectors below multiply column positions with dot(row, float4(position,1)).
// Both passes use this same current raster transform and final clip-Y flip.
// Previous rows already project into native UV-homogeneous space, not NDC.
// Inputs must be finite, with nonzero normals and valid coherent resources.
// This implements checked opaque/mask PBR presets, not arbitrary MToon,
// weather/highlight effects, alternate-secondary-normal mode, or transparency.
#ifndef EVRM_NATIVE_SURFACE
#define EVRM_NATIVE_SURFACE 0
#endif
#include "native_material_radiance.hlsli"
struct NativeFrame {
    float4 currentRows[4]; float4 previousUvRows[4]; float4 normalRows[3];
    float4 screen; // inverse framebuffer size.xy, native jitter/bias UV.zw
    uint4 control; // common commit, history valid, float bits: exposure/emissive gain
#if EVRM_NATIVE_SURFACE
    uint4 rootScale; // existing224-byte frame tail; scale is asfloat(w)
#endif
};
struct NativeMaterial {
    float4 base; float4 emissive; // RGB real emission; w legacy diffuse policy0/1
    float4 surface; // metallic, reflectance/G, transmission, occlusion
    // instance: uint@112, uint@40, preselected output mode bits(4/32/64),
    //           draw role(0=native stand-in, 1=avatar).
    uint4 instance;
    float4 alpha; // cutoff, mask enabled, double-sided lighting, opt-in diagnostic mode
};
#ifndef EVRM_NATIVE_DESCRIPTOR_SET
#define EVRM_NATIVE_DESCRIPTOR_SET 0
#endif
#ifndef EVRM_NATIVE_MATERIAL_UBO
#define EVRM_NATIVE_MATERIAL_UBO 0
#endif
#ifndef EVRM_NATIVE_VERTEX_PULL
#define EVRM_NATIVE_VERTEX_PULL 0
#endif
// The fixture uses set 0; runtime compilation can select its private set.
[[vk::binding(0,EVRM_NATIVE_DESCRIPTOR_SET)]] ConstantBuffer<NativeFrame> frame;
[[vk::binding(1,EVRM_NATIVE_DESCRIPTOR_SET)]] Texture2D<float4> baseTexture;
[[vk::binding(2,EVRM_NATIVE_DESCRIPTOR_SET)]] SamplerState baseSampler;
#if EVRM_NATIVE_MATERIAL_UBO
[[vk::binding(3,EVRM_NATIVE_DESCRIPTOR_SET)]] ConstantBuffer<NativeMaterial> material;
#else
[[vk::push_constant]] ConstantBuffer<NativeMaterial> material;
#endif
#if EVRM_NATIVE_VERTEX_PULL
// Byte layouts match the CPU geometry contract. SV_VertexID includes the
// nonindexed draw's firstVertex, which selects the start of its index range.
[[vk::binding(4,EVRM_NATIVE_DESCRIPTOR_SET)]] ByteAddressBuffer nativeVertices;
[[vk::binding(5,EVRM_NATIVE_DESCRIPTOR_SET)]] ByteAddressBuffer nativeIndices;
#endif
#if EVRM_NATIVE_SURFACE
#define EVRM_SURFACE_DESCRIPTOR_SET EVRM_NATIVE_DESCRIPTOR_SET
#include "native_surface.hlsli"
#include "native_surface_camera.hlsli"
[[vk::binding(11,EVRM_NATIVE_DESCRIPTOR_SET)]] ByteAddressBuffer surfaceVertices;
#endif
struct VertexIn {
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float3 previousPosition : POSITION1;
};
struct VertexOut {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal : NORMAL0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 previousUvW : TEXCOORD1;
#if EVRM_NATIVE_SURFACE
    [[vk::location(3)]] float4 tangent : TEXCOORD2;
    [[vk::location(4)]] float2 normalUv : TEXCOORD3;
    [[vk::location(5)]] float2 rimUv : TEXCOORD4;
    [[vk::location(6)]] float3 view : TEXCOORD5;
    [[vk::location(7)]] nointerpolation float3 cameraUp : TEXCOORD6;
#endif
};
#if EVRM_NATIVE_VERTEX_PULL
VertexOut vs_native(uint vertexId : SV_VertexID) {
    const uint offset=nativeIndices.Load(vertexId*4u)*44u;
    VertexIn input;
    input.position=asfloat(nativeVertices.Load3(offset));
    input.normal=asfloat(nativeVertices.Load3(offset+12u));
    input.uv=asfloat(nativeVertices.Load2(offset+24u));
    input.previousPosition=asfloat(nativeVertices.Load3(offset+32u));
#else
VertexOut vs_native(VertexIn input) {
#endif
    VertexOut o;
    if(material.instance.w!=0u&&material.alpha.w==3.f) {
        input.position.x+=.8f;
        input.previousPosition.x+=.8f;
    }
    const float4 p=float4(input.position,1);
    precise float4 raster=float4(dot(frame.currentRows[0],p),-dot(frame.currentRows[1],p),dot(frame.currentRows[2],p),dot(frame.currentRows[3],p));
    const bool active=(frame.control.x!=0)==(material.instance.w!=0);
    o.position=active?raster:float4(2,2,0,1);
    o.normal=float3(dot(frame.normalRows[0].xyz,input.normal),dot(frame.normalRows[1].xyz,input.normal),dot(frame.normalRows[2].xyz,input.normal));
    o.uv=input.uv;
#if EVRM_NATIVE_SURFACE
    const float3x3 rotation=float3x3(frame.normalRows[0].xyz,frame.normalRows[1].xyz,frame.normalRows[2].xyz);
    const float3 relative=float3(frame.normalRows[0].w,frame.normalRows[1].w,frame.normalRows[2].w);
    o.view=-evrm_surface_relative_position(rotation,input.position,asfloat(frame.rootScale.w),relative);
    o.cameraUp=evrm_surface_camera_up(rotation,frame.currentRows[1],frame.currentRows[3]);
    o.tangent=0;o.normalUv=o.rimUv=0;
    if((surfaceMaterial.flags.x&9u)!=0){
        const uint surfaceAt=nativeIndices.Load(vertexId*4u)*32u;
        o.normalUv=asfloat(surfaceVertices.Load2(surfaceAt));o.rimUv=asfloat(surfaceVertices.Load2(surfaceAt+8u));
        const float4 tangent=asfloat(surfaceVertices.Load4(surfaceAt+16u));
        o.tangent=float4(mul(rotation,tangent.xyz),tangent.w);
    }
#endif
    const float4 previous=float4(input.previousPosition,1);
    o.previousUvW=float3(dot(frame.previousUvRows[0],previous),dot(frame.previousUvRows[1],previous),dot(frame.previousUvRows[3],previous));
    if(frame.control.y==0)o.previousUvW.z=-1;
    return o;
}
float4 sample_base(VertexOut input) {
    // The same decision and alpha test guard both Visibility and GBuffer.
    const bool active=(frame.control.x!=0)==(material.instance.w!=0);
    clip(active?1.f:-1.f);
    const float4 base=baseTexture.Sample(baseSampler,input.uv)*material.base;
    if(material.alpha.y!=0)clip(base.a-material.alpha.x);
    return base;
}
uint ps_visibility(VertexOut input) : SV_Target0 {
    sample_base(input);
    return 0x08000000u|(material.instance.x&255u);
}
// Shared-exponent RGB9E5, with the standard 9-bit mantissas and exponent bias.
uint pack_rgb9e5(float3 value) {
    value=clamp(value,0.f,65408.f);
    const float largest=max(value.x,max(value.y,value.z));
    if(largest==0)return 0;
    // Select the shared power of two from the IEEE exponent, avoiding log2
    // approximation at bin boundaries. HLSL round is nearest, ties to even,
    // matching float32 bias-add packing rather than floor(x+.5).
    const uint ieeeExponent=asuint(largest)>>23;
    uint exponent=ieeeExponent>111u?ieeeExponent-111u:0u;
    float quantum=asfloat((exponent+103u)<<23);
    if(uint(round(largest/quantum))==512u){exponent+=1;quantum*=2;}
    const uint3 mantissa=uint3(round(value/quantum));
    return mantissa.x|(mantissa.y<<9)|(mantissa.z<<18)|(exponent<<27);
}
struct GBufferOut {float4 base:SV_Target0;float4 normal:SV_Target1;uint4 params:SV_Target2;uint emissive:SV_Target3;float2 motion:SV_Target4;float instance:SV_Target5;};
GBufferOut ps_gbuffer(VertexOut input,bool frontFace:SV_IsFrontFace) {
#if EVRM_NATIVE_SURFACE
    float4 base=sample_base(input);
#else
    const float4 base=sample_base(input);
#endif
    const bool backLighting=material.instance.w!=0 && material.alpha.z!=0 && !frontFace;
#if EVRM_NATIVE_SURFACE
    EvrmSurfaceInput surfaceInput;
    surfaceInput.normal=input.normal;surfaceInput.tangent=input.tangent;
    surfaceInput.normalUv=input.normalUv;surfaceInput.rimUv=input.rimUv;
    surfaceInput.view=input.view;surfaceInput.cameraUp=input.cameraUp;
    const EvrmSurfaceResult detailed=evrm_surface_sample(surfaceInput,backLighting);
    base.rgb+=detailed.lit;const float3 normal=detailed.normal;
#else
    const float3 normal=normalize(backLighting?-input.normal:input.normal);
#endif
    const float3 folded=normal/(abs(normal.x)+abs(normal.y)+abs(normal.z));
    const bool history=input.previousUvW.z>0;
    GBufferOut o;
    o.base=float4(base.rgb,material.surface.w);
    o.normal=float4(.5f*(folded.x+folded.y)+.5f,.5f*(folded.y-folded.x)+.5f,material.surface.y,normal.z>0?2.f/3.f:0.f);
    // Bit8 preserves the object-motion target through the native camera-only
    // MotionVector pass. An avatar owns its explicit zero on a reset frame too;
    // clearing this bit incorrectly treats a moving actor as static scenery.
    const bool objectMotion=history||material.instance.w!=0u;
    o.params=uint4(uint3(saturate(material.surface.xyz)*255.f+.5f),(material.instance.z&100u)|(objectMotion?8u:0u));
#if EVRM_NATIVE_SURFACE
    o.emissive=pack_rgb9e5(material.emissive.rgb+detailed.unlit*evrm_reflective_scale(asfloat(frame.control.z),asfloat(frame.control.w)));
#else
    o.emissive=pack_rgb9e5(material.emissive.rgb);
#endif
    o.motion=history?input.previousUvW.xy/input.previousUvW.z-(input.position.xy*frame.screen.xy+frame.screen.zw):float2(0,0);
    o.instance=(material.instance.y&255u)!=0?1.f:0.f;
    // Diagnostic only: retain all geometry/alpha/auxiliary outputs while
    // separating texture/post processing from diffuse/specular scene lighting.
    if(material.instance.w!=0u&&material.alpha.w==1.f) {
        o.base=float4(0,0,0,1);
        o.params.xyz=uint3(0,0,0);
        // Keep the diagnostic below the native night-exposure/bloom clipping
        // observed at unity radiance, so fine texture artifacts remain visible.
        o.emissive=pack_rgb9e5(base.rgb*(1.f/32.f));
    }
    if(material.instance.w!=0u&&material.alpha.w==2.f) {
        // Diagnostic only: retain diffuse lighting and its native shadows/AO,
        // while removing metallic/F0 and roughness-dependent grazing reflection.
        o.normal.z=1.f;
        o.params.xy=uint2(0,0);
    }
    return o;
}
