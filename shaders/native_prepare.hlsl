// Original additive preparation shader. All native sources remain read-only.
// Lighting qualifies observed active-view placement, not entity ownership.
// Previous motion is read only from a retained, successfully submitted private
// frame. Its GPU commit is checked independently of current replacement.
#include "native_oit_limits.hlsli"
struct PrepareControl {
    uint4 permit; uint4 offsets; uint4 extent; float4 config;
    uint4 parts[64]; uint4 regions[32];
    uint4 blend; float4 blendMin,blendMax,blendBaseMax,blendEmissiveMax;
    uint4 history;
    uint4 shadow; uint4 shadowViews[32];
};
struct UniformWindow { float4 rows[4096]; };
[[vk::binding(0,0)]] ConstantBuffer<PrepareControl> control;
[[vk::binding(1,0)]] ByteAddressBuffer sourceInstances;
[[vk::binding(2,0)]] ByteAddressBuffer sourceCulled;
[[vk::binding(3,0)]] ByteAddressBuffer indirectBuffer;
[[vk::binding(4,0)]] ByteAddressBuffer countBuffer;
[[vk::binding(5,0)]] RWByteAddressBuffer preparedFrame;
[[vk::binding(70,0)]] ByteAddressBuffer previousFrame;
// Independent descriptors avoid dynamic UBO descriptor indexing.
// Bind valid 64KiB dummy windows beyond the actual arena.
[[vk::binding(6,0)]] ConstantBuffer<UniformWindow> window00;
[[vk::binding(7,0)]] ConstantBuffer<UniformWindow> window01;
[[vk::binding(8,0)]] ConstantBuffer<UniformWindow> window02;
[[vk::binding(9,0)]] ConstantBuffer<UniformWindow> window03;
[[vk::binding(10,0)]] ConstantBuffer<UniformWindow> window04;
[[vk::binding(11,0)]] ConstantBuffer<UniformWindow> window05;
[[vk::binding(12,0)]] ConstantBuffer<UniformWindow> window06;
[[vk::binding(13,0)]] ConstantBuffer<UniformWindow> window07;
[[vk::binding(14,0)]] ConstantBuffer<UniformWindow> window08;
[[vk::binding(15,0)]] ConstantBuffer<UniformWindow> window09;
[[vk::binding(16,0)]] ConstantBuffer<UniformWindow> window10;
[[vk::binding(17,0)]] ConstantBuffer<UniformWindow> window11;
[[vk::binding(18,0)]] ConstantBuffer<UniformWindow> window12;
[[vk::binding(19,0)]] ConstantBuffer<UniformWindow> window13;
[[vk::binding(20,0)]] ConstantBuffer<UniformWindow> window14;
[[vk::binding(21,0)]] ConstantBuffer<UniformWindow> window15;
[[vk::binding(22,0)]] ConstantBuffer<UniformWindow> window16;
[[vk::binding(23,0)]] ConstantBuffer<UniformWindow> window17;
[[vk::binding(24,0)]] ConstantBuffer<UniformWindow> window18;
[[vk::binding(25,0)]] ConstantBuffer<UniformWindow> window19;
[[vk::binding(26,0)]] ConstantBuffer<UniformWindow> window20;
[[vk::binding(27,0)]] ConstantBuffer<UniformWindow> window21;
[[vk::binding(28,0)]] ConstantBuffer<UniformWindow> window22;
[[vk::binding(29,0)]] ConstantBuffer<UniformWindow> window23;
[[vk::binding(30,0)]] ConstantBuffer<UniformWindow> window24;
[[vk::binding(31,0)]] ConstantBuffer<UniformWindow> window25;
[[vk::binding(32,0)]] ConstantBuffer<UniformWindow> window26;
[[vk::binding(33,0)]] ConstantBuffer<UniformWindow> window27;
[[vk::binding(34,0)]] ConstantBuffer<UniformWindow> window28;
[[vk::binding(35,0)]] ConstantBuffer<UniformWindow> window29;
[[vk::binding(36,0)]] ConstantBuffer<UniformWindow> window30;
[[vk::binding(37,0)]] ConstantBuffer<UniformWindow> window31;
[[vk::binding(38,0)]] ConstantBuffer<UniformWindow> window32;
[[vk::binding(39,0)]] ConstantBuffer<UniformWindow> window33;
[[vk::binding(40,0)]] ConstantBuffer<UniformWindow> window34;
[[vk::binding(41,0)]] ConstantBuffer<UniformWindow> window35;
[[vk::binding(42,0)]] ConstantBuffer<UniformWindow> window36;
[[vk::binding(43,0)]] ConstantBuffer<UniformWindow> window37;
[[vk::binding(44,0)]] ConstantBuffer<UniformWindow> window38;
[[vk::binding(45,0)]] ConstantBuffer<UniformWindow> window39;
[[vk::binding(46,0)]] ConstantBuffer<UniformWindow> window40;
[[vk::binding(47,0)]] ConstantBuffer<UniformWindow> window41;
[[vk::binding(48,0)]] ConstantBuffer<UniformWindow> window42;
[[vk::binding(49,0)]] ConstantBuffer<UniformWindow> window43;
[[vk::binding(50,0)]] ConstantBuffer<UniformWindow> window44;
[[vk::binding(51,0)]] ConstantBuffer<UniformWindow> window45;
[[vk::binding(52,0)]] ConstantBuffer<UniformWindow> window46;
[[vk::binding(53,0)]] ConstantBuffer<UniformWindow> window47;
[[vk::binding(54,0)]] ConstantBuffer<UniformWindow> window48;
[[vk::binding(55,0)]] ConstantBuffer<UniformWindow> window49;
[[vk::binding(56,0)]] ConstantBuffer<UniformWindow> window50;
[[vk::binding(57,0)]] ConstantBuffer<UniformWindow> window51;
[[vk::binding(58,0)]] ConstantBuffer<UniformWindow> window52;
[[vk::binding(59,0)]] ConstantBuffer<UniformWindow> window53;
[[vk::binding(60,0)]] ConstantBuffer<UniformWindow> window54;
[[vk::binding(61,0)]] ConstantBuffer<UniformWindow> window55;
[[vk::binding(62,0)]] ConstantBuffer<UniformWindow> window56;
[[vk::binding(63,0)]] ConstantBuffer<UniformWindow> window57;
[[vk::binding(64,0)]] ConstantBuffer<UniformWindow> window58;
[[vk::binding(65,0)]] ConstantBuffer<UniformWindow> window59;
[[vk::binding(66,0)]] ConstantBuffer<UniformWindow> window60;
[[vk::binding(67,0)]] ConstantBuffer<UniformWindow> window61;
[[vk::binding(68,0)]] ConstantBuffer<UniformWindow> window62;
[[vk::binding(69,0)]] ConstantBuffer<UniformWindow> window63;

