// Explicit report request only. Sample the game image into bounded host storage.
[[vk::binding(0,0)]] Texture2D<float4> sourceImage;
[[vk::binding(1,0)]] SamplerState sourceSampler;
[[vk::binding(2,0)]] RWStructuredBuffer<uint> pixels;
struct CaptureSize {uint width,height,srgb,padding;};
[[vk::push_constant]] ConstantBuffer<CaptureSize> size;
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(id.x>=size.width||id.y>=size.height)return;
    float3 rgb=saturate(sourceImage.SampleLevel(sourceSampler,(float2(id.xy)+.5)/float2(size.width,size.height),0).rgb);
    // Sampling an sRGB image decodes it. PNG/preview pixels use encoded sRGB.
    if(size.srgb)rgb=lerp(12.92*rgb,1.055*pow(rgb,1.0/2.4)-.055,step(.0031308,rgb));
    uint3 value=uint3(round(saturate(rgb)*255));pixels[id.y*size.width+id.x]=value.r|(value.g<<8)|(value.b<<16)|0xff000000;
}
