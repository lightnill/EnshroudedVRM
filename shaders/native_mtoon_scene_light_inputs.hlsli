#ifndef EVRM_NATIVE_MTOON_SCENE_LIGHT_INPUTS
#define EVRM_NATIVE_MTOON_SCENE_LIGHT_INPUTS
struct NativeMToonScenePixelLight {
    float3 transmittance;
    float sun_visibility,fog_shadow,material_occlusion,ssao;
    // Native feature64 has additional per-pixel producers. Its sun and GI
    // multipliers must be computed explicitly, never inferred from the LUT
    // or the candidate's fourteen-role image mask.
    float3 sunlight_modulation;
    float gi_modulation;
    bool native_modulation_available;
    // Sampled native diffuse-irradiance cube values before intensity/1-pi.
    float3 environment_normal,environment_up,environment_down;
    uint available_images;
};
struct NativeMToonSceneModulation {float3 sunlight;float gi;bool available;};
#endif
