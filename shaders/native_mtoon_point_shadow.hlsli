#ifndef EVRM_NATIVE_MTOON_POINT_SHADOW
#define EVRM_NATIVE_MTOON_POINT_SHADOW
#include "native_mtoon_point_lights.hlsli"
// Factual native set1:0 scalar ABI. The nested atlas settings start at16,
// with tile element at32 and border at40. Other fields remain opaque words.
struct NativeMToonPointShadowParameters {uint4 words[6];};
// Native2048-entry uint2 dictionary: integer ID hash, linear wrap, at most32
// probes. Empty keys and explicit unmapped values use0xffffffff. These are
// independently expressed format facts; no native shader payload is used.
uint native_mtoon_shadow_hash(uint id){
    uint value=(id^(id>>16u))*2246822507u;
    value=(value^(value>>13u))*3266489909u;
    return (value^(value>>16u))&2047u;
}
bool native_mtoon_shadow_index(ByteAddressBuffer mapping,uint id,out uint index){
    index=0xffffffffu;uint bytes;mapping.GetDimensions(bytes);if(bytes<16384u)return false;
    uint slot=native_mtoon_shadow_hash(id);
    [loop]for(uint probe=0u;probe<32u;++probe){
        const uint2 record=mapping.Load2(slot*8u);
        if(record.x==id){index=record.y;return true;}
        if(record.x==0xffffffffu)return true;
        slot=(slot+1u)&2047u;
    }
    return true; // The original native bounded search treats this as unmapped.
}
// Apply only to the same-frame point light/position/normal admitted by the
// parent. Buffer mask0/1 is native many-lights/map; image/sampler0 is its exact
// EVSM atlas. Missing active sources are not an unshadowed-light certificate.
// Initialization, sampled tile/range, layout/use, queue and execution lifetime
// remain mandatory caller proofs. No permission follows from this function.
bool native_mtoon_point_shadow(NativeMToonPointShadowParameters parameters,ByteAddressBuffer mapping,
    Texture2D<float4> atlas,SamplerState atlasSampler,NativeMToonPointLight light,float3 position,float3 normal,
    uint buffers,uint images,uint samplers,out float visibility,bool filterEdges=true){
    visibility=0;
    if((buffers&~127u)!=0u||(images&~16383u)!=0u||(samplers&~127u)!=0u||(light.flags&~3u)!=0u)return false;
    if((light.flags&1u)==0u){visibility=1;return true;}
    if((buffers&1u)==0u)return false;
    if(parameters.words[0].x==0u){visibility=1;return true;}
    if((buffers&2u)==0u)return false;
    uint index;if(!native_mtoon_shadow_index(mapping,light.id,index))return false;
    if(index==0xffffffffu){visibility=1;return true;}
    if((images&1u)==0u||(samplers&1u)==0u)return false;
    const uint2 count=parameters.words[1].xy;const uint element=parameters.words[2].x,border=parameters.words[2].z;
    if(any(count==0u)||element==0u||element>16384u)return false;
    if(any(count>16384u/element)||count.x>65536u/count.y||index>=count.x*count.y)return false;
    uint width,height;atlas.GetDimensions(width,height);if(any(uint2(width,height)!=count*element))return false;
    const float normalBias=asfloat(parameters.words[4].x),depthBias=asfloat(parameters.words[4].y);
    float2 uv;float receiver;
    if(!native_mtoon_shadow_receiver(position,normal,light.position,light.position-position,count,element,border,index,normalBias,depthBias,uv,receiver))return false;
    const float4 moments=atlas.SampleLevel(atlasSampler,uv,0);
    const float2 exponents=asfloat(parameters.words[3].xy);const float minimum=asfloat(parameters.words[3].z),bleed=asfloat(parameters.words[3].w);
    float value;if(!native_mtoon_evsm(moments,exponents,minimum,bleed,receiver,value))return false;
#if EVRM_GAME_LOOK
    if(filterEdges&&border>0u&&element>=4u){
        const float2 dimensions=float2(width,height);
        const uint2 tile=uint2(index%count.x,index/count.x);
        const float2 low=(float2(tile*element)+1.f)/dimensions;
        const float2 high=(float2((tile+1u)*element)-1.f)/dimensions;
        const float2 offsets[4]={float2(-.75f,0),float2(.75f,0),float2(0,-.75f),float2(0,.75f)};
        float delta=0.f;
        [unroll]for(uint tap=0u;tap<4u;++tap){
            const float2 coordinate=clamp(uv+offsets[tap]/dimensions,low,high);
            float neighbour;
            if(!native_mtoon_evsm(atlas.SampleLevel(atlasSampler,coordinate,0),exponents,minimum,bleed,receiver,neighbour))return false;
            delta+=neighbour-value;
        }
        value=saturate(value+delta*.125f);
    }
#endif
    visibility=value;return true;
}
#endif
