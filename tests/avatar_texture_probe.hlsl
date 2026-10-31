[[vk::binding(0,0)]] Texture2D<float4> sourceTexture;
[[vk::binding(1,0)]] RWStructuredBuffer<float4> result;
[numthreads(1,1,1)] void cs_main(){result[0]=sourceTexture.Load(int3(0,0,0));}
