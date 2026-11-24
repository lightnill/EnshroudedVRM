#ifndef EVRM_NATIVE_MTOON_SURFACE
#define EVRM_NATIVE_MTOON_SURFACE
// Reconstruct smooth attributes from the *same frozen triangle and clip
// transform* that produced an admitted surface key. Visibility, pose lifetime,
// frame identity and the key-to-triangle lookup are caller obligations. This
// function cannot turn arbitrary geometry or a depth sample into that proof.
bool native_mtoon_surface_weights(float4 a,float4 b,float4 c,float2 pixel,float4 viewport,out float3 weights) {
    weights=0;
    if(!all(isfinite(a)) || !all(isfinite(b)) || !all(isfinite(c)) ||
        !all(isfinite(pixel)) || !all(isfinite(viewport)) || any(viewport.zw<=0) ||
        any(pixel<viewport.xy) || any(pixel-viewport.xy>=viewport.zw) ||
        a.w<=0 || b.w<=0 || c.w<=0) return false;
    float2 p0=a.xy/a.w,p1=b.xy/b.w,p2=c.xy/c.w;
    float2 samplePoint=((pixel-viewport.xy)/viewport.zw)*2.f-1.f;
    float2 e1=p1-p0,e2=p2-p0,at=samplePoint-p0;
    if(!all(isfinite(e1)) || !all(isfinite(e2)) || !all(isfinite(at))) return false;
    float scale=max(max(abs(e1.x),abs(e1.y)),max(abs(e2.x),abs(e2.y)));
    if(scale<=0) return false;
    e1/=scale;e2/=scale;at/=scale;
    float determinant=e1.x*e2.y-e1.y*e2.x;
    if(!isfinite(determinant) || abs(determinant)<1e-12f) return false;
    float u=(at.x*e2.y-at.y*e2.x)/determinant;
    float v=(e1.x*at.y-e1.y*at.x)/determinant;
    float3 affine=float3(1.f-u-v,u,v);
    // Tiny edge disagreement may arise from raster subpixel quantization.
    // The surface key supplies coverage; this tolerance supplies none.
    if(!all(isfinite(affine)) || any(affine<-.00390625f) || any(affine>1.00390625f)) return false;
    float3 q=affine/float3(a.w,b.w,c.w);float sum=q.x+q.y+q.z;
    if(!all(isfinite(q)) || !isfinite(sum) || sum<=0) return false;
    weights=q/sum;
    if(!all(isfinite(weights))){weights=0;return false;}
    return true;
}
#endif
