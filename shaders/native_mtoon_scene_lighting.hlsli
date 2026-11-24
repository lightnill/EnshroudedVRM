#ifndef EVRM_NATIVE_MTOON_SCENE_LIGHTING
#define EVRM_NATIVE_MTOON_SCENE_LIGHTING
#include "native_mtoon_probe_frame.hlsli"
#include "native_mtoon_composition.hlsli"
#ifndef EVRM_NATIVE_MTOON_SOFTEN_AMBIENT
#define EVRM_NATIVE_MTOON_SOFTEN_AMBIENT 0
#endif

// Exact default DeferredLighting set2:24 scalar layout, without native code or
// bindings. The enclosing renderer must admit the same-frame sources, queue,
// lifetime, original dispatch and destination before using this numeric seam.
#include "native_mtoon_scene_parameters.hlsli"
#include "native_mtoon_scene_light_inputs.hlsli"
struct NativeMToonSceneRadiance {
    NativeMToonRadiance toon;
    float3 diffuse_response,world_position;
    float point_gain;
    uint flags;
};
bool native_mtoon_scene_unit_interval(float x) {return isfinite(x)&&x>=0.0f&&x<=1.0f;}
bool native_mtoon_scene_decline(inout NativeMToonSceneRadiance state) {state.toon.ready=false;return false;}

