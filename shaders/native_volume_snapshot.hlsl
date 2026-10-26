[[vk::binding(0,0)]] Texture3D<float4> sourceRadiance;
[[vk::binding(1,0)]] Texture3D<float> sourceVisibility;
[[vk::binding(2,0),vk::image_format("rgba32f")]] RWTexture3D<float4> snapshotRadiance;
[[vk::binding(3,0),vk::image_format("r32f")]] RWTexture3D<float> snapshotVisibility;
[numthreads(4,4,4)]
void cs_snapshot(uint3 id : SV_DispatchThreadID) {
    uint w,h,d;snapshotRadiance.GetDimensions(w,h,d);
    if(any(id>=uint3(w,h,d)))return;
    // Point copy from the immediate full native producer. No sampler/filtering.
    snapshotRadiance[id]=sourceRadiance.Load(int4(id,0));
    snapshotVisibility[id]=sourceVisibility.Load(int4(id,0));
}
