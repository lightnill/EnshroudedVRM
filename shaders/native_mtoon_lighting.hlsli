#ifndef EVRM_NATIVE_MTOON_LIGHTING
#define EVRM_NATIVE_MTOON_LIGHTING
#include "mtoon_reference.hlsl"
// Numeric adapter only. These values must come from an independently admitted
// scene and the same frame's owned probe snapshot. This helper does not infer
// readiness, initialization, exposure identity, queue ordering or ownership.
struct NativeMToonLightUnits {
    float3 sun,sun_toward,transmittance;
    float sun_gain,visibility,gi_gain,probe_correction,exposure,emissive_gain;
};
float3 native_mtoon_diffuse_sh(float4 red,float4 green,float4 blue,float3 normal) {
    // L0/L1 radiance SH -> Lambert irradiance / pi. Unity's MToon SH seam
    // consumes this normalized diffuse response, rather than raw radiance SH.
    const float4 basis=float4(.28209479177387814f,normal*(.4886025119029199f*(2.f/3.f)));
    return max(float3(dot(red,basis),dot(green,basis),dot(blue,basis)),0);
}
bool native_mtoon_legacy_ceiling_ready(EvrmMToonSamples samples,float exposure){
    if(!isfinite(exposure)||exposure<=0.f)return false;
    if(mtoonMaterial.modes.y!=0u)return true;
    const float3 lit=mtoonMaterial.base.rgb*samples.base.rgb;
    const float3 ceiling=lit/exposure;
    // A subnormal bound can flush to zero before the final exposure. Decline
    // unrepresentable conversions rather than committing a false black pixel.
    return all(isfinite(lit))&&all(lit>=0.f)&&all(isfinite(ceiling))&&
        all(or(lit==0.f,ceiling>=1.1754943508222875e-38f));
}
bool native_mtoon_main_light(NativeMToonLightUnits u,float3 normal,float3 view,
    float4 red,float4 green,float4 blue,out EvrmMToonLighting light) {
    light=(EvrmMToonLighting)0;
    if(!all(isfinite(u.sun)) || any(u.sun<0) || !all(isfinite(u.sun_toward)) ||
        !all(isfinite(u.transmittance)) || any(u.transmittance<0) || any(u.transmittance>1) ||
        !all(isfinite(float4(u.sun_gain,u.visibility,u.gi_gain,u.probe_correction))) ||
        !all(isfinite(float2(u.exposure,u.emissive_gain))) || u.sun_gain<0 || u.visibility<0 || u.visibility>1 ||
        u.gi_gain<0 || u.probe_correction<=0 || u.exposure<=0 || u.emissive_gain<0 ||
        !all(isfinite(normal)) || !all(isfinite(view)) || !all(isfinite(red)) ||
        !all(isfinite(green)) || !all(isfinite(blue))) return false;
    normal=evrm_mtoon_unit(normal,0);view=evrm_mtoon_unit(view,0);
    if(all(normal==0) || all(view==0)) return false;
    float3 direction=evrm_mtoon_unit(u.sun_toward,0);
    float3 direct=u.sun*u.sun_gain*u.transmittance*(1.f/3.14159265358979323846f);
    if(!all(isfinite(direct)) || (any(direct!=0) && all(direction==0))) return false;
    float gain=u.gi_gain*u.probe_correction;
    float3 indirect=native_mtoon_diffuse_sh(red,green,blue,normal)*gain;
    // Sample and clamp each hemisphere before averaging, as the reference
    // MToon seam does. Discarding the L1 bands first is not equivalent.
    float3 equalized=(native_mtoon_diffuse_sh(red,green,blue,float3(0,1,0))+
        native_mtoon_diffuse_sh(red,green,blue,float3(0,-1,0)))*(.5f*gain);
    if(!isfinite(gain) || !all(isfinite(indirect)) || !all(isfinite(equalized))) return false;
    light.normal=normal;light.view=view;light.direction=direction;light.color=direct;
    light.indirect=indirect;light.equalized=equalized;light.attenuation=u.visibility;
    return true;
}
bool native_mtoon_main_radiance(NativeMToonLightUnits units,EvrmMToonLighting light,
    EvrmMToonSamples samples,out float4 hdr) {
    hdr=0;
    if(!isfinite(units.exposure) || units.exposure<=0 || !isfinite(units.emissive_gain) || units.emissive_gain<0) return false;
    samples.emission*=units.emissive_gain;
    if(!native_mtoon_legacy_ceiling_ready(samples,units.exposure))return false;
    EvrmMToonReferenceResult shaded=evrm_mtoon_reference(light,samples,units.exposure,.5f);
    float4 value=float4(shaded.color.rgb*units.exposure,shaded.color.a);
    if(shaded.discard_fragment || !all(isfinite(value)) || any(abs(value)>65504.f)) return false;
    hdr=value;return true;
}
#endif
