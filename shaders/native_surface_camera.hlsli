#ifndef EVRM_SURFACE_CAMERA_INCLUDED
#define EVRM_SURFACE_CAMERA_INCLUDED
float3 evrm_camera_unit(float3 v,float3 fallback) {
    const float scale=max(abs(v.x),max(abs(v.y),abs(v.z)));
    if(!all(isfinite(v))||!isfinite(scale)||scale<=1e-20f)return fallback;
    return normalize(v/scale);
}
float3 evrm_surface_camera_relative(uint3 root,uint3 origin,float3 cameraWorld,float offsetX) {
    // Subtract packed positions before converting to float. Float camera and
    // packed coordinates share the native32768-metre wrap convention.
    float3 delta=float3(asint(root-origin))*(1.f/131072.f)-
        (cameraWorld-float3(origin)*(1.f/131072.f));
    delta-=floor((delta+16384.f)/32768.f)*32768.f;
    return delta+float3(offsetX,0,0);
}
float3 evrm_surface_relative_position(float3x3 rotation,float3 position,float scale,float3 rootRelative) {
    return mul(rotation,position)*scale+rootRelative;
}
float3 evrm_surface_camera_up(float3x3 rotation,float4 row1,float4 row3) {
    // R recovers world covectors from actor-local projection direction rows.
    // The positive uniform actor scale cancels during normalization.
    const float3 forward=evrm_camera_unit(mul(rotation,row3.xyz),float3(0,0,1));
    const float3 projected=mul(rotation,row1.xyz);
    return evrm_camera_unit(projected-forward*dot(projected,forward),float3(0,1,0));
}
#endif