uint arena_word(uint byteOffset) {
    const uint row=(byteOffset&65535u)>>4,component=(byteOffset>>2)&3u;
    switch(byteOffset>>16) {
    case 0u: return asuint(window00.rows[row])[component];
    case 1u: return asuint(window01.rows[row])[component];
    case 2u: return asuint(window02.rows[row])[component];
    case 3u: return asuint(window03.rows[row])[component];
    case 4u: return asuint(window04.rows[row])[component];
    case 5u: return asuint(window05.rows[row])[component];
    case 6u: return asuint(window06.rows[row])[component];
    case 7u: return asuint(window07.rows[row])[component];
    case 8u: return asuint(window08.rows[row])[component];
    case 9u: return asuint(window09.rows[row])[component];
    case 10u: return asuint(window10.rows[row])[component];
    case 11u: return asuint(window11.rows[row])[component];
    case 12u: return asuint(window12.rows[row])[component];
    case 13u: return asuint(window13.rows[row])[component];
    case 14u: return asuint(window14.rows[row])[component];
    case 15u: return asuint(window15.rows[row])[component];
    case 16u: return asuint(window16.rows[row])[component];
    case 17u: return asuint(window17.rows[row])[component];
    case 18u: return asuint(window18.rows[row])[component];
    case 19u: return asuint(window19.rows[row])[component];
    case 20u: return asuint(window20.rows[row])[component];
    case 21u: return asuint(window21.rows[row])[component];
    case 22u: return asuint(window22.rows[row])[component];
    case 23u: return asuint(window23.rows[row])[component];
    case 24u: return asuint(window24.rows[row])[component];
    case 25u: return asuint(window25.rows[row])[component];
    case 26u: return asuint(window26.rows[row])[component];
    case 27u: return asuint(window27.rows[row])[component];
    case 28u: return asuint(window28.rows[row])[component];
    case 29u: return asuint(window29.rows[row])[component];
    case 30u: return asuint(window30.rows[row])[component];
    case 31u: return asuint(window31.rows[row])[component];
    case 32u: return asuint(window32.rows[row])[component];
    case 33u: return asuint(window33.rows[row])[component];
    case 34u: return asuint(window34.rows[row])[component];
    case 35u: return asuint(window35.rows[row])[component];
    case 36u: return asuint(window36.rows[row])[component];
    case 37u: return asuint(window37.rows[row])[component];
    case 38u: return asuint(window38.rows[row])[component];
    case 39u: return asuint(window39.rows[row])[component];
    case 40u: return asuint(window40.rows[row])[component];
    case 41u: return asuint(window41.rows[row])[component];
    case 42u: return asuint(window42.rows[row])[component];
    case 43u: return asuint(window43.rows[row])[component];
    case 44u: return asuint(window44.rows[row])[component];
    case 45u: return asuint(window45.rows[row])[component];
    case 46u: return asuint(window46.rows[row])[component];
    case 47u: return asuint(window47.rows[row])[component];
    case 48u: return asuint(window48.rows[row])[component];
    case 49u: return asuint(window49.rows[row])[component];
    case 50u: return asuint(window50.rows[row])[component];
    case 51u: return asuint(window51.rows[row])[component];
    case 52u: return asuint(window52.rows[row])[component];
    case 53u: return asuint(window53.rows[row])[component];
    case 54u: return asuint(window54.rows[row])[component];
    case 55u: return asuint(window55.rows[row])[component];
    case 56u: return asuint(window56.rows[row])[component];
    case 57u: return asuint(window57.rows[row])[component];
    case 58u: return asuint(window58.rows[row])[component];
    case 59u: return asuint(window59.rows[row])[component];
    case 60u: return asuint(window60.rows[row])[component];
    case 61u: return asuint(window61.rows[row])[component];
    case 62u: return asuint(window62.rows[row])[component];
    case 63u: return asuint(window63.rows[row])[component];
    default: return 0u;
    }
}
uint3 arena_uint3(uint offset) { return uint3(arena_word(offset),arena_word(offset+4u),arena_word(offset+8u)); }
float3 arena_float3(uint offset) { return asfloat(arena_uint3(offset)); }
uint4 arena_uint4(uint offset) { return uint4(arena_word(offset),arena_word(offset+4u),arena_word(offset+8u),arena_word(offset+12u)); }
bool fits(uint offset,uint length,uint bytes) { return offset<=bytes && length<=bytes-offset; }

