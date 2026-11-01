[[vk::binding(0, 0)]] RWStructuredBuffer<uint> values;
[numthreads(8, 1, 1)]
void cs_main(uint3 id : SV_DispatchThreadID) {
    if (id.x < 32) values[id.x] = values[id.x] * 3 + 7;
}
