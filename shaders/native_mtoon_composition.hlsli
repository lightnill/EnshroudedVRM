#ifndef EVRM_NATIVE_MTOON_COMPOSITION
#define EVRM_NATIVE_MTOON_COMPOSITION
#include "native_mtoon_lighting.hlsli"
#include "native_mtoon_cluster.hlsli"
// How far the shared light budget may rise above one unit; see
// native_mtoon_diffuse_scale. Zero makes one unit a hard limit.
#ifndef EVRM_NATIVE_MTOON_DIFFUSE_HEADROOM
#define EVRM_NATIVE_MTOON_DIFFUSE_HEADROOM (1.f/3.f)
#endif
// Pixel-local transaction. No destination write is permitted until finish
// succeeds. The surrounding renderer still supplies native scene admission.
struct NativeMToonRadiance {
    EvrmMToonLighting main_light;
    EvrmMToonSamples samples;
    float3 color,source_color;
    float alpha,exposure;
    uint light_count;
    bool ready,native_lighting;
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
    // `base` and `light` describe the main result already in `color`: its base
    // colour and the untextured light behind it. The additional lights of a
    // native, non-outline pixel are held apart until all of them are known:
    // their untextured light, the base colour it produced, and their rim and
    // MatCap extras.
    float3 base,light,point_light,point_diffuse,point_extras;
    bool points_open;
#endif
};
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
// The sun, the ambient light and every additional light are each limited to
// one exposed unit on their own, and their results used to be added. A surface
// under the sun and a fire, or under several lamps, was then written at two or
// more times its authored colour, where texture detail is lost.
//
// Give the main light and all additional lights one shared budget instead. P is
// the brightest channel of the light that actually reaches the surface, summed
// over the sources, in exposed units and before any texture. Up to one unit
// nothing changes. Above it the sum is brought down by one common factor,
// f(P)/P, with
//
//     f(P) = 1 + (P-1) / (1 + (P-1)/H),    H = EVRM_NATIVE_MTOON_DIFFUSE_HEADROOM
//
// f is the identity with slope one at P = 1, keeps rising with a positive slope
// so more light and less shadow still read as brighter, and approaches 1 + H.
// A common factor on untextured light keeps the lights' relative strengths,
// their hue and the texture's ratios. This is a native appearance choice, not a
// rule of MToon; H is the knob.
//
// What enters P:
// - Visible light only. A fully shadowed sun adds nothing, so it cannot dim the
//   ambient light or a lamp.
// - The main light counts for at most one unit per channel. Its own bound
//   already shows it at no more than that, and so a pixel without additional
//   lights has P <= 1 and is never changed.
float native_mtoon_diffuse_scale(float3 mainLight,float3 pointLight,float exposure) {
    const float3 total=min(mainLight,rcp(exposure))+pointLight;
    const float peak=max(total.x,max(total.y,total.z))*exposure;
    if(!(peak>1.f))return 1.f;
    const float over=peak-1.f;
    const float gain=EVRM_NATIVE_MTOON_DIFFUSE_HEADROOM>0.f?over/(1.f+over/EVRM_NATIVE_MTOON_DIFFUSE_HEADROOM):0.f;
    return (1.f+gain)/peak;
}
// Main result plus the additional lights. Only base colour is scaled: the main
// light's rim, MatCap and emission (mainColor - mainBase) and the additional
// lights' rim and MatCap (pointExtras) pass through whole. Where the budget is
// not exceeded this is the plain sum.
float3 native_mtoon_join_lights(float3 mainColor,float3 mainBase,float3 mainLight,
                                float3 pointLight,float3 pointDiffuse,float3 pointExtras,float exposure) {
    const float scale=native_mtoon_diffuse_scale(mainLight,pointLight,exposure);
    if(scale<1.f)return (mainBase+pointDiffuse)*scale+(mainColor-mainBase)+pointExtras;
    return mainColor+(pointDiffuse+pointExtras);
}
// Folds the held lights into `color`. The transparent prepare stage does not
// call this: its main result is not final, so it hands the three held terms to
// the finish stage, which calls native_mtoon_join_lights itself.
void native_mtoon_resolve_points(inout NativeMToonRadiance state) {
    if(!state.points_open)return;
    state.color=native_mtoon_join_lights(state.color,state.base,state.light,state.point_light,state.point_diffuse,state.point_extras,state.exposure);
    state.point_light=0;state.point_diffuse=0;state.point_extras=0;state.points_open=false;
    if(any(!isfinite(state.color)))state.ready=false;
}
#else
void native_mtoon_resolve_points(inout NativeMToonRadiance state) {}
#endif
bool native_mtoon_begin_radiance(NativeMToonLightUnits units,EvrmMToonLighting mainLight,
                               EvrmMToonSamples samples,out NativeMToonRadiance state,bool nativeFilteredShadow=false,float nativeSaturatedAmbientShadeTint=0.f) {
    state=(NativeMToonRadiance)0;
    if(!isfinite(units.exposure) || units.exposure<=0.0f || !isfinite(units.emissive_gain) || units.emissive_gain<0.0f ||
       mainLight.additional || any(!isfinite(mainLight.normal)) || dot(mainLight.normal,mainLight.normal)<=1e-20f ||
       any(!isfinite(mainLight.view)) || dot(mainLight.view,mainLight.view)<=1e-20f)return false;
    samples.emission*=units.emissive_gain;
    if(!native_mtoon_legacy_ceiling_ready(samples,units.exposure))return false;
    const EvrmMToonReferenceResult mainResult=evrm_mtoon_reference_radiance(mainLight,samples,units.exposure,.5f,nativeFilteredShadow,nativeSaturatedAmbientShadeTint);
    if(mainResult.discard_fragment || any(!isfinite(mainResult.color)))return false;
    state.main_light=mainLight;state.samples=samples;state.color=mainResult.color.rgb;
    state.source_color=mainResult.native_source_color;state.native_lighting=nativeFilteredShadow;
    state.alpha=mainResult.color.a;state.exposure=units.exposure;state.ready=true;
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
    state.base=mainResult.native_base;state.light=mainResult.native_light;
#endif
    return true;
}
bool native_mtoon_add_radiance(inout NativeMToonRadiance state,NativeMToonPointResponse response,float unshadowedAttenuation=-1.f) {
    if(!state.ready)return false;
    if(state.light_count>=128u || any(!isfinite(response.color)) || any(response.color<0.0f) ||
       !isfinite(response.attenuation) || response.attenuation<0.0f || any(!isfinite(response.toward)) ||
       (response.attenuation>0.0f && dot(response.toward,response.toward)<=1e-20f)) {
        state.ready=false;return false;
    }
    ++state.light_count;
    if(response.attenuation==0.0f)return true;
    EvrmMToonLighting light=(EvrmMToonLighting)0;
    light.normal=state.main_light.normal;light.view=state.main_light.view;light.outline=state.main_light.outline;
    light.normal_dx=state.main_light.normal_dx;light.normal_dy=state.main_light.normal_dy;
    light.additional=true;light.direction=response.toward;light.color=response.color;light.attenuation=response.attenuation;
    if(!isfinite(unshadowedAttenuation)){state.ready=false;return false;}
    const EvrmMToonReferenceResult addition=evrm_mtoon_reference_radiance(light,state.samples,state.exposure,0.f,state.native_lighting,0.f,unshadowedAttenuation);
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
    // Outlines and non-native callers keep the plain sum below. The original
    // unlimited source sum and its checks are the same on both paths.
    if(state.native_lighting&&!state.main_light.outline) {
        const float3 pointLight=state.point_light+addition.native_light,pointDiffuse=state.point_diffuse+addition.native_base,
            pointExtras=state.point_extras+(addition.color.rgb-addition.native_base);
        state.source_color+=addition.native_source_color;
        if(addition.discard_fragment || any(!isfinite(pointLight)) || any(!isfinite(pointDiffuse)) || any(!isfinite(pointExtras)) ||
           any(!isfinite(state.source_color))) {state.ready=false;return false;}
        state.point_light=pointLight;state.point_diffuse=pointDiffuse;state.point_extras=pointExtras;state.points_open=true;
        return true;
    }
#endif
    const float3 sum=state.color+addition.color.rgb;
    state.source_color+=addition.native_source_color;
    if(addition.discard_fragment || any(!isfinite(sum)) || any(!isfinite(state.source_color))) {state.ready=false;return false;}
    state.color=sum;return true;
}
bool native_mtoon_finish_radiance(NativeMToonRadiance state,out float4 hdr) {
    hdr=0.0f;
    // A caller that reads `color` itself resolves first; this covers the rest.
    native_mtoon_resolve_points(state);
    if(!state.ready)return false;
    const float3 radiance=state.main_light.outline?evrm_mtoon_outline_color(state.color):state.color;
    const float3 source=state.main_light.outline?evrm_mtoon_outline_color(state.source_color):state.source_color;
    if(any(!isfinite(source*state.exposure))||any(abs(source*state.exposure)>65504.f))return false;
    const float4 value=float4(radiance*state.exposure,state.alpha);
    if(any(!isfinite(value)) || any(abs(value)>65504.0f))return false;
    hdr=value;return true;
}
// Visibility must come from the same admitted native shadow atlas (or the
// native disabled/unshadowed case). A failed late record poisons the entire
// pixel transaction; callers cannot accidentally finish earlier contributions.
bool native_mtoon_add_native_point(inout NativeMToonRadiance state,ByteAddressBuffer lights,
    uint index,float3 position,float gain,float visibility) {
    if(!state.ready)return false;
    NativeMToonPointLight emitter;NativeMToonPointResponse response;
    if(!native_mtoon_cluster_light(lights,index,emitter)||!native_mtoon_point_response(emitter,position,gain,visibility,response)){
        state.ready=false;return false;
    }
    return native_mtoon_add_radiance(state,response);
}
#endif
