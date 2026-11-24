#ifndef EVRM_MTOON_OUTLINE_INCLUDED
#define EVRM_MTOON_OUTLINE_INCLUDED
#include "mtoon_reference.hlsl"
// Authored expanded-backface geometry. Inputs are the caller's already deformed
// positions/normals for that exact frame, never a second rest-pose mesh.
// The native bridge must admit projection/depth/history before calling this.
struct EvrmMToonOutlineFrame {float4 clip_rows[4];float2 extent;};
struct EvrmMToonOutlinePoint {
    float3 position,normal,view_normal,legacy_expansion;
    float2 width_uv,mask_uv;
};
struct EvrmMToonOutlineVertex {float4 clip;float3 world;float width;bool expanded;};
float4 evrm_mtoon_outline_project(EvrmMToonOutlineFrame frame,float3 geometry){
    const float4 p=float4(geometry,1);return float4(dot(frame.clip_rows[0],p),dot(frame.clip_rows[1],p),dot(frame.clip_rows[2],p),dot(frame.clip_rows[3],p));
}
EvrmMToonOutlineVertex evrm_mtoon_outline_vertex(EvrmMToonOutlineFrame frame,EvrmMToonOutlinePoint geometry,float seconds){
    EvrmMToonOutlineVertex value;value.world=geometry.position;value.clip=evrm_mtoon_outline_project(frame,geometry.position);value.width=0;value.expanded=false;
    const uint mode=mtoonMaterial.flags.x;const bool legacy=mtoonMaterial.modes.y==0u;
    if(mode==0u||mtoonMaterial.outline_settings.x<=0||!all(isfinite(value.clip))||value.clip.w<=0)return value;
    const float animation=evrm_mtoon_mask(9u,evrm_mtoon_sample_static(9u,geometry.mask_uv));
    const float width=mtoonMaterial.outline_settings.x*evrm_mtoon_mask(8u,evrm_mtoon_sample(8u,geometry.width_uv,seconds,animation));
    if(!isfinite(width)||width<=0)return value;
    if(mode==1u){
        const float3 direction=legacy?geometry.legacy_expansion:evrm_mtoon_unit(geometry.normal,float3(0,0,0));
        if(!all(isfinite(direction)))return value;
        value.world+=direction*width*(legacy?.01f:1.f);value.clip=evrm_mtoon_outline_project(frame,value.world);
    }else if(mode==2u){
        if(!all(isfinite(frame.extent))||any(frame.extent<=0))return value;
        const float3 normal=evrm_mtoon_unit(geometry.normal,float3(0,0,0)),viewNormal=evrm_mtoon_unit(geometry.view_normal,float3(0,0,0));
        float2 projected=float2(dot(frame.clip_rows[0].xyz,normal),dot(frame.clip_rows[1].xyz,normal));
        const float size=max(abs(projected.x),abs(projected.y));if(!isfinite(size)||size<=1e-20f)return value;
        projected=normalize(projected/size);projected.x*=frame.extent.y/frame.extent.x;
        const float distance=legacy?min(value.clip.w,mtoonMaterial.outline_settings.z):value.clip.w;
        const float facing=legacy?saturate(1.f-abs(viewNormal.z)):saturate(1.f-viewNormal.z*viewNormal.z);
        value.clip.xy+=projected*width*distance*facing*(legacy?.01f:2.f);
    }else return value;
    if(!all(isfinite(value.clip))||!all(isfinite(value.world))){value.world=geometry.position;value.clip=evrm_mtoon_outline_project(frame,geometry.position);return value;}
    value.width=width;value.expanded=true;return value;
}
#endif
