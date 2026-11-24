#ifndef EVRM_NATIVE_MTOON_COMPOSITE
#define EVRM_NATIVE_MTOON_COMPOSITE
// Owned per-pixel result: two packed HDR pairs, grey brightness/emission/tag,
// and the exact current visible-surface key. Not a native write permit.
bool native_mtoon_pack_composite(float4 hdr,float brightness,float emission,uint key,out uint4 packet){
    packet=0;
    if((key>>1u)==0u||any(!isfinite(hdr))||any(abs(hdr)>65504.0f)||hdr.a!=1.0f||
       !isfinite(brightness)||brightness<0.0f||brightness>65504.0f||!isfinite(emission)||emission<0.0f)return false;
    const uint4 half=f32tof16(hdr);
    packet=uint4(half.x|(half.y<<16u),half.z|(half.w<<16u),
        f32tof16(brightness)|(uint(round(saturate(emission)*255.0f))<<16u)|0x01000000u,key);
    return true;
}
// Only an explicitly outlined draw gets a token. Main and expanded shell
// share that token; other materials and actors cannot become colour donors.
bool native_mtoon_pack_owned_composite(float4 hdr,float brightness,float emission,uint key,uint owner,bool outline,float coverage,out uint4 packet){
    if(!native_mtoon_pack_composite(hdr,brightness,emission,key,packet))return false;
    if(owner==0u)return !outline;
    if(owner>4096u||!isfinite(coverage)||coverage<0.f||coverage>1.f){packet=0;return false;}
    // Twelve owner bits encode owner-1; the remaining four encode subpixel
    // coverage. Main pixels always have full coverage. Both are frame-local.
    const uint amount=outline?uint(round(coverage*15.f)):15u;
    packet.y=(packet.y&65535u)|(((owner-1u)|(amount<<12u))<<16u);
    packet.z=(packet.z&0x00ffffffu)|(outline?0x04000000u:0x03000000u);return true;
}
uint native_mtoon_composite_owner(uint4 packet){const uint tag=packet.z>>24u;return ((tag==3u&&(packet.y>>28u)==15u)||tag==4u)?((packet.y>>16u)&4095u)+1u:0u;}
float native_mtoon_composite_coverage(uint4 packet){return float(packet.y>>28u)*(1.f/15.f);}
bool native_mtoon_unpack_composite(uint4 packet,uint key,out float4 hdr,out float brightness,out float emission){
    hdr=0;brightness=0;emission=0;
    const uint tag=packet.z>>24u;
    if((key>>1u)==0u||packet.w!=key||(tag!=1u&&!native_mtoon_composite_owner(packet)))return false;
    const float4 value=f16tof32(uint4(packet.x&65535u,packet.x>>16u,packet.y&65535u,tag==1u?packet.y>>16u:0x3c00u));
    const float grey=f16tof32(packet.z&65535u);
    if(any(!isfinite(value))||value.a!=1.0f||!isfinite(grey)||grey<0.0f)return false;
    hdr=value;brightness=grey;emission=float((packet.z>>16u)&255u)*(1.0f/255.0f);return true;
}
#endif
