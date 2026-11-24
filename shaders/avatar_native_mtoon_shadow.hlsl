// Owned main-geometry caster. Uses the same coherent authored/fallback choice,
// raw UVs, material transform/animation/cutoff and time as the visible MToon
// material. Outline extrusion is deliberately absent from this depth pass.
#define EVRM_MTOON_DESCRIPTOR_SET 3
#include "mtoon_material.hlsli"
#include "native_mtoon_raster.hlsli"
struct MToonShadowFrame {
    float4 currentRows[4],previousUvRows[4],normalRows[3];float4 screen;
    uint4 control,rootScale;
};
struct MToonShadowMaterial {float4 base,emissive,surface;uint4 instance;float4 alpha;};
struct MToonShadowLight {float4 prefix[16];uint4 origin;float4 projectionRows[4];float4 tail;};
struct MToonShadowControl {uint4 facts;float4 time;};
[[vk::binding(0,0)]] ConstantBuffer<MToonShadowFrame> frame;
[[vk::binding(1,0)]] Texture2D<float4> baseTexture;
[[vk::binding(2,0)]] SamplerState baseSampler;
[[vk::binding(3,0)]] ConstantBuffer<MToonShadowMaterial> material;
[[vk::binding(4,0)]] ByteAddressBuffer vertices;
[[vk::binding(5,0)]] ByteAddressBuffer indices;
[[vk::binding(0,1)]] ConstantBuffer<MToonShadowLight> light;
[[vk::binding(0,2)]] ConstantBuffer<NativeMToonRasterUniform> draw;
[[vk::binding(2,2)]] ByteAddressBuffer rawUvs;
[[vk::binding(7,2)]] ConstantBuffer<MToonShadowControl> control;
struct MToonShadowVertex {
    float4 position:SV_Position;
    [[vk::location(0)]] float2 uv:TEXCOORD0;
    [[vk::location(1)]] float2 baseUv:TEXCOORD1;
    [[vk::location(2)]] float2 maskUv:TEXCOORD2;
    [[vk::location(3)]] nointerpolation uint valid:TEXCOORD3;
};
bool mtoon_shadow_common(){return frame.control.x==1u&&material.instance.w==1u;}
bool mtoon_shadow_authored(){return control.facts.w==1u;}
bool mtoon_shadow_material(){return draw.uv[2].w==1u&&material.alpha.w==0.f&&mtoonMaterial.modes.x==1u&&
    mtoonMaterial.modes.y<=1u&&mtoonMaterial.modes.z<=1u&&mtoonMaterial.modes.w<=2u&&isfinite(draw.time.x);}
MToonShadowVertex vs_shadow(uint id:SV_VertexID){
    MToonShadowVertex o=(MToonShadowVertex)0;o.position=float4(2,2,0,1);
    if(!mtoon_shadow_common())return o;const bool authored=mtoon_shadow_authored();
    if(authored&&(!mtoon_shadow_material()||id<draw.geometry.z||id-draw.geometry.z>=draw.geometry.w))return o;
    uint indexBytes,vertexBytes;indices.GetDimensions(indexBytes);vertices.GetDimensions(vertexBytes);
    if(id>=indexBytes/4u)return o;const uint vertex=indices.Load(id*4u);if(vertex>=vertexBytes/44u)return o;
    if(authored&&(vertex<draw.geometry.x||vertex-draw.geometry.x>=draw.geometry.y))return o;
    float3 position=asfloat(vertices.Load3(vertex*44u));o.uv=asfloat(vertices.Load2(vertex*44u+24u));
    if(any(!isfinite(position))||any(!isfinite(o.uv)))return o;
    if(authored){
        if(mtoonMaterial.textures[0].info.y!=0u&&!native_mtoon_raster_uv(rawUvs,draw,vertex,0u,o.baseUv))return o;
        if(mtoonMaterial.textures[9].info.y!=0u&&!native_mtoon_raster_uv(rawUvs,draw,vertex,9u,o.maskUv))return o;
    }else if(material.alpha.w==3.f)position.x+=.8f; // Existing explicit diagnostic.
    const float scale=asfloat(frame.rootScale.w);if(!isfinite(scale)||scale<=0.f)return o;
    const float3 rotated=float3(dot(frame.normalRows[0].xyz,position),dot(frame.normalRows[1].xyz,position),dot(frame.normalRows[2].xyz,position));
    const float3 relative=float3(asint(frame.rootScale.xyz-light.origin.xyz))*(1.f/131072.f);
    const float4 world=float4(rotated*scale+relative,1.f);
    precise float4 raster=float4(dot(light.projectionRows[0],world),-dot(light.projectionRows[1],world),dot(light.projectionRows[2],world),dot(light.projectionRows[3],world));
    if(any(!isfinite(raster)))return o;o.position=raster;o.valid=1u;return o;
}
void ps_shadow(MToonShadowVertex input,bool front:SV_IsFrontFace){
    if(!mtoon_shadow_common()||input.valid!=1u)discard;
    if(mtoon_shadow_authored()){
        if(!mtoon_shadow_material())discard;
        const uint cull=mtoonMaterial.modes.w;if((cull==1u&&front)||(cull==2u&&!front))discard;
        if(mtoonMaterial.modes.z==1u){
            const float animation=evrm_mtoon_mask(9u,evrm_mtoon_sample_static(9u,input.maskUv));
            const float alpha=evrm_mtoon_sample(0u,input.baseUv,draw.time.x,animation).a*mtoonMaterial.base.a;
            if(!isfinite(animation)||!isfinite(alpha))discard;clip(alpha-mtoonMaterial.animation.w);
        }
    }else{
        if(material.alpha.z==0.f&&!front)discard;
        if(material.alpha.y!=0.f){const float alpha=baseTexture.Sample(baseSampler,input.uv).a*material.base.a;
            clip(alpha-(material.alpha.y==1.f?material.alpha.x:.5f));}
    }
}
