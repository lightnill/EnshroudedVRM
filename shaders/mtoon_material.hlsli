#ifndef EVRM_MTOON_MATERIAL_INCLUDED
#define EVRM_MTOON_MATERIAL_INCLUDED
// MToon algorithms adapted from MIT UniVRM and factual VRM1 contracts.
// Copyright (c) 2020 VRM Consortium; (c) 2018 Masataka SUMI for MToon.
// Pinned provenance/license: docs/research/2026-11-05-mtoon-reference-provenance.md.
#ifndef EVRM_MTOON_DESCRIPTOR_SET
#define EVRM_MTOON_DESCRIPTOR_SET 0
#endif
struct EvrmMToonTexture {float4 offset_scale,rotation;uint4 info;};
struct EvrmMToonMaterial {
    float4 base,shade,emission,matcap,rim,outline;
    float4 shading,indirect,rim_settings,outline_settings,animation;
    uint4 modes,flags;
    int4 queue;
    EvrmMToonTexture textures[11];
};
// ABI v1: 752 bytes; scalar/vector offsets and array stride verified by the
// headless Vulkan probe as well as C++ static assertions.
[[vk::binding(0,EVRM_MTOON_DESCRIPTOR_SET)]] ConstantBuffer<EvrmMToonMaterial> mtoonMaterial;
[[vk::binding(1,EVRM_MTOON_DESCRIPTOR_SET)]] Texture2D<float4> mtoonTextures[11];
[[vk::binding(2,EVRM_MTOON_DESCRIPTOR_SET)]] SamplerState mtoonSamplers[11];
float4 evrm_mtoon_fallback(uint slot){
    if(slot==7u||slot==10u)return 0; // absent matcap / shift
    if(slot==2u)return float4(.5,.5,1,1);
    return 1;
}
float2 evrm_mtoon_transform(uint slot,float2 uv){
    const EvrmMToonTexture t=mtoonMaterial.textures[slot];uv*=t.offset_scale.zw;
    return t.offset_scale.xy+float2(t.rotation.x*uv.x-t.rotation.y*uv.y,t.rotation.y*uv.x+t.rotation.x*uv.y);
}
float2 evrm_mtoon_uv(uint slot,float2 uv,float seconds,float mask){
    if(slot==7u||slot==9u)return evrm_mtoon_transform(slot,uv); // view matcap / unanimated mask
    const bool legacy=mtoonMaterial.modes.y==0u;
    if(legacy){
        uv=evrm_mtoon_transform(slot,uv);
        if(slot==8u)return uv; // legacy vertex width sampler is static
    }
    const float time=seconds*mask;
    const float angle=mtoonMaterial.animation.z*time*(legacy?-6.283185307179586f:1.f);
    const float2 scroll=mtoonMaterial.animation.xy*time*float2(1,legacy?-1.f:1.f);
    if(legacy)uv+=scroll;
    float s,c;sincos(angle,s,c);uv-=.5f;uv=float2(c*uv.x-s*uv.y,s*uv.x+c*uv.y)+.5f;
    if(!legacy)uv=evrm_mtoon_transform(slot,uv+scroll);
    return uv;
}
float4 evrm_mtoon_texel(uint slot,float2 uv){
    if(slot>=11u)return 1;
    if(mtoonMaterial.textures[slot].info.y==0u||!all(isfinite(uv)))return evrm_mtoon_fallback(slot);
    // Constant descriptor indexes do not require optional dynamic sampled-image
    // indexing features. Sampler wrap/filter stays authored by each texture.
    switch(slot){
    case 0:return mtoonTextures[0].SampleLevel(mtoonSamplers[0],uv,0);
    case 1:return mtoonTextures[1].SampleLevel(mtoonSamplers[1],uv,0);
    case 2:return mtoonTextures[2].SampleLevel(mtoonSamplers[2],uv,0);
    case 3:return mtoonTextures[3].SampleLevel(mtoonSamplers[3],uv,0);
    case 4:return mtoonTextures[4].SampleLevel(mtoonSamplers[4],uv,0);
    case 5:return mtoonTextures[5].SampleLevel(mtoonSamplers[5],uv,0);
    case 6:return mtoonTextures[6].SampleLevel(mtoonSamplers[6],uv,0);
    case 7:return mtoonTextures[7].SampleLevel(mtoonSamplers[7],uv,0);
    case 8:return mtoonTextures[8].SampleLevel(mtoonSamplers[8],uv,0);
    case 9:return mtoonTextures[9].SampleLevel(mtoonSamplers[9],uv,0);
    case 10:return mtoonTextures[10].SampleLevel(mtoonSamplers[10],uv,0);
    }
    return 1;
}
float4 evrm_mtoon_sample(uint slot,float2 rawUv,float seconds,float animationMask){return evrm_mtoon_texel(slot,evrm_mtoon_uv(slot,rawUv,seconds,animationMask));}
float4 evrm_mtoon_sample_static(uint slot,float2 rawUv){return evrm_mtoon_texel(slot,evrm_mtoon_transform(slot,rawUv));}
float evrm_mtoon_mask(uint slot,float4 texel){
    if(mtoonMaterial.modes.y==0u)return texel.r;
    return slot==9u?texel.b:slot==8u?texel.g:texel.r;
}
float3 evrm_mtoon_unit(float3 v,float3 fallback){
    const float maximum=max(abs(v.x),max(abs(v.y),abs(v.z)));
    return all(isfinite(v))&&maximum>1e-20f?normalize(v/maximum):fallback;
}
float3 evrm_mtoon_normal(float3 normal,float4 tangent,float3 encoded,float scale){
    const float3 n=evrm_mtoon_unit(normal,float3(0,0,1));if(abs(tangent.w)!=1.f)return n;
    const float3 t=evrm_mtoon_unit(tangent.xyz-n*dot(n,tangent.xyz),float3(0,0,0));if(all(t==0))return n;
    float3 mapped=encoded*2.f-1.f;mapped.xy*=scale;mapped=evrm_mtoon_unit(mapped,float3(0,0,1));
    return evrm_mtoon_unit(t*mapped.x+cross(n,t)*tangent.w*mapped.y+n*mapped.z,n);
}
#endif
