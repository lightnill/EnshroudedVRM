#include "native_mtoon_forward_records.hlsli"
// Only owned buffers cross from the admitted compute stage to native OIT.
#ifndef EVRM_FORWARD_CAPTURE_BINDING
#define EVRM_FORWARD_CAPTURE_BINDING 19
#endif
#ifndef EVRM_FORWARD_PACKET_BINDING
#define EVRM_FORWARD_PACKET_BINDING 20
#endif
[[vk::binding(EVRM_FORWARD_CAPTURE_BINDING,EVRM_NATIVE_DESCRIPTOR_SET)]] ByteAddressBuffer forwardCapture;
[[vk::binding(EVRM_FORWARD_PACKET_BINDING,EVRM_NATIVE_DESCRIPTOR_SET)]] ByteAddressBuffer forwardPackets;
bool evrm_forward_lookup(float4 position,uint actor,uint sourceTriangle,out float4 color){
    color=0;if(!actor||any(!isfinite(position))||position.z<=0.f||position.z>1.f)return false;
    uint bytes,packetBytes;forwardCapture.GetDimensions(bytes);forwardPackets.GetDimensions(packetBytes);
    if(bytes<32u||packetBytes<16u)return false;
    const uint4 h=forwardCapture.Load4(0),state=forwardCapture.Load4(16);uint records;
    if(state.y!=0u||state.w!=0u||state.x>state.z||!evrm_forward_shape(h,state.z,bytes,records)||
       any(forwardPackets.Load3(0)!=uint3(0x4d544650u,h.y,state.z))||packetBytes<16u+state.z*16u)return false;
    const uint2 pixel=uint2(position.xy);if(any(pixel>=h.zw))return false;
    const uint at=pixel.y*h.z+pixel.x;uint entry=forwardCapture.Load(32u+at*4u);
    [loop]for(uint n=0u;n<32u&&entry!=0xffffffffu;++n){
        if(entry>=state.x)return false;const uint offset=records+entry*24u;
        const uint4 a=forwardCapture.Load4(offset),b=forwardCapture.Load2(offset+16u).xyxy;
        if(a.x!=at)return false;
        if(a.z==actor&&(a.y>>1u)!=0u&&(a.y>>1u)-1u==sourceTriangle){
            const float depth=asfloat(b.y);
            if(!isfinite(depth)||abs(depth-position.z)>max(1e-7f,abs(depth)*1e-6f))return false;
            return evrm_forward_packet(forwardPackets.Load4(16u+entry*16u),a.y,color);
        }
        if(b.x==entry)return false;entry=b.x;
    }
    return false;
}
