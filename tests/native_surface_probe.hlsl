// Material-kernel fixture: actual production sampling, no raster/game dependency.
#ifndef EVRM_PROBE_ENHANCED
#define EVRM_PROBE_ENHANCED 0
#endif
struct ProbeMaterial {float4 base,emissive,surface;uint4 instance;float4 alpha;};
[[vk::binding(1,0)]] Texture2D<float4> baseTexture;
[[vk::binding(2,0)]] SamplerState baseSampler;
[[vk::binding(3,0)]] ConstantBuffer<ProbeMaterial> material;
#if EVRM_PROBE_ENHANCED
#include "../shaders/native_surface.hlsli"
#endif
struct ProbeInput {float4 normal,tangent,uv,view,cameraUp,baseUv;};
struct ProbeOutput {float4 normal,lit,unlit,rgba;};
[[vk::binding(19,0)]] StructuredBuffer<ProbeInput> probeInputs;
[[vk::binding(20,0)]] RWStructuredBuffer<ProbeOutput> probeOutputs;
struct ProbeControl {uint sampleCount,width,height,workload;float4 diffuse;};
[[vk::binding(21,0)]] ConstantBuffer<ProbeControl> probeControl;
[numthreads(64,1,1)]
void main(uint3 dispatch:SV_DispatchThreadID) {
    uint index=dispatch.x;
    if(index>=probeControl.width*probeControl.height)return;
    ProbeInput p=probeInputs[index%probeControl.sampleCount];
    if(probeControl.workload!=0) {
        float2 uv=(float2(index%probeControl.width,index/probeControl.width)+0.5)/float2(probeControl.width,probeControl.height);
        p.uv=float4(uv,uv);p.baseUv.xy=uv;
    }
    float4 base=baseTexture.SampleLevel(baseSampler,p.baseUv.xy,0)*material.base;
    ProbeOutput result;
    result.normal=float4(normalize(p.normal.xyz),0);
    result.lit=0;result.unlit=0;
#if EVRM_PROBE_ENHANCED
    EvrmSurfaceInput input;
    input.normal=p.normal.xyz;input.tangent=p.tangent;
    input.normalUv=p.uv.xy;input.rimUv=p.uv.zw;
    input.view=p.view.xyz;input.cameraUp=p.cameraUp.xyz;
    EvrmSurfaceResult sampled=evrm_surface_sample(input,p.normal.w!=0);
    result.normal=float4(sampled.normal,0);
    result.lit=float4(sampled.lit,0);result.unlit=float4(sampled.unlit,0);
#endif
    result.rgba=float4((base.rgb+result.lit.rgb)*probeControl.diffuse.rgb+material.emissive.rgb+result.unlit.rgb,base.a);
    probeOutputs[index]=result;
}
