#ifndef EVRM_NATIVE_MTOON_PROBE_FRAME
#define EVRM_NATIVE_MTOON_PROBE_FRAME
// Only consumes the owned, complete post-update snapshot. Source admission,
// full dispatch and retirement remain the enclosing transaction's obligations.
bool native_mtoon_sample_probe_frame(ByteAddressBuffer frame,float3 worldPosition,float exposure,
                                    out float4 red,out float4 green,out float4 blue) {
    red=0;green=0;blue=0;
    uint bytes;frame.GetDimensions(bytes);
    if(bytes<2097440u)return false;
    if(any(frame.Load4(272u)!=uint4(0x4d544650u,32768,0,0)) || any(!isfinite(worldPosition)) || !isfinite(exposure) || exposure<=0.0f)return false;
    const float3 units=asfloat(frame.Load3(256u));
    if(any(!isfinite(units)) || units.x<0.0f || units.y<=0.0f || units.z<=0.0f || abs(units.z-exposure)>max(units.z,exposure)*0.0001f)return false;
    uint cascade=8u;float4 origin=0;
    [loop] for(uint c=0u;c<8u;++c) {
        const float4 candidate=asfloat(frame.Load4(c*16u));
        if(any(!isfinite(candidate)) || candidate.w<=0.0f)continue;
        const float3 coordinate=(worldPosition-candidate.xyz)/candidate.w;
        if(all(isfinite(coordinate)) && all(coordinate>=0.5f) && all(coordinate<=15.5f)){cascade=c;origin=candidate;break;}
    }
    if(cascade==8u)return false;
    const float3 grid=(worldPosition-origin.xyz)/origin.w-0.5f;
    const int3 base=int3(grid);const float3 fraction=saturate(grid-float3(base));
    float4 r=0,g=0,b=0;float weightSum=0;
    [loop] for(uint corner=0u;corner<8u;++corner) {
        const uint3 step=uint3(corner&1u,(corner>>1u)&1u,(corner>>2u)&1u);const int3 coordinate=base+int3(step);
        if(any(coordinate<0) || any(coordinate>=16))continue;
        const uint index=cascade*4096u+uint(coordinate.x+16*(coordinate.y+16*coordinate.z)),at=288u+index*64u;
        const uint4 stateBounds=frame.Load4(at+48u);
        if((stateBounds.w>>16u)==0u || (stateBounds.w&22u)!=0u)continue;
        const float3 positive=f16tof32(stateBounds.xyz&65535u),negative=f16tof32(stateBounds.xyz>>16u);
        if(any(!isfinite(positive)) || any(!isfinite(negative)) || any(positive<0.0f) || any(negative<0.0f))return false;
        const float3 center=origin.xyz+(float3(coordinate)+0.5f)*origin.w;
        if(any(worldPosition<center-negative) || any(worldPosition>center+positive))continue;
        const float4 cellR=asfloat(frame.Load4(at)),cellG=asfloat(frame.Load4(at+16u)),cellB=asfloat(frame.Load4(at+32u));
        if(any(!isfinite(cellR)) || any(!isfinite(cellG)) || any(!isfinite(cellB)))return false;
        const float3 axisWeight=max(0.001f,lerp(1.0f-fraction,fraction,float3(step)));
        const float weight=axisWeight.x*axisWeight.y*axisWeight.z;
        r+=cellR*weight;g+=cellG*weight;b+=cellB*weight;weightSum+=weight;
    }
    if(weightSum>0.0f) {
        r/=weightSum;g/=weightSum;b/=weightSum;
        if(any(!isfinite(r)) || any(!isfinite(g)) || any(!isfinite(b)))return false;
        red=r;green=g;blue=b;
    }
    return true;
}
#endif
