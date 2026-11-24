#ifndef EVRM_MTOON_PIXEL_RAMP
#define EVRM_MTOON_PIXEL_RAMP
// Exact box-footprint average of an authored linear toon ramp. Only an edge
// within the current pixel's normal variation changes; there is no global
// band-width multiplier, image blur or change to native cast-shadow samples.
float evrm_mtoon_pixel_ramp(float input,float low,float high,float footprint,float original){
    const float width=max(0.f,high-low);
    if(!isfinite(footprint)||footprint<=8.f*1.192092896e-7f*max(1.f,max(abs(input),width)))return original;
    const float a=input-low-.5f*footprint,b=input-low+.5f*footprint;
    if(b<=0.f)return 0.f;
    if(a>=width)return 1.f;
    if(width==0.f)return saturate(b/footprint);
    const float left=max(a,0.f),right=min(b,width);
    const float rising=right>left?(right-left)*(right+left)/(2.f*width):0.f;
    const float full=max(0.f,b-max(a,width));
    return saturate((rising+full)/footprint);
}
#endif
