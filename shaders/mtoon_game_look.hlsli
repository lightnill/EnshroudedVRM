#ifndef EVRM_MTOON_GAME_LOOK_INCLUDED
#define EVRM_MTOON_GAME_LOOK_INCLUDED
#ifndef EVRM_GAME_LOOK
#define EVRM_GAME_LOOK 0
#endif
// Owned material padding:0 compatibility,1 NillToon. Pure reference ignores it.
bool evrm_mtoon_nilltoon(){return mtoonMaterial.shade.w==1.f;}
// Artistic native policy only. Source validation and physical light availability
// remain external; these helpers cannot manufacture light or repair bad inputs.
float3 evrm_game_shadow_palette(float3 albedo,float3 shade,float visibility){
    if(any(!isfinite(albedo))||any(albedo<0.f)||any(!isfinite(shade))||any(shade<0.f)||
       !isfinite(visibility)||visibility<0.f||visibility>=1.f)return albedo;
    const float3 weights=float3(.2125f,.7154f,.0721f);
    const float energy=dot(albedo,weights),shadeEnergy=dot(shade,weights),peak=max(shade.x,max(shade.y,shade.z));
    if(!isfinite(energy)||!isfinite(shadeEnergy)||energy<=0.f||peak<=0.f)return albedo;
    // Retain the established near-black authored-palette fade. Normalize before
    // multiplying luminance, so tiny/HDR palettes need no huge shade ratio.
    const float strength=shadeEnergy>=energy/1024.f?1.f:(shadeEnergy/energy)*1024.f;
    const float3 unit=shade/peak;const float unitY=dot(unit,weights);
    const float3 target=unit*(energy/unitY);
    if(any(!isfinite(target))||!isfinite(strength))return albedo;
    return lerp(albedo,target,(1.f-visibility)*strength);
}
float3 evrm_game_visible_light_colour(float3 bounded,float3 mixture,float3 ceiling,bool useCeiling){
    if(any(!isfinite(bounded))||any(bounded<0.f)||any(!isfinite(mixture))||any(mixture<0.f))return bounded;
    const float3 weights=float3(.2125f,.7154f,.0721f);
    const float energy=dot(bounded,weights),peak=max(mixture.x,max(mixture.y,mixture.z));
    if(!isfinite(energy)||energy<=0.f||peak<=0.f)return bounded;
    const float3 unit=mixture/peak;const float unitY=dot(unit,weights);
    const float3 target=unit*(energy/unitY);
    if(any(!isfinite(target)))return bounded;
    float3 value=target;
    if(useCeiling){
        if(any(!isfinite(ceiling))||any(ceiling<0.f))return bounded;
        // A segment beginning at bounded has a colour cliff when one of its
        // channels is already at its ceiling. Instead project continuously:
        // trim only the target's excess, then restore the lost luminance along
        // the remaining authored headroom. Feasible targets are unchanged.
        const float3 clipped=min(target,ceiling),headroom=ceiling-clipped;
        const float capacity=dot(headroom,weights),deficit=max(0.f,energy-dot(clipped,weights));
        if(!isfinite(capacity)||!isfinite(deficit))return bounded;
        value=min(ceiling,clipped+(capacity>0.f?saturate(deficit/capacity):0.f)*headroom);
    }
    return all(isfinite(value))?value:bounded;
}
#endif
