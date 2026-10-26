// Private continuous-alpha native-format OIT diffuse subset.
// No atmosphere, fog, water, specular or native auxiliary-mask semantics.
#include "avatar_native.hlsl"
#include "native_oit_limits.hlsli"
struct NativeTransparentConstants { float4 words[145]; };
struct NativeForwardConstants { float4 words[6]; };
[[vk::binding(6,EVRM_NATIVE_DESCRIPTOR_SET)]] ConstantBuffer<NativeTransparentConstants> transparentConstants;
[[vk::binding(7,EVRM_NATIVE_DESCRIPTOR_SET)]] ConstantBuffer<NativeForwardConstants> forwardConstants;
[[vk::binding(8,EVRM_NATIVE_DESCRIPTOR_SET)]] Texture3D<float4> pointRadiance;
[[vk::binding(9,EVRM_NATIVE_DESCRIPTOR_SET)]] Texture3D<float> directionalVisibility;
[[vk::binding(10,EVRM_NATIVE_DESCRIPTOR_SET)]] SamplerState volumeSampler;
struct OitOut { float4 accumulation:SV_Target0; float reveal:SV_Target4; };
OitOut ps_oit(VertexOut input) {
    // Control-flow guard must dominate all borrowed lighting resources in SPIR-V.
    if(frame.control.x==0 || material.instance.w==0)discard;
    OitOut o; o.accumulation=0; o.reveal=0;
    // Runtime owns a single mip (LOD0); explicit sampling avoids helper-lane
    // derivatives outside the common bounded UV/W contract.
    const float4 base=baseTexture.SampleLevel(baseSampler,input.uv,0)*material.base;
    if(!all(isfinite(base)) || !all(isfinite(material.emissive.rgb)))discard;
    const float a=saturate(base.a);
    if(a==0)return o;
    const float3 mapping=transparentConstants.words[114].xyz; // bytes1824..1832
    const float3 sunColor=transparentConstants.words[111].rgb; // byte1776
    const float sunFactor=transparentConstants.words[122].x; // byte1952
    const float exposure=transparentConstants.words[123].x; // byte1968
    const float2 invOutput=forwardConstants.words[4].zw; // byte72
    const float reciprocalW=input.position.w;
    if(!all(isfinite(mapping)) || !all(isfinite(sunColor)) || !isfinite(sunFactor) || !isfinite(exposure) || !all(isfinite(invOutput)) || !isfinite(reciprocalW) || reciprocalW<=0)discard;
    const float linearDepth=rcp(reciprocalW);
    const float domain=linearDepth*mapping.x+mapping.y;
    if(!isfinite(linearDepth) || !isfinite(domain) || domain<=0)discard;
    const float3 uvw=float3(input.position.xy*invOutput,log2(domain)*mapping.z);
    if(!all(isfinite(uvw)))discard;
    const float3 P=pointRadiance.SampleLevel(volumeSampler,uvw,0).rgb;
    const float visibility=directionalVisibility.SampleLevel(volumeSampler,uvw,0).r;
    if(!all(isfinite(P)) || !isfinite(visibility))discard;
    // VRM emissive is our material extension, before exposure/premultiply.
    // The native diffuse subset contains no Lambert normal/direction factor.
    const float3 L=(base.rgb*(P+sunColor*(visibility*sunFactor/(4.f*3.14159265358979323846f)))+material.emissive.rgb)*exposure;
    // Explicit bounded private HDR scope: uniform RGB scaling preserves hue/sign
    // and ordinary finite values; alpha and the native weight stay unchanged.
    if(!all(isfinite(L)))discard;
    const float3 boundedL=evrm_native_oit_limit_radiance(L);
    const float weight=clamp(pow(.9f*input.position.z,3.f),.01f,3000.f);
    const float4 accumulation=float4(boundedL*a,a)*weight;
    // Local numeric safeguard only. Whole-model common validation is separate.
    if(!all(isfinite(accumulation)) || any(abs(accumulation)>65504.f))discard;
    o.accumulation=accumulation;o.reveal=a;return o;
}
