#define EVRM_MTOON_FORWARD_CAPTURE 1
#include "avatar_native_mtoon_main.hlsl"
#include "native_mtoon_forward_records.hlsli"
// Late GBuffer coverage only. Native attachment masks and depth writes are zero.
[earlydepthstencil]
void ps_forward_capture(NativeMainVertexOut input,bool front:SV_IsFrontFace,uint primitive:SV_PrimitiveID){
    float animation;const float4 base=native_main_base(input,animation);
    if(saturate(base.a)==0.f||primitive>=mainDraw.domain.w||input.position.z<=0.f||input.position.z>1.f)return;
    uint bytes;mainIdentity.GetDimensions(bytes);if(bytes<32u)return;
    const uint4 h=mainIdentity.Load4(0),state=mainIdentity.Load4(16);uint records;
    if(h.y!=mainDraw.domain.z||any(h.zw!=mainDraw.domain.xy)||state.w!=0u||
       !evrm_forward_shape(h,state.z,bytes,records))return;
    const uint2 pixel=uint2(input.position.xy);if(any(pixel>=h.zw))return;
    uint entry;mainIdentity.InterlockedAdd(16u,1u,entry);
    // Every write is capacity checked. Overflow is sticky for the entire view;
    // even a diagnostic counter wrap cannot re-enable lookup or borrowed reads.
    if(entry>=state.z){uint unused;mainIdentity.InterlockedOr(20u,1u,unused);return;}
    const uint at=pixel.y*h.z+pixel.x;uint previous;
    mainIdentity.InterlockedExchange(32u+at*4u,entry,previous);
    // Domain.z is generation in this independent capture; triangle keys use
    // source index ordinals. The shade draw supplies that exact same domain.
    const uint sourceTriangle=mainDraw.geometry.z/3u+primitive;
    const uint key=((sourceTriangle+1u)<<1u)|(front?1u:0u);
    mainIdentity.Store4(records+entry*24u,uint4(at,key,mainControl.facts.y,asuint(input.position.w)));
    mainIdentity.Store2(records+entry*24u+16u,uint2(previous,asuint(input.position.z)));
}
