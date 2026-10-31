// Execute the production GBuffer material function on the GPU. The fixture
// samples one uploaded texel explicitly because compute has no raster derivatives.
// Raster/discard behavior is outside this material-output probe; production
// shader modules remain unchanged. CPU constants cover mask/BLEND preservation.
#define EVRM_NATIVE_MATERIAL_UBO 1
#define Sample(sampler,uv) Load(int3(0,0,0))
#define clip(expression)
#include "../shaders/avatar_native.hlsl"
[[vk::binding(4,0)]] RWStructuredBuffer<float4> result;
[numthreads(1,1,1)] void cs_material() {
    VertexOut input=(VertexOut)0;input.normal=float3(0,0,1);input.uv=float2(.5,.5);input.previousUvW.z=-1;
    GBufferOut output=ps_gbuffer(input,true);
    result[0]=output.base;result[1]=output.normal;result[2]=float4(output.params);result[3]=float4(output.emissive,output.motion,output.instance);
}
