#ifndef EVRM_NATIVE_MTOON_FORWARD_RECORDS
#define EVRM_NATIVE_MTOON_FORWARD_RECORDS
// OWNED bounded fragment storage. Header: magic,generation,width,height,
// allocated,overflow,capacity,reserved. Then per-pixel heads and24-byte records:
// pixel,key,actor,reciprocal-W,next,depth. No native inputs live here.
static const uint evrm_forward_magic=0x4d544652u;
bool evrm_forward_shape(uint4 header,uint capacity,uint bytes,out uint records){
    records=0;
    if(header.x!=evrm_forward_magic||!header.y||any(header.zw==0u)||any(header.zw>16384u)||
       header.z>16777216u/header.w||!capacity||capacity>262144u)return false;
    records=32u+header.z*header.w*4u;
    return bytes>=records+capacity*24u;
}
bool evrm_forward_packet(uint4 packet,uint key,out float4 color){
    color=0;if(packet.w!=key||packet.z!=0x46574431u)return false;
    const float4 decoded=f16tof32(uint4(packet.x&65535u,packet.x>>16u,packet.y&65535u,packet.y>>16u));
    if(any(!isfinite(decoded))||decoded.a<0.f||decoded.a>1.f)return false;
    color=decoded;return true;
}
#endif
