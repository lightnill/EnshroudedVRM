#ifndef EVRM_MTOON_REFERENCE_INCLUDED
#define EVRM_MTOON_REFERENCE_INCLUDED
#include "mtoon_material.hlsli"
#include "mtoon_pixel_ramp.hlsli"
#include "mtoon_game_look.hlsli"
#ifndef EVRM_NATIVE_MTOON_LIGHT_LIMIT
#define EVRM_NATIVE_MTOON_LIGHT_LIMIT 0
#endif
// Native composition policy for additional lights, off unless the including
// shader asks for it: the main light and all additional lights share one
// budget of visible light, see native_mtoon_composition.hlsli. With it off
// this file compiles exactly as before.
#ifndef EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
#define EVRM_NATIVE_MTOON_DIFFUSE_BUDGET 0
#endif
// Controlled linear-radiance shading seam. Native integration must supply
// independently admitted lights/GI/shadow inputs and fog/exposure ordering.
// Adapted from pinned MIT UniVRM; attribution in mtoon_material.hlsli/notices.
struct EvrmMToonLighting {
    float3 normal,view,direction,color,indirect,equalized,normal_dx,normal_dy;
    float attenuation;
    bool additional,outline;
};
struct EvrmMToonSamples {
    float4 base;
    float3 shade,emission,matcap,rim;
    float receive_shadow,grade,shift;
};
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
// The two native_* terms after the source colour describe `color`; they never
// change it. native_base: the colour before rim, MatCap and emission, that is
// texture and shade colours times light. native_light: that light alone, as
// it reaches the surface: after each source's own limit and after visibility,
// without any texture. For the main light it is sun plus ambient.
struct EvrmMToonReferenceResult {float4 color;float3 native_source_color;float shade;bool depth_write,discard_fragment;float3 native_base,native_light;};
#else
struct EvrmMToonReferenceResult {float4 color;float3 native_source_color;float shade;bool depth_write,discard_fragment;};
#endif
float3 evrm_mtoon_light_color(float3 color,float attenuation){
    return lerp(color,max(.0009765625f,max(color.x,max(color.y,color.z))),attenuation);
}
float evrm_mtoon_v1_ramp(float input,float toony){
    const float halfWidth=1.f-toony;
    // At exactly zero width the source equation divides by zero. Define the
    // limiting hard step explicitly so an authored toony=1 never produces NaN.
    return halfWidth>0?saturate((input+halfWidth)/(2.f*halfWidth)):(input>=0?1.f:0.f);
}
float2 evrm_mtoon_matcap_uv(float3 normal,float3 view,float3 cameraUp){
    view=evrm_mtoon_unit(view,float3(0,0,1));
    float3 up=mtoonMaterial.modes.y==0u?cameraUp:float3(0,1,0);
    up-=view*dot(up,view);
    if(dot(up,up)<1e-10f){up=abs(view.z)<.9f?float3(0,0,1):float3(1,0,0);up-=view*dot(up,view);}
    up=evrm_mtoon_unit(up,float3(0,1,0));const float3 right=evrm_mtoon_unit(cross(view,up),float3(-1,0,0));
    return float2(dot(right,normal),dot(up,normal))*.5f+.5f;
}
EvrmMToonSamples evrm_mtoon_samples(float2 rawUv[11],float seconds,float3 normal,float3 view,float3 cameraUp){
    EvrmMToonSamples s;
    const float mask=evrm_mtoon_mask(9u,evrm_mtoon_sample_static(9u,rawUv[9]));
    s.base=evrm_mtoon_sample(0u,rawUv[0],seconds,mask);
    s.shade=evrm_mtoon_sample(1u,rawUv[1],seconds,mask).rgb;
    s.emission=evrm_mtoon_sample(5u,rawUv[5],seconds,mask).rgb;
    s.rim=evrm_mtoon_sample(6u,rawUv[6],seconds,mask).rgb;
    s.matcap=evrm_mtoon_sample_static(7u,evrm_mtoon_matcap_uv(normal,view,cameraUp)).rgb;
    s.receive_shadow=evrm_mtoon_sample(3u,rawUv[3],seconds,mask).r;
    s.grade=evrm_mtoon_sample(4u,rawUv[4],seconds,mask).r;
    s.shift=evrm_mtoon_sample(10u,rawUv[10],seconds,mask).r;
    return s;
}
float3 evrm_mtoon_native_ceiling_chroma(float3 bounded,float3 before,float3 ceiling,float3 shade,float amount){
    if(!isfinite(amount)||amount<=0.f||amount>1.f||any(!isfinite(before))||any(!isfinite(ceiling))||
       any(ceiling<=0.f)||any(!isfinite(shade))||any(shade<0.f))return bounded;
    const float3 ratio=before/ceiling;
    if(any(!isfinite(ratio)))return bounded;
    // Continuous entry: no adjustment at the first ceiling contact, reaching
    // the configured mix only when every base channel has twice that energy.
    const float blend=amount*saturate(min(ratio.x,min(ratio.y,ratio.z))-1.f);
    if(blend<=0.f)return bounded;
    const float3 weights=float3(.2125f,.7154f,.0721f);
    const float targetY=dot(ceiling,weights),shadeY=dot(shade,weights);
    if(!isfinite(targetY)||!isfinite(shadeY)||targetY<=0.f||shadeY<=0.f)return bounded;
    float3 target=shade*(targetY/shadeY);
    if(any(!isfinite(target))||any(target<0.f))return bounded;
    const float3 limits=float3(target.x>0.f?ceiling.x/target.x:1.f,
        target.y>0.f?ceiling.y/target.y:1.f,target.z>0.f?ceiling.z/target.z:1.f);
    const float scale=min(1.f,min(limits.x,min(limits.y,limits.z)));
    if(any(!isfinite(limits))||!isfinite(scale)||scale<=0.f)return bounded;
    target=min(target*scale,ceiling);
    const float oldChroma=max(bounded.x,max(bounded.y,bounded.z))-min(bounded.x,min(bounded.y,bounded.z));
    const float targetChroma=max(target.x,max(target.y,target.z))-min(target.x,min(target.y,target.z));
    // Select the authored hue continuously. Testing the completed blend with a
    // binary chroma comparison introduces a jump when RGB channel order changes.
    // Crossed hues may slightly reduce intermediate RGB range; all channels stay
    // inside the original ceiling and no extra energy is invented.
    const float eligibility=saturate((targetChroma-oldChroma)/max(targetChroma,1e-20f));
    // Normalizing a nearly black shade must not create a full-strength tint.
    // Fade with authored shade energy relative to the existing diffuse bound.
    const float shadeStrength=saturate(shadeY/targetY);
    const float3 proposed=lerp(bounded,target,blend*eligibility*shadeStrength);
    return all(isfinite(proposed))?proposed:bounded;
}
float3 evrm_mtoon_native_ceiling_fit(float3 bounded,float3 before,float3 ceiling,float3 shade,float amount){
    if(!isfinite(amount)||amount<=0.f||amount>1.f||any(!isfinite(before))||any(before<0.f)||
       any(!isfinite(ceiling))||any(ceiling<=0.f)||any(!isfinite(shade))||any(shade<0.f))return bounded;
    const float3 ratio=before/ceiling;
    if(any(!isfinite(ratio)))return bounded;
    // Preserve the already evaluated main-light/material RGB direction within
    // the same diffuse bounds. Enter continuously after first ceiling contact.
    // A disappearing ceiling channel must not activate a finite hue change.
    const float channelStrength=saturate(1024.f*(min(ceiling.x,min(ceiling.y,ceiling.z))/max(ceiling.x,max(ceiling.y,ceiling.z))));
    const float blend=amount*saturate(max(ratio.x,max(ratio.y,ratio.z))-1.f)*channelStrength;
    if(blend<=0.f)return bounded;
    const float3 limits=float3(before.x>0.f?ceiling.x/before.x:1.f,
        before.y>0.f?ceiling.y/before.y:1.f,before.z>0.f?ceiling.z/before.z:1.f);
    const float scale=min(1.f,min(limits.x,min(limits.y,limits.z)));
    if(any(!isfinite(limits))||!isfinite(scale)||scale<=0.f)return bounded;
    // Uniform fit <= independently clipped RGB, so it adds no diffuse energy.
    const float3 target=min(before*scale,ceiling);
    const float normalization=max(max(bounded.x,max(bounded.y,bounded.z)),max(target.x,max(target.y,target.z)));
    if(!isfinite(normalization)||normalization<=0.f)return bounded;
    const float3 meanWeights=float3(1.f/3.f,1.f/3.f,1.f/3.f);
    const float3 current=bounded/normalization,change=(target-bounded)/normalization;
    const float3 chroma=current-dot(current,meanWeights),delta=change-dot(change,meanWeights);
    // Reuse the established continuous projection rule. A source fit that
    // would weaken existing material chroma gets zero weight; neutral colours
    // and a vanishing change cannot enable a finite tint or a hard selector.
    const float eligibility=saturate(dot(chroma,delta)/max(dot(delta,delta),1e-20f));
    const float3 proposed=lerp(bounded,target,blend*eligibility);
    return isfinite(eligibility)&&all(isfinite(proposed))?proposed:bounded;
}
EvrmMToonReferenceResult evrm_mtoon_reference_radiance(EvrmMToonLighting light,EvrmMToonSamples s,float legacyCeilingExposure=1.f,float nativeAmbientShadeTint=0.f,bool nativeFilteredShadow=false,float nativeSaturatedAmbientShadeTint=0.f,float nativeUnshadowedAttenuation=-1.f){
    const bool legacy=mtoonMaterial.modes.y==0u;
    // Native opaque shading leaves sunlight visibility neutral without any
    // incident sun energy. Match that branch for late transparent shading:
    // a sun-shadow volume alone cannot recolour GI-only eyes or eyebrows.
    // Keep pure reference and additional-light evaluation unchanged.
    if(nativeFilteredShadow&&!light.additional&&all(light.color==0.f))light.attenuation=1.f;
    const float3 n=evrm_mtoon_unit(light.normal,float3(0,0,1)),view=evrm_mtoon_unit(light.view,float3(0,0,1));
    const float3 direction=evrm_mtoon_unit(light.direction,float3(0,0,0));
    const float ndl=dot(n,direction);
    float3 lightColor=all(direction==0)?float3(0,0,0):light.color;
    const float4 lit=mtoonMaterial.base*s.base;
    const float3 shade=mtoonMaterial.shade.rgb*s.shade;
    float rate,normalRate,shadow=1.f;float3 indirect=0,sourceIndirect=0;
    if(legacy){
        const float attenuation=light.additional?1.f:light.attenuation*lerp(1.f,light.attenuation,mtoonMaterial.shading.z*s.receive_shadow);
        const float grade=1.f-mtoonMaterial.shading.w*(1.f-s.grade);
        const float input=((ndl*.5f+.5f)*attenuation*grade)*2.f-1.f;
        const float low=mtoonMaterial.shading.x,high=lerp(1.f,low,mtoonMaterial.shading.y);
        rate=saturate((input-low)/max(.0009765625f,high-low));
        normalRate=saturate((((ndl*.5f+.5f)*grade)*2.f-1.f-low)/max(.0009765625f,high-low));
        lightColor=evrm_mtoon_light_color(lightColor,mtoonMaterial.indirect.x);
        if(light.additional){
            if(mtoonMaterial.modes.z>=2u)lightColor*=ndl>=0?1.f:0.f;
            lightColor*=.5f*(min(0.f,ndl)+1.f)*light.attenuation;
        }else{
            sourceIndirect=lerp(light.equalized,light.indirect,mtoonMaterial.indirect.y);
            indirect=sourceIndirect;
            indirect=evrm_mtoon_light_color(indirect,mtoonMaterial.indirect.x);
        }
    }else{
        const float shift=mtoonMaterial.shading.x+s.shift*mtoonMaterial.rim_settings.w;
        rate=evrm_mtoon_v1_ramp(ndl+shift,mtoonMaterial.shading.y);
        normalRate=rate;
        if(light.additional)shadow=.5f*(min(0.f,ndl)+1.f)*light.attenuation;
        else {rate*=light.attenuation;sourceIndirect=lerp(light.indirect,light.equalized,mtoonMaterial.indirect.z);indirect=sourceIndirect;}
    }
    // Native incident sunlight remains unattenuated until after the legacy
    // colour-floor operation. A filtered mask must still remove all direct
    // light at zero visibility, and a disabled sun is not an epsilon emitter.
    const float3 unoccludedLightColor=lightColor;
    if(nativeFilteredShadow&&!light.additional)
        lightColor=any(light.color>0.f)?lightColor*light.attenuation:float3(0,0,0);
    float3 ambientAlbedo=lit.rgb;
    // Optional native adaptation, disabled by default for the pure MToon
    // reference. Preserve the authored lit luminance while borrowing chroma
    // from the authored shade at the actual main-light shadow/ramp rate.
    // Native physical-grey metering and additional coloured lights stay apart.
    if(nativeAmbientShadeTint>0.f&&!light.additional){
        const float3 weights=float3(.2125f,.7154f,.0721f);
        const float3 authoredAmbient=lerp(shade,lit.rgb,rate);
        const float litY=dot(lit.rgb,weights),shadeY=dot(authoredAmbient,weights);
        if(litY>0.f&&shadeY>0.f){
            const float3 target=authoredAmbient*(litY/shadeY);
            const float3 proposed=lerp(lit.rgb,target,nativeAmbientShadeTint);
            if(all(isfinite(target))&&all(isfinite(proposed))){
                // Continuous positive chroma growth replaces binary RGB-range
                // selection. A neutral authored albedo stays neutral here.
                const float3 greyWeights=float3(1.f/3.f,1.f/3.f,1.f/3.f);
                const float scale=max(max(abs(lit.r),max(abs(lit.g),abs(lit.b))),max(abs(proposed.r),max(abs(proposed.g),abs(proposed.b))));
                const float3 scaledLit=lit.rgb/scale;
                const float3 chroma=scaledLit-dot(scaledLit,greyWeights);
                const float3 delta=(proposed-lit.rgb)/scale;
                const float3 chromaDelta=delta-dot(delta,greyWeights);
                const float changeEnergy=dot(chromaDelta,chromaDelta);
                // Preserve the existing proposal when its chroma projection is
                // supported by the authored colour. Fade continuously at zero
                // growth and neutral albedo; zero delta cannot change output.
                const float eligibility=saturate(dot(chroma,chromaDelta)/max(changeEnergy,1e-20f));
                // Normalizing a nearly black authored shade must not create
                // full-strength colour. Fade only below the reference's1/1024
                // relative energy scale, leaving ordinary shade colours intact.
                const float shadeStrength=saturate(1024.f*(shadeY/litY));
                if(isfinite(eligibility)&&isfinite(shadeStrength))
                    ambientAlbedo=lerp(lit.rgb,proposed,eligibility*shadeStrength);
            }
        }
    }
    // Explicit native adaptation only: the native filtered incident-light
    // visibility was applied above. Keep that filter out of the direct
    // normal ramp while retaining the original shade rate for ambient chroma.
    // Pure reference calls and additional lights retain their exact equations.
    float directRate=nativeFilteredShadow&&!light.additional?normalRate:rate;
    const float3 sourceColorBeforeCap=lerp(shade,lit.rgb,directRate)*lightColor*shadow+indirect*ambientAlbedo;
    float3 sourceColor=sourceColorBeforeCap;
    if(nativeFilteredShadow&&!light.outline){
        const float footprint=abs(dot(light.normal_dx,direction))+abs(dot(light.normal_dy,direction));
        if(legacy){
            const float grade=1.f-mtoonMaterial.shading.w*(1.f-s.grade);
            const float low=mtoonMaterial.shading.x,high=low+max(.0009765625f,lerp(1.f,low,mtoonMaterial.shading.y)-low);
            const float input=((ndl*.5f+.5f)*grade)*2.f-1.f;
            directRate=evrm_mtoon_pixel_ramp(input,low,high,footprint*abs(grade),directRate);
        }else{
            const float halfWidth=1.f-mtoonMaterial.shading.y;
            directRate=evrm_mtoon_pixel_ramp(ndl+mtoonMaterial.shading.x+s.shift*mtoonMaterial.rim_settings.w,-halfWidth,halfWidth,footprint,directRate);
        }
    }
    float3 color=lerp(shade,lit.rgb,directRate)*lightColor*shadow+indirect*ambientAlbedo;
    float3 gameMixture=color;
#if EVRM_GAME_LOOK
    if(nativeFilteredShadow&&!light.additional&&!light.outline){
        // Keep actual visible sunlight and GI in their original colour ratio.
        // Both native profiles retain the model's own shade palette. This
        // colour correction is independent of NillToon's cast-edge filtering;
        // existing limited lighting below still determines diffuse brightness.
        const float3 directAlbedo=evrm_game_shadow_palette(lerp(shade,lit.rgb,directRate),shade,light.attenuation);
        const float3 ambient=evrm_game_shadow_palette(ambientAlbedo,shade,light.attenuation);
        gameMixture=directAlbedo*lightColor*shadow+indirect*ambient;
    }
#endif
    float lightScale=1.f,indirectScale=1.f,matcapUnlitScale=1.f,emissionScale=1.f;
#if EVRM_NATIVE_MTOON_LIGHT_LIMIT
    if(nativeFilteredShadow&&!light.outline){
        // Native toon adaptation: limit INCIDENT illumination in exposed units,
        // before it multiplies authored textures. A common RGB scale retains
        // light hue and texture contrast; this is not a finished-colour curve.
        // Limit before cast visibility, otherwise intense light would undo
        // the native shadow filter until its visibility fell almost to zero.
        const float3 pointIncident=light.color*(.5f*(min(0.f,ndl)+1.f))*
            max(light.attenuation,nativeUnshadowedAttenuation);
        const float3 incident=light.additional?pointIncident:unoccludedLightColor;
        const float peak=max(incident.x,max(incident.y,incident.z));
        const float exposedPeak=peak*legacyCeilingExposure;
        lightScale=rcp(max(1.f,exposedPeak));
        const float giPeak=max(indirect.x,max(indirect.y,indirect.z))*legacyCeilingExposure;
        indirectScale=rcp(max(1.f,giPeak));
        // GI is an independent light source. A bright occluded sun cannot
        // darken it by consuming a shared illumination budget.
        color=lerp(shade,lit.rgb,directRate)*lightColor*shadow*lightScale+indirect*ambientAlbedo*indirectScale;
        // Those native fits compensated for per-channel material clipping.
        // Incoming-light limiting removes that cause; do not fit finished RGB
        // back toward the authored shade after preserving its texture ratios.
        nativeSaturatedAmbientShadeTint=0.f;
        // A native MatCap is reflected detail, not an emissive light source.
        // Keep low-exposure gain unchanged; remove dark-adaptation amplification
        // and fade its unlit share with available illumination. Real emission
        // and the source-version masks/mixes remain distinct below.
        const float3 visibleIncident=lightColor*shadow*lightScale+indirect*indirectScale;
        const float visiblePeak=max(visibleIncident.x,max(visibleIncident.y,visibleIncident.z))*legacyCeilingExposure;
        matcapUnlitScale=saturate(visiblePeak)/max(1.f,legacyCeilingExposure);
        // VRM emission is authored in the unit-light material convention.
        // Retain its glow without magnifying a baked shine texture through
        // dark adaptation. Unlike MatCap, it does not fade with scene light.
        emissionScale=rcp(max(1.f,legacyCeilingExposure));
    }
#endif
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
    // Exactly the factors that multiply the ramp colour and the ambient colour
    // above. A fully shadowed sun contributes nothing here.
    const float3 nativeLight=lightColor*shadow*lightScale+indirect*indirectScale;
#endif
    // The pure reference uses unit exposure. Native callers supply their
    // validated positive exposure so the authored base bound is expressed in
    // the same pre-exposed colour units as the native material path. Extras
    // below remain outside this base-only ceiling and receive exposure once.
    if(legacy&&!light.additional){
        const float3 ceiling=lit.rgb/legacyCeilingExposure,before=color;
        sourceColor=min(sourceColor,ceiling);
        color=min(color,ceiling);
        // Native appearance adaptation only. Intense ambient can erase the
        // earlier authored hue at this component-wise cap. Recover bounded
        // shade chroma after the cap, before rim/matcap/emission/held lights.
        // Pure reference defaults, VRM1 HDR and direct-only lighting stay apart.
        if(nativeSaturatedAmbientShadeTint>0.f){
            const float3 weights=float3(.2125f,.7154f,.0721f);
            const float ambientY=dot(max(sourceIndirect*ambientAlbedo,0.f),weights);
            const float totalY=dot(max(before,0.f),weights);
            // Use the selected raw GI, before the legacy epsilon colour floor.
            // Unused GI endpoints and negligible ambient must not enable a
            // finite correction under strong direct sunlight.
            const float ambientFraction=isfinite(ambientY)&&isfinite(totalY)&&totalY>0.f?
                saturate(ambientY/totalY):0.f;
            color=evrm_mtoon_native_ceiling_chroma(color,before,ceiling,shade/legacyCeilingExposure,nativeSaturatedAmbientShadeTint*ambientFraction);
        }
        // Strong native sunlight can also erase existing chroma at the cap.
        // Keep the established ambient result, then admit only a continuous
        // source-colour fit that cannot weaken its mean-centred RGB chroma.
        if(nativeSaturatedAmbientShadeTint>0.f)
            color=evrm_mtoon_native_ceiling_fit(color,before,ceiling,shade/legacyCeilingExposure,nativeSaturatedAmbientShadeTint);
    }
#if EVRM_GAME_LOOK
    if(nativeFilteredShadow&&!light.additional&&!light.outline)
        color=evrm_game_visible_light_colour(color,gameMixture,legacy?lit.rgb/legacyCeilingExposure:0,legacy);
#endif
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
    const float3 nativeBase=color;
#endif
    const float rimShape=pow(saturate(1.f-dot(n,view)+mtoonMaterial.rim_settings.z),max(.0009765625f,mtoonMaterial.rim_settings.y));
    const float3 rim=mtoonMaterial.rim.rgb*rimShape;
    const float3 rimLight=lerp(light.additional?float3(0,0,0):float3(1,1,1),lightColor*shadow+(light.additional?float3(0,0,0):(legacy?indirect:light.indirect)),mtoonMaterial.rim_settings.x);
    const float3 matcapGi=legacy?indirect:light.indirect;
    float matcapGiScale=1.f;
#if EVRM_NATIVE_MTOON_LIGHT_LIMIT
    if(nativeFilteredShadow&&!light.outline)
        matcapGiScale=rcp(max(1.f,max(matcapGi.x,max(matcapGi.y,matcapGi.z))*legacyCeilingExposure));
#endif
    const float3 nativeMatcapLight=lerp(light.additional?float3(0,0,0):matcapUnlitScale.xxx,
        lightColor*shadow*lightScale+(light.additional?float3(0,0,0):matcapGi)*matcapGiScale,mtoonMaterial.rim_settings.x);
    if(legacy){
        if(!light.outline){color+=rim*s.rim*rimLight;
            sourceColor+=rim*s.rim*rimLight;
            if(!light.additional){color+=s.matcap*matcapUnlitScale+mtoonMaterial.emission.rgb*s.emission*emissionScale;
                sourceColor+=s.matcap+mtoonMaterial.emission.rgb*s.emission;}}
    }else{
        // Preserve the reference expression/order outside the native policy.
#if EVRM_NATIVE_MTOON_LIGHT_LIMIT
        if(nativeFilteredShadow&&!light.outline)
            color+=rim*s.rim*rimLight+mtoonMaterial.matcap.rgb*s.matcap*s.rim*nativeMatcapLight;
        else
#endif
            color+=(rim+mtoonMaterial.matcap.rgb*s.matcap)*s.rim*rimLight;
        sourceColor+=(rim+mtoonMaterial.matcap.rgb*s.matcap)*s.rim*rimLight;
        if(!light.additional){color+=mtoonMaterial.emission.rgb*s.emission*emissionScale;sourceColor+=mtoonMaterial.emission.rgb*s.emission;}
    }
    EvrmMToonReferenceResult value;
    value.native_source_color=sourceColor;
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
    value.native_base=nativeBase;value.native_light=nativeLight;
#endif
    value.color=float4(color,mtoonMaterial.modes.z>=2u?lit.a:1.f);value.shade=rate;
    value.depth_write=mtoonMaterial.flags.z!=0u;
    value.discard_fragment=mtoonMaterial.modes.z==1u&&lit.a<mtoonMaterial.animation.w;
    return value;
}
#include "mtoon_native_source_bound.hlsli"
// Outline tint is a final operation over accumulated lighting.
float3 evrm_mtoon_outline_color(float3 radiance){
    if(mtoonMaterial.modes.y==0u&&mtoonMaterial.flags.y==0u)return mtoonMaterial.outline.rgb;
    return mtoonMaterial.outline.rgb*lerp(float3(1,1,1),radiance,mtoonMaterial.outline_settings.y);
}
EvrmMToonReferenceResult evrm_mtoon_reference(EvrmMToonLighting light,EvrmMToonSamples samples,float legacyCeilingExposure=1.f,float nativeAmbientShadeTint=0.f){
    EvrmMToonReferenceResult result=evrm_mtoon_reference_radiance(light,samples,legacyCeilingExposure,nativeAmbientShadeTint);
    if(light.outline)result.color.rgb=evrm_mtoon_outline_color(result.color.rgb);
    return result;
}
#endif