// Caller bounds every referenced BLEND position and |UV|<=16384, and proves
// a full standard-depth viewport. Selected arena bytes are finalized/available
// before submit and stable through this early read, as for the camera slices.
// Owned packed-source snapshots bound radiance by65536 and visibility by1.
bool prepare_oit_constants(float4 matrix[4],out float values[10]) {
    [unroll] for(uint i=0u;i<10u;++i)values[i]=0;
    // Output-only checkpoints in owned [256,384), after the caller has proved
    // the complete mixed output size and original-use witness. These bytes
    // never participate in the permit or any rendering decision.
    [unroll] for(uint byte=256u;byte<384u;byte+=16u)preparedFrame.Store4(byte,uint4(0,0,0,0));
    preparedFrame.Store4(256u,uint4(0x4F495431u,1u,0u,0u));
    uint diagnosticMask=0u;
    if(control.blend.w!=0u||((control.blend.y|control.blend.z)&15u)!=0u||
       !fits(control.blend.y,2320u,control.offsets.w)||!fits(control.blend.z,96u,control.offsets.w)||
       control.extent.x>16384u||control.extent.y>16384u)return false;
    preparedFrame.Store(260u,2u);
    if(!all(isfinite(control.blendMin))||!all(isfinite(control.blendMax))||
       !all(isfinite(control.blendBaseMax))||!all(isfinite(control.blendEmissiveMax))||
       any(control.blendMin.xyz>control.blendMax.xyz)||control.blendMin.w!=0||control.blendMax.w!=0||
       any(control.blendBaseMax.rgb<0)||control.blendBaseMax.w!=1||
       any(control.blendEmissiveMax.rgb<0)||control.blendEmissiveMax.w!=0)return false;
    preparedFrame.Store(260u,3u);
    float4 clipMin,clipMax;
    [unroll] for(uint row=0u;row<4u;++row){
        precise float3 a=matrix[row].xyz*control.blendMin.xyz;
        precise float3 b=matrix[row].xyz*control.blendMax.xyz;
        precise float lo=dot(min(a,b),float3(1,1,1))+matrix[row].w;
        precise float hi=dot(max(a,b),float3(1,1,1))+matrix[row].w;
        const float magnitude=dot(max(abs(a),abs(b)),float3(1,1,1))+abs(matrix[row].w);
        if(!isfinite(lo)||!isfinite(hi)||!isfinite(magnitude)||magnitude>1e20f)return false;
        // Wide outward margin relative to the bounded normal FP32 row dots.
        const float slack=magnitude*(1.f/4096.f)+1e-6f;
        clipMin[row]=lo-slack;clipMax[row]=hi+slack;
    }
    const float wMin=clipMin.w*(1.f-1.f/1024.f),wMax=clipMax.w*(1.f+1.f/1024.f);
    preparedFrame.Store2(312u,asuint(float2(wMin,wMax)));
    diagnosticMask|=2u;preparedFrame.Store(264u,diagnosticMask);
    preparedFrame.Store(260u,4u);
    // Keep reciprocal, interpolation and log domains far from denormals and
    // float overflow. Bound spread as well as each endpoint.
    if(wMin<1.f/1024.f||wMax>1048576.f||wMin>wMax||wMax/wMin>4096.f)return false;
    preparedFrame.Store(260u,5u);
    const uint n=control.blend.y,f=control.blend.z;
    const float3 sun=arena_float3(n+1776u),mapping=arena_float3(n+1824u);
    const float sunFactor=asfloat(arena_word(n+1952u)),exposure=asfloat(arena_word(n+1968u));
    const float2 inv=asfloat(uint2(arena_word(f+72u),arena_word(f+76u)));
    preparedFrame.Store3(272u,asuint(sun));preparedFrame.Store3(284u,asuint(mapping));
    preparedFrame.Store4(296u,asuint(float4(sunFactor,exposure,inv)));
    diagnosticMask|=1u;preparedFrame.Store(264u,diagnosticMask);
    if(!all(isfinite(sun))||!all(isfinite(mapping))||!isfinite(sunFactor)||!isfinite(exposure)||
       !all(isfinite(inv))||any(inv<=0))return false;
    preparedFrame.Store(260u,6u);
    const float2 expected=1.f/float2(control.extent.xy);
    if(any(abs(inv-expected)>expected*1e-5f))return false;
    preparedFrame.Store(260u,7u);
    precise float2 domain=float2(wMin,wMax)*mapping.x+mapping.y;
    const float domainMagnitude=abs(mapping.x)*wMax+abs(mapping.y);
    preparedFrame.Store2(320u,asuint(domain));
    diagnosticMask|=4u;preparedFrame.Store(264u,diagnosticMask);
    if(!all(isfinite(domain))||!isfinite(domainMagnitude))return false;
    const float domainSlack=domainMagnitude*(1.f/4096.f)+1e-6f;
    const float2 domainBounds=float2(min(domain.x,domain.y)-domainSlack,max(domain.x,domain.y)+domainSlack);
    preparedFrame.Store2(320u,asuint(domainBounds));
    preparedFrame.Store(260u,8u);
    if(domainBounds.x<1.f/1024.f||domainBounds.y>1048576.f)return false;
    preparedFrame.Store(260u,9u);
    const float2 logarithm=log2(domainBounds)*mapping.z;
    if(!all(isfinite(logarithm))||any(abs(logarithm)>1048576.f))return false;
    preparedFrame.Store(260u,10u);
    const float3 light=65536.f+abs(sun)*(abs(sunFactor)/(4.f*3.14159265358979323846f));
    const float3 beforeExposure=control.blendBaseMax.rgb*light+control.blendEmissiveMax.rgb;
    const float3 radiance=beforeExposure*abs(exposure);
    preparedFrame.Store3(328u,asuint(radiance));
    diagnosticMask|=8u;preparedFrame.Store(264u,diagnosticMask);
    if(!all(isfinite(light))||!all(isfinite(beforeExposure))||!all(isfinite(radiance))||
       any(light>1e30f)||any(beforeExposure>1e30f)||any(radiance>1e30f)||
       any(min(radiance,evrm_native_oit_radiance_limit)*.73f>65000.f))return false;
    preparedFrame.Store(260u,11u);
    values[0]=sun.x;values[1]=sun.y;values[2]=sun.z;
    values[3]=mapping.x;values[4]=mapping.y;values[5]=mapping.z;
    values[6]=sunFactor;values[7]=exposure;values[8]=inv.x;values[9]=inv.y;
    return true;
}

