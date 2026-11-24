#ifndef EVRM_NATIVE_MTOON_OVERLAY_SHADOW
#define EVRM_NATIVE_MTOON_OVERLAY_SHADOW
// Owned current-frame prefix/range supplied by the same retained frame bank.
// This qualifies the near-surface supplement, never any native read permission.
bool native_mtoon_same_actor_shadow(ByteAddressBuffer opaqueIds,ByteAddressBuffer owner,
    uint2 pixel,uint2 extent,uint generation,uint actor){
    if(!generation||!actor||actor>32u||any(extent==0u)||any(extent>16384u)||
       extent.x>16777216u/extent.y||any(pixel>=extent))return false;
    uint bytes;owner.GetDimensions(bytes);if(bytes<16u)return false;
    const uint4 range=owner.Load4(0);
    if(range.z!=generation||range.w!=actor||!range.y||range.x>0x7fffffffu||range.y>0x7fffffffu-range.x)return false;
    opaqueIds.GetDimensions(bytes);if(bytes/4u<extent.x*extent.y)return false;
    const uint key=opaqueIds.Load((pixel.y*extent.x+pixel.x)*4u),rank=key>>1u;
    return rank!=0u&&(rank-1u)>=range.x&&(rank-1u)-range.x<range.y;
}
#endif
