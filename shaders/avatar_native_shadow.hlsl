// Generic loaded-mesh caster. Same immutable geometry/materials and common
// GPU-validated actor/basis/scale as the visible avatar. No sample-specific data.
struct ShadowFrame {
    float4 currentRows[4],previousUvRows[4],normalRows[3];float4 screen;
    uint4 control,rootScale;
};
struct ShadowMaterial {
    float4 base,emissive,surface;uint4 instance;float4 alpha;
};
struct ShadowView {
    float4 prefix[16];uint4 origin;float4 projectionRows[4];float4 tail;
};
[[vk::binding(0,0)]] ConstantBuffer<ShadowFrame> frame;
[[vk::binding(1,0)]] Texture2D<float4> baseTexture;
[[vk::binding(2,0)]] SamplerState baseSampler;
[[vk::binding(3,0)]] ConstantBuffer<ShadowMaterial> material;
[[vk::binding(4,0)]] ByteAddressBuffer vertices;
[[vk::binding(5,0)]] ByteAddressBuffer indices;
[[vk::binding(0,1)]] ConstantBuffer<ShadowView> light;
struct ShadowVertexOut {float4 position:SV_Position; [[vk::location(0)]] float2 uv:TEXCOORD0;};
ShadowVertexOut vs_shadow(uint id:SV_VertexID){
    ShadowVertexOut o;o.position=float4(2,2,0,1);o.uv=0;
    if(frame.control.x!=1u||material.instance.w!=1u)return o;
    const uint offset=indices.Load(id*4u)*44u;
    float3 position=asfloat(vertices.Load3(offset));o.uv=asfloat(vertices.Load2(offset+24u));
    if(material.alpha.w==3.f)position.x+=.8f; // Existing explicit geometry-offset diagnostic only.
    const float3 rotated=float3(dot(frame.normalRows[0].xyz,position),dot(frame.normalRows[1].xyz,position),dot(frame.normalRows[2].xyz,position));
    const float3 relative=float3(asint(frame.rootScale.xyz-light.origin.xyz))*(1.f/131072.f);
    const float4 world=float4(rotated*asfloat(frame.rootScale.w)+relative,1.f);
    precise float4 raster=float4(dot(light.projectionRows[0],world),-dot(light.projectionRows[1],world),dot(light.projectionRows[2],world),dot(light.projectionRows[3],world));
    o.position=all(isfinite(raster))?raster:float4(2,2,0,1);
    return o;
}
void ps_shadow(ShadowVertexOut input,bool frontFace:SV_IsFrontFace){
    if(frame.control.x!=1u||material.instance.w!=1u)discard;
    if(material.alpha.z==0.f&&!frontFace)discard;
    // OPAQUE ignores alpha, MASK preserves the loaded cutoff. Initial BLEND
    // caster is binary at0.5 rather than a fully opaque transparent card.
    if(material.alpha.y!=0.f){
        const float alpha=baseTexture.Sample(baseSampler,input.uv).a*material.base.a;
        clip(alpha-(material.alpha.y==1.f?material.alpha.x:.5f));
    }
}