bool previous_uv_rows(out float4 rows[4]) {
    [unroll] for(uint r=0u;r<4u;++r)rows[r]=0;
    // No read at all of the fallback alias when history was not admitted.
    if(control.history.x!=1u||any(control.history.yzw!=0u))return false;
    uint bytes;previousFrame.GetDimensions(bytes);if(bytes<208u)return false;
    const uint4 previousControl=previousFrame.Load4(192u);
    if(previousControl.x!=1u||previousControl.y>1u||any(previousControl.zw!=0u))return false;
    const float4 screen=asfloat(previousFrame.Load4(176u));
    const float2 expected=1.f/float2(control.extent.xy);
    if(!all(isfinite(screen))||any(abs(screen.xy-expected)>expected*1e-5f))return false;
    float4 previous[4];
    [unroll] for(uint r=0u;r<4u;++r){previous[r]=asfloat(previousFrame.Load4(r*16u));if(!all(isfinite(previous[r])))return false;}
    // Native raster flips clip Y. The fragment subtracts raster UV + current
    // jitter, so the old sample must use the old raster UV + old jitter too.
    rows[0]=.5f*previous[0]+(.5f+screen.z)*previous[3];
    rows[1]=-.5f*previous[1]+(.5f+screen.w)*previous[3];
    rows[2]=previous[2];rows[3]=previous[3];
    [unroll] for(uint r=0u;r<4u;++r)if(!all(isfinite(rows[r])))return false;
    return true;
}

