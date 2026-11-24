#ifndef EVRM_MTOON_NATIVE_SOURCE_BOUND
#define EVRM_MTOON_NATIVE_SOURCE_BOUND 1
// Positive upper bound over every late main-light visibility. No native reads,
// destination authority or relaxed generation checks. Source samples already
// contain the admitted emission gain, just as the ordinary reference does.
float3 evrm_mtoon_native_source_bound(EvrmMToonLighting light,EvrmMToonSamples s,float exposure,float ambientTint){
    const bool legacy=mtoonMaterial.modes.y==0u;
    const float3 lit=mtoonMaterial.base.rgb*s.base.rgb,shade=mtoonMaterial.shade.rgb*s.shade;
    const float3 direct=any(light.color>0.f)?(legacy?evrm_mtoon_light_color(light.color,mtoonMaterial.indirect.x):light.color):0;
    const float3 selected=legacy?evrm_mtoon_light_color(lerp(light.equalized,light.indirect,mtoonMaterial.indirect.y),mtoonMaterial.indirect.x):
        lerp(light.indirect,light.equalized,mtoonMaterial.indirect.z);
    float3 ambientBound=abs(lit);
    if(all(lit>=0.f)&&all(shade>=0.f)){
        const float3 weights=float3(.2125f,.7154f,.0721f);
        const float litY=dot(lit,weights),shadeMaximum=max(shade.x,max(shade.y,shade.z));
        // Each channel of normalized lerp(shade,lit,rate) is a ratio of
        // affine functions with positive denominator: its extrema are at
        // the endpoints. Eligibility/near-black fade only reduce the mix.
        if(litY>0.f&&shadeMaximum>0.f){
            // Normalize the shade before multiplying by lit luminance: the
            // direct litY/shadeY ratio can overflow for a finite tiny shade.
            const float3 scaledShade=shade/shadeMaximum;
            const float3 endpoint=(scaledShade/dot(scaledShade,weights))*litY;
            ambientBound=lit+saturate(ambientTint)*max(endpoint-lit,0.f);
        }
    }else{
        // Signed synthetic inputs: retain the existing near-black strength
        // bound rather than assuming a positive normalized palette.
        ambientBound=abs(lit)+abs(ambientTint)*1024.f*max(abs(lit),abs(shade));
    }
    float3 baseBound=max(abs(lit),abs(shade))*abs(direct)+ambientBound*abs(selected);
    if(legacy&&all(lit>=0.f)&&all(shade>=0.f))baseBound=min(baseBound,lit/exposure);
    const float3 n=evrm_mtoon_unit(light.normal,float3(0,0,1)),view=evrm_mtoon_unit(light.view,float3(0,0,1));
    const float rimShape=pow(saturate(1.f-dot(n,view)+mtoonMaterial.rim_settings.z),max(.0009765625f,mtoonMaterial.rim_settings.y));
    const float3 rim=mtoonMaterial.rim.rgb*rimShape;
    const float mix=mtoonMaterial.rim_settings.x;
    const float3 rimLightBound=abs(1.f-mix)+abs(mix)*(abs(direct)+abs(legacy?selected:light.indirect));
    const float3 extras=legacy?abs(rim*s.rim)*rimLightBound+abs(s.matcap):
        (abs(rim)+abs(mtoonMaterial.matcap.rgb*s.matcap))*abs(s.rim)*rimLightBound;
    return baseBound+extras+abs(mtoonMaterial.emission.rgb*s.emission);
}
#endif
