#ifndef EVRM_NATIVE_MTOON_GAME_SHADOWS
#define EVRM_NATIVE_MTOON_GAME_SHADOWS
#include "native_mtoon_scene_position.hlsli"
uint evrm_game_sun_neighbours(ByteAddressBuffer owned,uint2 pixel,uint2 extent,uint first,uint count){
    if(any(extent==0u)||any(extent>16384u)||extent.x>16777216u/extent.y||any(pixel>=extent)||count==0u)return 0u;
    uint bytes;owned.GetDimensions(bytes);if(bytes/4u<extent.x*extent.y)return 0u;
    const uint centre=owned.Load((pixel.y*extent.x+pixel.x)*4u),centreRank=(centre>>1u)-1u;
    if((centre>>1u)==0u||centreRank<first||centreRank-first>=count)return 0u;
    const int2 offsets[8]={int2(-1,0),int2(1,0),int2(0,-1),int2(0,1),int2(-2,0),int2(2,0),int2(0,-2),int2(0,2)};uint admitted=0u;
    [unroll]for(uint tap=0u;tap<8u;++tap){
        const int2 q=int2(pixel)+offsets[tap];if(any(q<0)||any(q>=int2(extent)))continue;
        const uint key=owned.Load((uint(q.y)*extent.x+uint(q.x))*4u),rank=(key>>1u)-1u;
        if((key>>1u)!=0u&&rank>=first&&rank-first<count)admitted|=1u<<tap;
    }
    return admitted;
}
// Original-dispatch helper only: caller has already admitted current mask/depth
// objects and their full view. It creates neither readiness nor late read rights.
bool evrm_game_sun_shadow(NativeMToonSceneLightParameters scene,NativeMToonScenePosition centrePosition,float centreDepth,
    uint2 pixel,uint2 extent,Texture2D<float> depth,Texture2D<float> mask,float centre,out float value,uint admitted=255u){
    value=centre;
    if(!isfinite(centre)||centre<0.f||centre>1.f||any(pixel>=extent))return false;
    uint mw,mh;mask.GetDimensions(mw,mh);if(any(uint2(mw,mh)!=extent))return false;
    uint dw,dh;depth.GetDimensions(dw,dh);if(any(uint2(dw,dh)!=extent))return true;
    const int2 offsets[8]={int2(-1,0),int2(1,0),int2(0,-1),int2(0,1),int2(-2,0),int2(2,0),int2(0,-2),int2(0,2)};
    float delta=0.f,total=6.f;
    [unroll]for(uint tap=0u;tap<8u;++tap){
        if((admitted&(1u<<tap))==0u)continue;
        const int2 neighbour=int2(pixel)+offsets[tap];if(any(neighbour<0)||any(neighbour>=int2(extent)))continue;
        NativeMToonScenePosition actual,plane;
        const float neighbourDepth=depth.Load(int3(neighbour,0));
        if(!native_mtoon_scene_position(scene,uint2(neighbour),extent,neighbourDepth,actual)||
           !native_mtoon_scene_position(scene,uint2(neighbour),extent,centreDepth,plane))continue;
        const float footprint=length(plane.relative-centrePosition.relative),separation=length(actual.relative-plane.relative);
        if(!isfinite(footprint)||!isfinite(separation)||footprint<=1e-20f)continue;
        const float weight=(tap<4u?2.f:.5f)*saturate(1.f-separation/(4.f*footprint));if(weight<=0.f)continue;
        const float visibility=mask.Load(int3(neighbour,0));
        if(!isfinite(visibility)||visibility<0.f||visibility>1.f)return false;
        delta+=weight*(visibility-centre);total+=weight;
    }
    // Zero differences retain uniform interiors exactly, including partial
    // visibility. This filters the shadow boundary, not the surface gradient.
    value=saturate(centre+delta/total);return isfinite(value);
}
#endif
