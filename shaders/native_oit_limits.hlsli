#ifndef EVRM_NATIVE_OIT_LIMITS_INCLUDED
#define EVRM_NATIVE_OIT_LIMITS_INCLUDED

// Private BLEND highlight policy, after finite scene lighting/exposure and
// before unchanged alpha/weight. The early common proof uses this same bound.
// At standard depth the weight is <=0.729; 88000*0.73=64240 leaves FP16 margin.
static const float evrm_native_oit_radiance_limit=88000.f;
float3 evrm_native_oit_limit_radiance(float3 radiance) {
    const float magnitude=max(abs(radiance.x),max(abs(radiance.y),abs(radiance.z)));
    // One positive scale preserves RGB ratios/sign within ordinary FP rounding.
    // The common proof requires abs(raw RGB)<=1e30; reject nonfinite RGB first.
    return radiance*(evrm_native_oit_radiance_limit/max(evrm_native_oit_radiance_limit,magnitude));
}

#endif
