// Math-only entry point for the existing reviewed 96-byte/64-byte probe ABI.
// No sampled textures, game resources, rasterization or presentation.
#include "../shaders/native_material_radiance.hlsli"
struct ProbeInput {float4 normal,tangent,uv,view,cameraUp,baseUv;};
struct ProbeOutput {float4 normal,lit,unlit,rgba;};
struct ProbeControl {uint sampleCount,width,height,workload;float4 diffuse;};
[[vk::binding(19,0)]] StructuredBuffer<ProbeInput> probeInputs;
[[vk::binding(20,0)]] RWStructuredBuffer<ProbeOutput> probeOutputs;
[[vk::binding(21,0)]] ConstantBuffer<ProbeControl> probeControl;
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
    if(id.x>=probeControl.width*probeControl.height)return;
    const ProbeInput p=probeInputs[id.x%probeControl.sampleCount];
    float3 colour;
    if(p.uv.w==2.f)colour=evrm_reflective_scale(p.uv.x,p.uv.y);
    else if(p.uv.w==1.f)
        colour=(p.baseUv.xyz+p.cameraUp.xyz*evrm_reflective_scale(p.uv.x,p.uv.y))*(p.uv.x*p.uv.y);
    else colour=evrm_forward_radiance(p.normal.xyz,p.tangent.xyz,p.view.xyz,p.baseUv.xyz,p.cameraUp.xyz,p.uv.x,p.uv.z!=0.f);
    ProbeOutput result;
    result.normal=float4(0,0,1,0);result.lit=result.unlit=0;
    result.rgba=float4(colour,p.normal.w);probeOutputs[id.x]=result;
}
