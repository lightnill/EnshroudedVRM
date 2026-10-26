[[vk::binding(0, 0)]] cbuffer Frame {
    float4 clipRows[4];
    float4 screenInfo; // xy: inverse target size; zw: native temporal jitter UV.
};
[[vk::binding(1, 0)]] Texture2D<float4> baseTexture;
[[vk::binding(2, 0)]] SamplerState baseSampler;
[[vk::binding(3, 0)]] Texture2D<float> sceneDepth;
[[vk::binding(4, 0)]] SamplerState depthSampler;
struct Material {
    float4 baseColor;
    float alphaCutoff;
    uint alphaMode;
    uint unlit;
    uint gammaEncode;
};
[[vk::push_constant]] ConstantBuffer<Material> material;
struct VertexInput { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct FragmentInput { float4 position : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
FragmentInput vs_main(VertexInput input) {
    FragmentInput result;
    float4 p = float4(input.position, 1);
    result.position = float4(dot(clipRows[0],p), -dot(clipRows[1],p), dot(clipRows[2],p), dot(clipRows[3],p));
    result.normal = input.normal;
    result.uv = input.uv;
    return result;
}
float4 ps_main(FragmentInput input) : SV_Target0 {
    // Stable avatar raster coordinates map back to jittered native depth.
    float depth = sceneDepth.SampleLevel(depthSampler, input.position.xy * screenInfo.xy - screenInfo.zw, 0);
    // This verified game build uses a reversed-Z projection. Native depth is
    // sampled read-only; a separate depth attachment handles avatar self-depth.
    if (input.position.z + 0.0000005 < depth) discard;
    float4 color = baseTexture.Sample(baseSampler, input.uv) * material.baseColor;
    if (material.alphaMode == 1 && color.a < material.alphaCutoff) discard;
    if (material.alphaMode == 0) color.a = 1;
    if (!material.unlit) color.rgb *= 0.45 + 0.55 * saturate(dot(normalize(input.normal), normalize(float3(0.4,0.8,0.3))));
    if (material.gammaEncode) {
        float3 c = max(color.rgb, 0);
        color.rgb = lerp(12.92*c, 1.055*pow(c, 1.0/2.4)-0.055, step(0.0031308, c));
    }
    return color;
}
