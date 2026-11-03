#ifndef EVRM_NATIVE_SURFACE_INCLUDED
#define EVRM_NATIVE_SURFACE_INCLUDED
#ifndef EVRM_SURFACE_DESCRIPTOR_SET
#define EVRM_SURFACE_DESCRIPTOR_SET 0
#endif
// Matches the separate112-byte NativeSurfaceMaterial; existing material ABI is unchanged.
struct EvrmSurfaceMaterial {
    float4 normal,matcap,rim,params,cap_uv0,cap_uv1;
    uint4 flags;
};
[[vk::binding(12,EVRM_SURFACE_DESCRIPTOR_SET)]] ConstantBuffer<EvrmSurfaceMaterial> surfaceMaterial;
[[vk::binding(13,EVRM_SURFACE_DESCRIPTOR_SET)]] Texture2D<float4> normalTexture;
[[vk::binding(14,EVRM_SURFACE_DESCRIPTOR_SET)]] SamplerState normalSampler;
[[vk::binding(15,EVRM_SURFACE_DESCRIPTOR_SET)]] Texture2D<float4> matcapTexture;
[[vk::binding(16,EVRM_SURFACE_DESCRIPTOR_SET)]] SamplerState matcapSampler;
[[vk::binding(17,EVRM_SURFACE_DESCRIPTOR_SET)]] Texture2D<float4> rimTexture;
[[vk::binding(18,EVRM_SURFACE_DESCRIPTOR_SET)]] SamplerState rimSampler;
struct EvrmSurfaceInput {
    float3 normal;
    float4 tangent;
    float2 normalUv,rimUv;
    float3 view,cameraUp;
};
struct EvrmSurfaceResult {float3 normal,lit,unlit;};
float3 evrm_surface_unit(float3 v,float3 fallback) {
    const float scale=max(abs(v.x),max(abs(v.y),abs(v.z)));
    if(!all(isfinite(v))||!isfinite(scale)||scale<=1e-20f)return fallback;
    return normalize(v/scale);
}
float3 evrm_surface_normal(float3 n,float4 tangent,float3 encoded,float scale) {
    n=evrm_surface_unit(n,float3(0,0,1));
    if(abs(tangent.w)!=1.f)return n;
    const float3 t=evrm_surface_unit(tangent.xyz-n*dot(n,tangent.xyz),float3(0,0,0));
    if(all(t==0))return n;
    float3 mapped=encoded*2.f-1.f;mapped.xy*=scale;
    mapped=evrm_surface_unit(mapped,float3(0,0,1));
    const float3 b=cross(n,t)*tangent.w;
    return evrm_surface_unit(t*mapped.x+b*mapped.y+n*mapped.z,n);
}
float3 evrm_surface_orthogonal_up(float3 view,float3 candidate) {
    float3 up=candidate-view*dot(view,candidate);
    if(max(abs(up.x),max(abs(up.y),abs(up.z)))<=1e-6f){
        const float3 axis=abs(view.y)<.9f?float3(0,1,0):float3(0,0,1);
        up=axis-view*dot(view,axis);
    }
    return evrm_surface_unit(up,float3(0,1,0));
}
float2 evrm_surface_matcap_uv(uint kind,float3 normal,float3 view,float3 cameraUp) {
    view=evrm_surface_unit(view,float3(0,0,1));
    float3 right,up;float inset;
    if(kind==1u){
        up=evrm_surface_orthogonal_up(view,cameraUp);
        right=evrm_surface_unit(cross(view,up),float3(-1,0,0));inset=.5f;
    }else{
        right=evrm_surface_unit(float3(view.z,0,-view.x),float3(1,0,0));
        up=evrm_surface_unit(cross(view,right),float3(0,1,0));inset=.495f;
    }
    const float2 uv=float2(dot(right,normal),dot(up,normal))*inset+.5f;
    const float2 scaled=uv*surfaceMaterial.cap_uv0.xy;
    return surfaceMaterial.cap_uv0.zw+float2(
        surfaceMaterial.cap_uv1.x*scaled.x-surfaceMaterial.cap_uv1.y*scaled.y,
        surfaceMaterial.cap_uv1.y*scaled.x+surfaceMaterial.cap_uv1.x*scaled.y);
}
EvrmSurfaceResult evrm_surface_terms(float3 normal,float3 view,float3 cap,float3 mask) {
    EvrmSurfaceResult value;value.normal=normal;value.lit=value.unlit=0;
    const uint flags=surfaceMaterial.flags.x;
    float3 rim=0;
    if((flags&4u)!=0){
        const float edge=saturate(1.f-dot(normal,view)+surfaceMaterial.params.y);
        rim=pow(edge,max(surfaceMaterial.params.x,1e-5f))*surfaceMaterial.rim.rgb;
    }
    cap=(flags&2u)!=0?cap*surfaceMaterial.matcap.rgb:float3(0,0,0);
    if(surfaceMaterial.flags.y==1u){
        rim*=mask;value.lit=rim*surfaceMaterial.params.z;
        value.unlit=cap+rim*(1.f-surfaceMaterial.params.z);
    }else{
        const float3 combined=(cap+rim)*mask;
        value.lit=combined*surfaceMaterial.params.z;value.unlit=combined*(1.f-surfaceMaterial.params.z);
    }
    return value;
}
bool evrm_surface_uv(float2 uv){return all(isfinite(uv))&&all(abs(uv)<=16384.f);}
EvrmSurfaceResult evrm_surface_sample(EvrmSurfaceInput input,bool backFace) {
    float3 n=evrm_surface_unit(input.normal,float3(0,0,1));
    const uint flags=surfaceMaterial.flags.x;
    if((flags&1u)!=0&&abs(input.tangent.w)==1.f&&evrm_surface_uv(input.normalUv))
        n=evrm_surface_normal(n,input.tangent,normalTexture.SampleLevel(normalSampler,input.normalUv,0).rgb,surfaceMaterial.normal.x);
    if(backFace)n=-n;
    const float3 view=evrm_surface_unit(input.view,float3(0,0,1));
    float3 cap=0,mask=1;
    if((flags&2u)!=0){
        const float2 uv=evrm_surface_matcap_uv(surfaceMaterial.flags.y,n,view,input.cameraUp);
        if(evrm_surface_uv(uv))cap=matcapTexture.SampleLevel(matcapSampler,uv,0).rgb;
    }
    if((flags&8u)!=0)mask=evrm_surface_uv(input.rimUv)?rimTexture.SampleLevel(rimSampler,input.rimUv,0).rgb:float3(0,0,0);
    return evrm_surface_terms(n,view,cap,mask);
}
#endif