#ifdef EVRM_NATIVE_MENU_PREVIEW
void menu_checkpoint(uint phase){
    uint bytes;preparedFrame.GetDimensions(bytes);
    if(bytes>=240u)preparedFrame.Store4(224u,uint4(0x4D454E55u,phase,0u,0u));
}
#define EVRM_MENU_STAGE(n) menu_checkpoint(n)
#else
#define EVRM_MENU_STAGE(n)
#endif
[numthreads(1,1,1)]
void cs_prepare() {
    uint outputBytes; preparedFrame.GetDimensions(outputBytes);
    if(outputBytes<208u)return;
    // Never retain a previous commit or invent motion history on rejection.
    [unroll] for(uint offset=0u;offset<208u;offset+=16u)preparedFrame.Store4(offset,uint4(0,0,0,0));
    if(outputBytes<224u)return;
    preparedFrame.Store4(208u,uint4(0,0,0,0));
    EVRM_MENU_STAGE(1u);
    if(control.permit.x!=1u)return;
    if(control.blend.x>1u||(control.blend.x==1u&&outputBytes<131168u))return;
    const uint partCount=control.permit.y,recordCount=control.permit.z,regionCount=control.permit.w;
    if(partCount==0u||partCount>64u||recordCount==0u||recordCount>131072u||regionCount==0u||regionCount>32u)return;
    if(control.extent.x==0u||control.extent.y==0u||control.extent.z!=0u||control.extent.w!=0u)return;
    if(!all(isfinite(control.config))||control.config.x<0.1f||control.config.x>10.f||
       (control.config.y!=1.f&&control.config.y!=-1.f)||control.config.w!=0.f)return;
    const uint v=control.offsets.x,g=control.offsets.y,l=control.offsets.z,arenaBytes=control.offsets.w;
    if(arenaBytes==0u||arenaBytes>4194304u||((v|g|l|arenaBytes)&15u)!=0u||
       !fits(v,480u,arenaBytes)||!fits(g,1024u,arenaBytes)||!fits(l,2048u,arenaBytes))return;
    EVRM_MENU_STAGE(2u);
    if(control.shadow.x>1u||control.shadow.y>32u||any(control.shadow.zw!=0u)||
       (control.shadow.x==0u&&control.shadow.y!=0u))return;
    [loop] for(uint view=0u;view<control.shadow.y;++view){
        const uint4 selected=control.shadowViews[view];
        if((selected.x&15u)!=0u||any(selected.yzw!=0u)||!fits(selected.x,352u,arenaBytes))return;
        [unroll] for(uint row=0u;row<4u;++row)
            if(!all(isfinite(asfloat(arena_uint4(selected.x+272u+row*16u)))))return;
    }
    // The first original counted call must invoke its exact native vertex
    // shader before borrowed partial descriptors may be accessed. These two
    // argument buffers are direct references of that original command.
    EVRM_MENU_STAGE(3u);
    uint indirectBytes,countBytes;
    indirectBuffer.GetDimensions(indirectBytes);countBuffer.GetDimensions(countBytes);
    const uint4 firstInfo=control.regions[0];
    if(firstInfo.z==0u||firstInfo.z>1024u||firstInfo.w!=24u||((firstInfo.x|firstInfo.y)&3u)!=0u||
       !fits(firstInfo.y,4u,countBytes)||firstInfo.x>indirectBytes)return;
    const uint firstDrawCount=countBuffer.Load(firstInfo.y);
    if(firstDrawCount>firstInfo.z||
       (firstDrawCount!=0u&&!fits(firstInfo.x,(firstDrawCount-1u)*24u+20u,indirectBytes)))return;
    bool nativeVertexUse=false;
    [loop] for(uint draw=0u;draw<firstDrawCount;++draw) {
        const uint commandOffset=firstInfo.x+draw*24u;
        if(indirectBuffer.Load(commandOffset)>=3u&&indirectBuffer.Load(commandOffset+4u)>0u)nativeVertexUse=true;
    }
    if(!nativeVertexUse)return;
    uint sourceBytes,culledBytes;
    sourceInstances.GetDimensions(sourceBytes);sourceCulled.GetDimensions(culledBytes);
    if(recordCount>sourceBytes/128u)return;
    EVRM_MENU_STAGE(4u);
    const uint3 origin=arena_uint3(v+304u);
    if(any(origin!=arena_uint3(g+816u)))return;
    float4 nativeRows[4];
    [unroll] for(uint r=0u;r<4u;++r) {
        const uint4 visBits=arena_uint4(v+336u+r*16u),gbufBits=arena_uint4(g+848u+r*16u);
        if(any(visBits!=gbufBits)||!all(isfinite(asfloat(visBits))))return;
        nativeRows[r]=asfloat(visBits);
    }
    EVRM_MENU_STAGE(5u);
    const float3 playerWorld=arena_float3(l+256u),cameraWorld=arena_float3(v+176u);
    const float2 jitter=asfloat(uint2(arena_word(g+792u),arena_word(g+796u)));
#ifdef EVRM_NATIVE_MENU_PREVIEW
    // Frontend LightingOwner carries the camera, not an ECS player anchor.
    // Host admission requires a separately guarded empty-world menu context.
    // Its camera position is in GBufferCamera1024. Visibility's legacy
    // position field is not the frontend camera-position contract.
    const float3 menuCameraWorld=arena_float3(g+176u);
    if(!all(isfinite(playerWorld))||!all(isfinite(menuCameraWorld))||!all(isfinite(jitter))||
       any(abs(playerWorld-menuCameraWorld)>0.001f))return;
#else
    if(!all(isfinite(playerWorld))||!all(isfinite(cameraWorld))||!all(isfinite(jitter))||
       all(abs(playerWorld-cameraWorld)<=0.001f))return;
#endif
    EVRM_MENU_STAGE(6u);
    uint3 root=uint3(0,0,0);uint4 quaternionBits=uint4(0,0,0,0);uint scaleBits=0u;
    [loop] for(uint part=0u;part<partCount;++part) {
        const uint4 expected=control.parts[part];const uint id=expected.x;
        if(id>=recordCount)return;
        [loop] for(uint earlier=0u;earlier<part;++earlier)if(control.parts[earlier].x==id)return;
        const uint base=id*128u;
        if((sourceInstances.Load(base+44u)&1u)!=0u||sourceInstances.Load(base+120u)!=expected.y||
           sourceInstances.Load(base+108u)!=expected.z||sourceInstances.Load(base+112u)!=expected.w)return;
        const uint3 partRoot=sourceInstances.Load3(base);
        const uint partScale=sourceInstances.Load(base+12u);const uint4 partQuat=sourceInstances.Load4(base+16u);
        const float scale=asfloat(partScale);const float4 q=asfloat(partQuat);const float norm=dot(q,q);
        if(!isfinite(scale)||scale<=0.f||!all(isfinite(q))||!isfinite(norm)||abs(norm-1.f)>0.01f)return;
        if(part==0u){root=partRoot;scaleBits=partScale;quaternionBits=partQuat;}
        else if(any(root!=partRoot)||scaleBits!=partScale||any(quaternionBits!=partQuat))return;
    }
    EVRM_MENU_STAGE(7u);
    // Packed coordinates wrap every32768 world units. Expected IDs still
    // come only from the host's bounded identity/provenance list.
#ifdef EVRM_NATIVE_MENU_PREVIEW
    // This pinned frontend preview is rooted at the menu origin. Nonzero
    // actors cannot use this pipeline even with otherwise matching stamps.
    if(any(root!=uint3(0,0,0)))return;
#else
    float3 qualifierDelta=float3(root)*(1.f/131072.f)-playerWorld;
    qualifierDelta-=round(qualifierDelta/32768.f)*32768.f;
    if(!all(isfinite(qualifierDelta))||any(abs(qualifierDelta)>0.001f))return;
#endif
    EVRM_MENU_STAGE(8u);
    bool member=false;uint scanned=0u;
    [loop] for(uint region=0u;region<regionCount;++region) {
        const uint4 info=control.regions[region];
        if(info.z==0u||info.z>1024u||info.w!=24u||((info.x|info.y)&3u)!=0u||
           !fits(info.y,4u,countBytes)||info.x>indirectBytes)return;
        const uint drawCount=countBuffer.Load(info.y);
        if(drawCount>info.z)return;
        if(drawCount!=0u&&!fits(info.x,(drawCount-1u)*24u+20u,indirectBytes))return;
        [loop] for(uint draw=0u;draw<drawCount;++draw) {
            const uint commandOffset=info.x+draw*24u;
            const uint count=indirectBuffer.Load(commandOffset+4u),first=indirectBuffer.Load(commandOffset+16u);
            if(count>262144u-scanned||first>culledBytes/8u||count>culledBytes/8u-first)return;
            scanned+=count;
            if(indirectBuffer.Load(commandOffset)==0u)continue;
            [loop] for(uint instance=0u;instance<count;++instance) {
                const uint id=sourceCulled.Load((first+instance)*8u+4u);
                if(id>=recordCount)return;
                [loop] for(uint part=0u;part<partCount;++part)if(id==control.parts[part].x)member=true;
            }
        }
    }
    if(!member)return;
    EVRM_MENU_STAGE(9u);
    float4 q=asfloat(quaternionBits);q*=rsqrt(dot(q,q));
    const float x=q.x,y=q.y,z=q.z,w=q.w,basis=control.config.y;
    float3 normalRows[3];
    normalRows[0]=float3((1.f-2.f*(y*y+z*z))*basis,2.f*(x*y-z*w),2.f*(x*z+y*w)*basis);
    normalRows[1]=float3(2.f*(x*y+z*w)*basis,1.f-2.f*(x*x+z*z),2.f*(y*z-x*w)*basis);
    normalRows[2]=float3(2.f*(x*z-y*w)*basis,2.f*(y*z+x*w),(1.f-2.f*(x*x+y*y))*basis);
    const float scale=asfloat(scaleBits)*control.config.x;
    const float3 relative=float3(asint(root-origin))*(1.f/131072.f)+float3(control.config.z,0,0);
    if(!isfinite(scale)||!all(isfinite(relative)))return;
    const float4 actorRows[4]={
        float4(normalRows[0]*scale,relative.x),float4(normalRows[1]*scale,relative.y),
        float4(normalRows[2]*scale,relative.z),float4(0,0,0,1)
    };
    float4 currentRows[4];
    [unroll] for(uint row=0u;row<4u;++row) {
        const float4 n=nativeRows[row];
        currentRows[row]=n.x*actorRows[0]+n.y*actorRows[1]+n.z*actorRows[2]+n.w*actorRows[3];
        if(!all(isfinite(currentRows[row])))return;
    }
    EVRM_MENU_STAGE(10u);
    float oitValues[10];
    if(control.blend.x==1u&&!prepare_oit_constants(currentRows,oitValues))return;
    float4 previousUv[4];const bool history=previous_uv_rows(previousUv);
    [unroll] for(uint row=0u;row<4u;++row)preparedFrame.Store4(row*16u,asuint(currentRows[row]));
    [unroll] for(uint row=0u;row<3u;++row)preparedFrame.Store4(128u+row*16u,asuint(float4(normalRows[row],0)));
    preparedFrame.Store4(176u,asuint(float4(1.f/float(control.extent.x),1.f/float(control.extent.y),jitter)));
    preparedFrame.Store4(208u,uint4(root,asuint(scale)));
    if(control.blend.x==1u){
        preparedFrame.Store3(65536u+1776u,asuint(float3(oitValues[0],oitValues[1],oitValues[2])));
        preparedFrame.Store3(65536u+1824u,asuint(float3(oitValues[3],oitValues[4],oitValues[5])));
        preparedFrame.Store(65536u+1952u,asuint(oitValues[6]));preparedFrame.Store(65536u+1968u,asuint(oitValues[7]));
        preparedFrame.Store2(131072u+72u,asuint(float2(oitValues[8],oitValues[9])));
    }
    if(history)[unroll] for(uint row=0u;row<4u;++row)preparedFrame.Store4(64u+row*16u,asuint(previousUv[row]));
    // A rejected previous frame changes only motion, never current admission.
    EVRM_MENU_STAGE(11u);
    preparedFrame.Store4(192u,uint4(1,history?1u:0u,0,0));
}

