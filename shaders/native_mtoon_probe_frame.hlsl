// Independent full current-state copy after the pinned native probe update.
// The source transaction proves classifier/converge/update resources, backing,
// queue and native lifetime. A scalar permission alone grants none of them.
struct ProbeParameters {float4 origins[8];int4 gridOffsets[8];uint4 prefilter[8];uint4 control[6];};
[[vk::binding(0,0)]] ConstantBuffer<ProbeParameters> coefficientsParameters;
[[vk::binding(3,0),vk::image_format("rgba16f")]] RWTexture3D<float4> nativeRed;
[[vk::binding(4,0),vk::image_format("rgba16f")]] RWTexture3D<float4> nativeGreen;
[[vk::binding(5,0),vk::image_format("rgba16f")]] RWTexture3D<float4> nativeBlue;
[[vk::binding(0,1)]] ConstantBuffer<ProbeParameters> stateParameters;
[[vk::binding(1,1)]] ByteAddressBuffer nativeState;
[[vk::binding(2,1)]] ByteAddressBuffer nativeBounds;
[[vk::binding(0,2)]] RWByteAddressBuffer snapshot;
struct SourcePermit {uint4 words;};
[[vk::binding(1,2)]] ConstantBuffer<SourcePermit> sourcePermit;
[[vk::binding(2,2)]] ConstantBuffer<ProbeParameters> postUpdateParameters;
[[vk::binding(3,2)]] ConstantBuffer<ProbeParameters> firstUpdateParameters;
[numthreads(64,1,1)] void cs_probe_frame(uint3 id:SV_DispatchThreadID) {
    // No borrowed descriptor read may precede this gate.
    if(sourcePermit.words.x!=1u || id.y!=0u || id.z!=0u || id.x>=32768u)return;
    uint stateBytes,boundsBytes,outputBytes;nativeState.GetDimensions(stateBytes);nativeBounds.GetDimensions(boundsBytes);snapshot.GetDimensions(outputBytes);
    if(stateBytes<131072u || boundsBytes<393216u || outputBytes<2097440u)return;
    uint3 redExtent,greenExtent,blueExtent;
    nativeRed.GetDimensions(redExtent.x,redExtent.y,redExtent.z);nativeGreen.GetDimensions(greenExtent.x,greenExtent.y,greenExtent.z);nativeBlue.GetDimensions(blueExtent.x,blueExtent.y,blueExtent.z);
    if(any(redExtent!=uint3(128,16,16)) || any(greenExtent!=redExtent) || any(blueExtent!=redExtent))return;
    [unroll] for(uint i=0u;i<8u;++i)
        if(any(asuint(coefficientsParameters.origins[i])!=asuint(stateParameters.origins[i])) ||
           any(coefficientsParameters.gridOffsets[i]!=stateParameters.gridOffsets[i]) ||
           any(asuint(coefficientsParameters.origins[i])!=asuint(postUpdateParameters.origins[i])) ||
           any(coefficientsParameters.gridOffsets[i]!=postUpdateParameters.gridOffsets[i]) ||
           any(asuint(coefficientsParameters.origins[i])!=asuint(firstUpdateParameters.origins[i])) ||
           any(coefficientsParameters.gridOffsets[i]!=firstUpdateParameters.gridOffsets[i]))return;
    // Only Converge establishes the coefficient units. Other stages use their
    // own slices and stage-local controls, not a byte-identical 480-byte block.
    const float3 units=asfloat(coefficientsParameters.control[2].xyz);
    if(any(!isfinite(units)) || units.x<0.0f || units.y<=0.0f || units.z<=0.0f)return;
    if(id.x==0u) {
        [unroll] for(uint h=0u;h<8u;++h) {
            snapshot.Store4(h*16u,asuint(stateParameters.origins[h]));
            snapshot.Store4(128u+h*16u,asuint(stateParameters.gridOffsets[h]));
        }
        snapshot.Store4(256u,uint4(asuint(units),0));snapshot.Store4(272u,uint4(0x4d544650u,32768,0,0));
    }
    const uint cascade=id.x/4096u,local=id.x%4096u;
    const uint3 logical=uint3(local%16u,(local/16u)%16u,local/256u);
    const uint3 rolled=(logical+asuint(stateParameters.gridOffsets[cascade].xyz))&15u;
    const uint state=nativeState.Load((cascade*4096u+rolled.x+16u*(rolled.y+16u*rolled.z))*4u);
    const uint at=288u+id.x*64u;
    if((state>>16u)>0u && (state&22u)==0u) {
        const uint3 texel=rolled+uint3(cascade*16u,0,0);
        snapshot.Store4(at,asuint(nativeRed[texel]));snapshot.Store4(at+16u,asuint(nativeGreen[texel]));snapshot.Store4(at+32u,asuint(nativeBlue[texel]));
        snapshot.Store4(at+48u,uint4(nativeBounds.Load3(id.x*12u),state));
    }else snapshot.Store4(at+48u,uint4(0,0,0,state));
}
