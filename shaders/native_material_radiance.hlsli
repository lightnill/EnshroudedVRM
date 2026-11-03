#ifndef EVRM_NATIVE_MATERIAL_RADIANCE_INCLUDED
#define EVRM_NATIVE_MATERIAL_RADIANCE_INCLUDED

// Matcaps and unlit rims describe reflected appearance, not a light source.
// Retain the existing value in daylight; do not let exposure/emissive gain
// alone amplify it above its authored linear colour. Real emission is separate.
float evrm_reflective_scale(float exposure,float gain) {
    if(!isfinite(exposure)||!isfinite(gain)||exposure<=0.f||gain<=0.f)return 0.f;
    const float amplification=exposure*gain;
    if(!isfinite(amplification))return 0.f;
    return 1.f/max(1.f,amplification);
}

float3 evrm_forward_radiance(float3 base,float3 light,float3 lit,float3 emission,
    float3 unlit,float exposure,bool legacy) {
    const float3 reflected=unlit*evrm_reflective_scale(exposure,1.f);
    if(legacy){
        // Legacy MToon caps base diffuse before its independently authored rim,
        // matcap and emission. Keep those additions and continuous alpha intact.
        const float3 diffuse=min(base*light*exposure,base);
        return diffuse+(lit*light+emission+reflected)*exposure;
    }
    return ((base+lit)*light+emission+reflected)*exposure;
}
#endif