bool native_mtoon_begin_scene_radiance(NativeMToonSceneLightParameters scene,ByteAddressBuffer probes,
    float3 position,float3 normal,float3 view,NativeMToonScenePixelLight pixel,EvrmMToonSamples samples,
    bool outline,out NativeMToonSceneRadiance state,float3 normalDx=0,float3 normalDy=0) {
    state=(NativeMToonSceneRadiance)0;
    const float4 gains=asfloat(scene.words[0]); // sun, points, GI, emission
    const float2 exposure=asfloat(scene.words[1].xy);
    const uint flags=scene.words[118].w;
    if(any(!isfinite(gains))||any(gains<0.0f)||!isfinite(exposure.x)||exposure.x<=0.0f||
       (flags&~127u)!=0u||any(!isfinite(position)))return false;
    NativeMToonLightUnits units=(NativeMToonLightUnits)0;
    units.sun=asfloat(scene.words[111].xyz);units.sun_toward=asfloat(scene.words[110].xyz);
    units.sun_gain=gains.x;units.exposure=exposure.x;units.emissive_gain=gains.w;
    units.visibility=1;units.transmittance=1;units.probe_correction=1;
    const bool sunActive=gains.x>0.0f&&any(units.sun!=0.0f);
    const bool probeGi=gains.z>0.0f&&(flags&8u)!=0u;
    const bool environment=(flags&8u)==0u&&scene.words[118].z!=0u;
    float giModulation=1;
    if((flags&64u)!=0u&&(sunActive||probeGi)) {
        if(!pixel.native_modulation_available)return false;
        if(sunActive) {
            if(any(!isfinite(pixel.sunlight_modulation))||any(pixel.sunlight_modulation<0.0f))return false;
            units.sun*=pixel.sunlight_modulation;
        }
        if(probeGi) {
            if(!isfinite(pixel.gi_modulation)||pixel.gi_modulation<0.0f)return false;
            giModulation=pixel.gi_modulation;
        }
    }
    if(sunActive) {
        uint required=1u<<4; // native full-resolution sun mask
        if(scene.words[112].x!=0u)required|=1u<<1;
        if((flags&1u)==0u)required|=1u<<2;
        if((pixel.available_images&required)!=required||!native_mtoon_scene_unit_interval(pixel.sun_visibility))return false;
        units.visibility=pixel.sun_visibility;
        if(scene.words[112].x!=0u)units.transmittance=pixel.transmittance;
        if((flags&1u)==0u) {
            if(!native_mtoon_scene_unit_interval(pixel.fog_shadow))return false;
            units.visibility*=pixel.fog_shadow;
        }
    }
    float occlusion=1;
    if(probeGi||environment) {
        if((flags&4u)!=0u) {
            if(!native_mtoon_scene_unit_interval(pixel.material_occlusion))return false;
            occlusion*=pixel.material_occlusion;
        }
        if((flags&2u)!=0u) {
            if(!(pixel.available_images&(1u<<5))||!native_mtoon_scene_unit_interval(pixel.ssao))return false;
            occlusion*=pixel.ssao;
        }
    }
    float4 red=0,green=0,blue=0;
    if(probeGi) {
        uint bytes;probes.GetDimensions(bytes);
        if(bytes<2097440u||!isfinite(exposure.y)||exposure.y<=0.0f)return false;
        const float correction=asfloat(probes.Load(260u));
        if(!isfinite(correction)||correction<=0.0f||
           abs(correction-exposure.y)>max(correction,exposure.y)*.0001f||
           !native_mtoon_sample_probe_frame(probes,position,exposure.x,red,green,blue))return false;
        units.gi_gain=gains.z*giModulation*occlusion;units.probe_correction=correction;
    }
    EvrmMToonLighting mainLight;
    if(!native_mtoon_main_light(units,normal,view,red,green,blue,mainLight))return false;
    // Preserve incident sunlight and its native filtered visibility separately.
    // The explicit native reference adapter applies visibility once, after
    // legacy colour attenuation, without re-thresholding the direct ramp.
    if(environment) {
        const float intensity=asfloat(scene.words[118].y);
        if(!(pixel.available_images&(1u<<3))||!isfinite(intensity)||intensity<0.0f||
           any(!isfinite(pixel.environment_normal))||any(pixel.environment_normal<0.0f)||
           any(!isfinite(pixel.environment_up))||any(pixel.environment_up<0.0f)||
           any(!isfinite(pixel.environment_down))||any(pixel.environment_down<0.0f))return false;
        // Native diffuse IBL intensity is independent of the screen-GI gain.
        const float scale=intensity*occlusion*(1.0f/3.14159265358979323846f);
        mainLight.indirect=pixel.environment_normal*scale;
        mainLight.equalized=(pixel.environment_up+pixel.environment_down)*(.5f*scale);
        if(!isfinite(scale)||any(!isfinite(mainLight.indirect))||any(!isfinite(mainLight.equalized)))return false;
    }
    // Keep native exposure/grey metering on the physical incident response.
    // The optional native colour policy changes only MToon's ambient tint.
    const float3 physicalDiffuse=mainLight.indirect+(mainLight.color*units.visibility)*max(dot(mainLight.normal,mainLight.direction),0.0f);
#if EVRM_NATIVE_MTOON_SOFTEN_AMBIENT
    const float3 luminance=float3(.2125f,.7154f,.0721f);
    mainLight.indirect=lerp(mainLight.indirect,dot(mainLight.indirect,luminance).xxx,.5f);
    mainLight.equalized=lerp(mainLight.equalized,dot(mainLight.equalized,luminance).xxx,.5f);
#endif
    mainLight.outline=outline;
    mainLight.normal_dx=normalDx;mainLight.normal_dy=normalDy;
    if(!native_mtoon_begin_radiance(units,mainLight,samples,state.toon,true,.5f))return false;
    // Keep physical grey response independent of toon ramps, base colour,
    // emission and outline tint. Native brightness uses grey albedo 0.18.
    state.diffuse_response=physicalDiffuse;
    state.world_position=position;state.point_gain=gains.y;state.flags=flags;
    if(any(!isfinite(state.diffuse_response)))return native_mtoon_scene_decline(state);
    return true;
}
// The concrete point-source path can retain its already decoded response,
// avoiding a second52-byte light load after exact atlas visibility is known.
bool native_mtoon_add_scene_point_response(inout NativeMToonSceneRadiance state,NativeMToonPointResponse response,float unshadowedAttenuation=-1.f) {
    if(!state.toon.ready)return false;
    if(!(state.flags&16u)||state.toon.light_count>=128u||!native_mtoon_add_radiance(state.toon,response,unshadowedAttenuation))return native_mtoon_scene_decline(state);
    state.diffuse_response+=response.color*(response.attenuation*max(dot(state.toon.main_light.normal,response.toward),0.0f));
    if(any(!isfinite(state.diffuse_response)))return native_mtoon_scene_decline(state);
    return true;
}
bool native_mtoon_add_scene_native_point(inout NativeMToonSceneRadiance state,ByteAddressBuffer lights,
                                       uint index,float visibility) {
    if(!state.toon.ready)return false;
    if(!(state.flags&16u)||state.toon.light_count>=128u)return native_mtoon_scene_decline(state);
    NativeMToonPointLight light;NativeMToonPointResponse response;
    if(!native_mtoon_cluster_light(lights,index,light)||
       !native_mtoon_point_response(light,state.world_position,state.point_gain,visibility,response))return native_mtoon_scene_decline(state);
    return native_mtoon_add_scene_point_response(state,response);
}
bool native_mtoon_finish_scene_radiance(NativeMToonSceneRadiance state,out float4 hdr,out float brightness) {
    hdr=0;brightness=0;
    float4 value;
    if(!native_mtoon_finish_radiance(state.toon,value))return false;
    const float grey=.18f*dot(state.diffuse_response,float3(.2125f,.7154f,.0721f))*state.toon.exposure;
    if(!isfinite(grey)||grey<0.0f||grey>65504.0f)return false;
    hdr=value;brightness=grey;return true;
}
#endif
