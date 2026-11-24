// Original bounded material/geometry/native-source -> owned packet producer.
// There are no native destination writes here. Parent admission, initialization,
// resource use/layout/queue/lifetime and original submit scope remain mandatory.
#ifndef EVRM_GAME_LOOK
#define EVRM_GAME_LOOK 1
#endif
#ifndef EVRM_NATIVE_MTOON_LIGHT_LIMIT
#define EVRM_NATIVE_MTOON_LIGHT_LIMIT 1
#endif
#ifndef EVRM_NATIVE_MTOON_SOFTEN_AMBIENT
#define EVRM_NATIVE_MTOON_SOFTEN_AMBIENT 1
#endif
// The main light and the additional lights share one budget of visible light,
// see native_mtoon_composition.hlsli. Opaque shading, transparent prepare and
// transparent finish are all compiled from this file; the setting must be the
// same for all three.
#ifndef EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
#define EVRM_NATIVE_MTOON_DIFFUSE_BUDGET 1
#endif
#define EVRM_MTOON_DESCRIPTOR_SET 0
#include "native_mtoon_reconstruct_outline.hlsli"
#include "native_mtoon_scene_water.hlsli"
#include "native_mtoon_scene_samples.hlsli"
#include "native_mtoon_scene_lighting.hlsli"
#include "native_mtoon_point_shadow.hlsli"
#include "native_mtoon_game_shadows.hlsli"
#include "native_mtoon_overlay_shadow.hlsli"
#include "native_mtoon_composite.hlsli"
struct ShadeControl {uint4 domain,grants,draw,extra;};
[[vk::binding(0,1)]] ConstantBuffer<ShadeControl> control;
[[vk::binding(1,1)]] ConstantBuffer<NativeMToonRasterUniform> raster;
[[vk::binding(2,1)]] ConstantBuffer<NativeMToonPreparedFrame> frame;
[[vk::binding(3,1)]] ByteAddressBuffer vertices;
[[vk::binding(4,1)]] ByteAddressBuffer indices;
[[vk::binding(5,1)]] ByteAddressBuffer surfaceVertices;
[[vk::binding(6,1)]] ByteAddressBuffer rawUvs;
[[vk::binding(7,1)]] ByteAddressBuffer legacy;
[[vk::binding(8,1)]] ByteAddressBuffer batches;
[[vk::binding(9,1)]] ByteAddressBuffer identities;
[[vk::binding(10,1)]] RWByteAddressBuffer packets;
[[vk::binding(11,1)]] ByteAddressBuffer work;
[[vk::binding(12,1)]] ByteAddressBuffer summary;
[[vk::binding(13,1)]] ByteAddressBuffer probes;
// Set2 is a private partially-bound snapshot of exact admitted native objects.
// It is immutable after binding. Whole submission withdrawal precedes every
// borrowed resource access; absent optional inputs cannot synthesize readiness.
[[vk::binding(0,2)]] ConstantBuffer<NativeMToonSceneLightParameters> scene;
[[vk::binding(1,2)]] ConstantBuffer<NativeMToonClusterParameters> cluster;
[[vk::binding(2,2)]] ConstantBuffer<NativeMToonPointShadowParameters> pointShadow;
[[vk::binding(3,2)]] ConstantBuffer<NativeMToonWaterParameters> waterParameters;
[[vk::binding(4,2)]] ByteAddressBuffer cells;
[[vk::binding(5,2)]] ByteAddressBuffer visible;
[[vk::binding(6,2)]] ByteAddressBuffer lights;
[[vk::binding(7,2)]] ByteAddressBuffer shadowMapping;
[[vk::binding(8,2)]] Texture2D<float> nativeDepth;
[[vk::binding(9,2)]] Texture2D<float4> atmosphere;
[[vk::binding(10,2)]] Texture3D<float> fog;
[[vk::binding(11,2)]] TextureCube<float4> environment;
[[vk::binding(12,2)]] Texture2D<float> sunMask;
[[vk::binding(13,2)]] Texture2D<float> ssao;
[[vk::binding(14,2)]] Texture2D<float4> shadowAtlas;
[[vk::binding(15,2)]] Texture2D<float4> waterSurface;
[[vk::binding(16,2)]] Texture2D<float> waterDepth;
[[vk::binding(17,2)]] Texture2DArray<float> waterShadow;
[[vk::binding(18,2)]] Texture2D<float4> caustic;
[[vk::binding(19,2)]] SamplerState atmosphereSampler;
[[vk::binding(20,2)]] SamplerState fogSampler;
[[vk::binding(21,2)]] SamplerState environmentSampler;
[[vk::binding(22,2)]] SamplerState ssaoSampler;
[[vk::binding(23,2)]] SamplerState shadowSampler;
[[vk::binding(24,2)]] SamplerState waterShadowSampler;
[[vk::binding(25,2)]] SamplerState causticSampler;

bool shade_sun_edge(uint2 pixel,bool outline,inout NativeMToonScenePixelLight sampled){
#if EVRM_GAME_LOOK && !EVRM_MTOON_FORWARD
    if(!outline&&evrm_mtoon_nilltoon()&&asfloat(scene.words[0].x)>0.f&&any(asfloat(scene.words[111].xyz)>0.f)){
        // Global opaque ranks uniquely identify this draw across all actors.
        // Forward binding9 is a capture stream, not an opaque identity map;
        // its optional filter stays centre-only rather than guessing ownership.
        const uint admitted=evrm_game_sun_neighbours(identities,pixel,control.domain.xy,raster.domain.z,raster.domain.w);
        if(admitted==0u)return true;
        uint width,height;nativeDepth.GetDimensions(width,height);
        if(any(uint2(width,height)!=control.domain.xy))return true;
        const float depth=nativeDepth.Load(int3(pixel,0));NativeMToonScenePosition position;
        if(!native_mtoon_scene_position(scene,pixel,control.domain.xy,depth,position))return true;
        float filtered;
        if(!evrm_game_sun_shadow(scene,position,depth,pixel,control.domain.xy,nativeDepth,sunMask,sampled.sun_visibility,filtered,admitted))return false;
        sampled.sun_visibility=filtered;
    }
#endif
    return true;
}
bool shade_water(NativeMToonScenePosition position,uint2 pixel,out NativeMToonSceneModulation value,out uint detail,out uint facts){
    value=(NativeMToonSceneModulation)0;detail=facts=0;
    NativeMToonSceneModulation unit;unit.sunlight=1;unit.gi=1;unit.available=true;
    const uint flags=scene.words[118].w;const float4 gains=asfloat(scene.words[0]);
    const bool sun=gains.x>0.f&&any(asfloat(scene.words[111].xyz)!=0.f),gi=gains.z>0.f&&(flags&8u)!=0u;
    if((flags&64u)==0u||(!sun&&!gi)||scene.words[21].x==0u){value=unit;return true;}
    // Classify before passing the optional borrowed640-byte block. Its absent
    // descriptor is not accessed even when a proven inactive pixel needs units.
    if((control.extra.x&6u)!=6u){detail=1;return false;}
    float factor;if(!native_mtoon_water_factor_diagnostic(scene,position,pixel,waterSurface,waterDepth,factor,detail,facts))return false;
    if(factor<=0.f){value=unit;return true;}
    if(control.extra.x!=31u||control.extra.y!=1u||(control.grants.w&96u)!=96u){detail=7;facts|=32u;if(control.extra.y!=1u)facts|=64u;if((control.grants.w&96u)!=96u)facts|=128u;return false;}
    if(!native_mtoon_scene_water(scene,waterParameters,position,pixel,control.extra.x,control.grants.w,control.grants.z,true,
        nativeDepth,waterSurface,waterDepth,waterShadow,waterShadowSampler,caustic,causticSampler,value)){detail=8;return false;}return true;
}
bool shade_points(inout NativeMToonSceneRadiance state,uint2 pixel,float depth,float3 normal,out uint detail,out uint facts){
    detail=facts=0;if((state.flags&16u)==0u||state.point_gain==0.f)return true;
    if((control.grants.y&60u)!=60u){detail=1;return false;}
    NativeMToonClusterIterator iterator;
    if(!native_mtoon_cluster_begin(cluster,pixel,depth,cells,visible,lights,iterator)){
        detail=2;
        if(control.extra.z==1u){
            const uint3 counts=cluster.words[0].xyz;const float3 scale=asfloat(cluster.words[1].xyz);
            const uint stride=cluster.words[1].w;const float2 pixelScale=asfloat(cluster.words[3].xy),planes=asfloat(cluster.words[3].zw);
            if(any(counts==0u)||any(counts>4096u))facts|=1u;
            else if(counts.x>16777216u/counts.y||counts.x*counts.y>16777216u/counts.z)facts|=1u;
            if(any(!isfinite(scale))||any(scale<=0.f)||any(!isfinite(pixelScale))||any(pixelScale<=0.f))facts|=2u;
            if(!stride||(stride&3u)!=0u||stride>8192u)facts|=4u;
            if(any(!isfinite(planes))||any(planes<=0.f))facts|=8u;
            if(!isfinite(depth)||depth<0.f||depth>1.f)facts|=16u;
            const float numerator=(depth-1.f)*planes.x,denominator=numerator-planes.y*depth;
            if(!isfinite(numerator)||!isfinite(denominator)||denominator==0.f)facts|=32u;
            if(facts==0u){
                const float3 coordinate=float3((float(pixel.x)+.5f)*pixelScale.x,1.f-(float(pixel.y)+.5f)*pixelScale.y,numerator/denominator)*scale;
                if(any(!isfinite(coordinate)))facts|=1024u;
                uint a,v,l;cells.GetDimensions(a);visible.GetDimensions(v);lights.GetDimensions(l);
                if(a<4u||iterator.cell>(a-4u)/4u)facts|=64u;
                if(v<stride||iterator.cell>(v-stride)/stride)facts|=128u;
                if(iterator.end_word>=stride/4u)facts|=256u;
                if(l/52u<=iterator.last)facts|=512u;
            }
        }
        return false;
    }
    uint index;
    [loop]while(native_mtoon_cluster_next(iterator,visible,index)){
        NativeMToonPointLight light;NativeMToonPointResponse response;
        if(!native_mtoon_cluster_light(lights,index,light)){
            detail=3;
            if(control.extra.z==1u){uint bytes;lights.GetDimensions(bytes);
                if(index>=bytes/52u)facts=256u;
                else{const uint at=index*52u;const uint4 a=lights.Load4(at),b=lights.Load4(at+16u),c=lights.Load4(at+32u);
                    const float2 radius=f16tof32(uint2(a.w&65535u,a.w>>16u));
                    const float3 color=f16tof32(uint3(b.x>>16u,b.x&65535u,b.y>>16u));
                    const float3 position=asfloat(a.xyz)+f16tof32(uint3(b.y&65535u,b.z>>16u,b.z&65535u));
                    if(any(!isfinite(radius)))facts|=1u;if(any(radius<=0.f))facts|=2u;
                    if(any(!isfinite(color)))facts|=4u;if(any(color<0.f))facts|=8u;
                    if(any(!isfinite(position)))facts|=16u;if((c.w&~3u)!=0u)facts|=32u;
                    if((c.w&2u)!=0u){const float4 rotation=f16tof32(uint4(c.x&65535u,c.x>>16u,c.y&65535u,c.y>>16u));const float2 spot=f16tof32(uint2(c.z&65535u,c.z>>16u));
                        if(any(!isfinite(rotation))||abs(dot(rotation,rotation)-1.f)>.02f)facts|=64u;
                        if(any(!isfinite(spot))||spot.y<0.f)facts|=128u;}
                }
            }
            return false;
        }
        if(!native_mtoon_point_response(light,state.world_position,state.point_gain,1.f,response)){
            detail=4;
            if(control.extra.z==1u){
                facts=uint(light.filter!=0u);
                if(!isfinite(state.point_gain))facts|=2u;if(state.point_gain<0.f)facts|=4u;
                if(any(!isfinite(state.world_position)))facts|=8u;
                const float3 delta=light.position-state.world_position;if(!isfinite(dot(delta,delta)))facts|=16u;
                if(any(!isfinite(light.color*(state.point_gain/3.14159265358979323846f))))facts|=32u;
            }
            return false;
        }
        if(response.attenuation==0.f)continue;
        const float unshadowedAttenuation=response.attenuation;
        if((light.flags&1u)!=0u){
            if((control.grants.y&1u)==0u){detail=5;return false;}
            float visibility;
            if(!native_mtoon_point_shadow(pointShadow,shadowMapping,shadowAtlas,shadowSampler,light,state.world_position,normal,
                                         control.grants.y,control.grants.z,control.grants.w,visibility,evrm_mtoon_nilltoon())){detail=6;return false;}
            response.attenuation*=visibility;
        }
        if(!native_mtoon_add_scene_point_response(state,response,unshadowedAttenuation)){detail=7;return false;}
    }
    if(iterator.failed){detail=8;return false;}return true;
}
bool shade_emission(EvrmMToonSamples samples,bool outline,out float marker){
    marker=0;const float gain=asfloat(scene.words[0].w),scale=asfloat(scene.words[16].w);
    if(!isfinite(gain)||gain<0.f||!isfinite(scale)||scale<0.f)return false;
    float3 emission=mtoonMaterial.emission.rgb*samples.emission*gain;
    // Legacy reference outlines have no emission term. VRM1 outline colour is
    // a final tint/mix, so its actual emission contribution receives that mix.
    if(outline)emission=mtoonMaterial.modes.y==0u?float3(0,0,0):emission*mtoonMaterial.outline.rgb*mtoonMaterial.outline_settings.y;
    const float originalValue=dot(emission,float3(.2125f,.7154f,.0721f))*scale;
    if(any(!isfinite(emission))||any(emission<0.f)||!isfinite(originalValue)||originalValue<0.f)return false;
    const float exposure=asfloat(scene.words[1].x);
    if(!isfinite(exposure)||exposure<=0.f)return false;
#if EVRM_NATIVE_MTOON_LIGHT_LIMIT
    if(!outline)emission/=max(1.f,exposure); // same native unit conversion as the HDR contribution
#endif
    const float value=dot(emission,float3(.2125f,.7154f,.0721f))*scale;
    if(any(!isfinite(emission))||any(emission<0.f)||!isfinite(value)||value<0.f)return false;
    marker=value;return true;
}
// Opt-in evidence at the same fixed17x17 grid copied after original GPU
// retirement. Only a current owned identity's matching draw may write a
// marker. Tag2 cannot be consumed as colour by the native compositor. No
// borrowed object, texture content or native destination is written here.
void shade_decline(uint2 pixel,uint at,uint key,uint stage,uint detail=0,uint facts=0){
    if((control.extra.z&1u)==0u)return;
    if(control.draw.y!=1u){const uint2 cell=pixel*17u/control.domain.xy;
        if(any(pixel!=((cell*2u+1u)*control.domain.xy/34u)))return;}
    // Outlines may be thinner than one pixel. Keep their owned decline marker
    // at every surviving identity so dense readback can diagnose their colour.
    packets.Store4(16u+at*16u,uint4(detail,facts,0x02000000u|stage,key));
}
#if !EVRM_MTOON_FORWARD
[numthreads(8,8,1)]void cs_shade(uint3 id:SV_DispatchThreadID){
    // Only the always-retained owned control is consulted before withdrawal.
    if(control.grants.x!=1u||control.domain.z==0u||control.domain.w!=control.domain.z||
       any(control.domain.xy==0u)||any(control.domain.xy>16384u)||control.draw.x>=8192u||control.draw.y>1u||control.draw.z>2u||control.draw.w>2u||
       (control.grants.y&~127u)!=0u||(control.grants.z&~16383u)!=0u||(control.grants.w&~127u)!=0u||
       (control.extra.x&~31u)!=0u||control.extra.y>1u||(control.extra.z&~5u)!=0u||control.extra.w>4096u)return;
    if(control.domain.x>16777216u/control.domain.y)return;
    uint workBytes,summaryBytes;work.GetDimensions(workBytes);summary.GetDimensions(summaryBytes);
    if(control.draw.x>=workBytes/32u||summaryBytes<16u)return;
    const uint4 total=summary.Load4(0);
    if(total.x>262144u||any(total.yzw!=0u))return;
    const uint4 box=work.Load4(control.draw.x*32u);
    if(any(box.xy>box.zw)||any(box.zw>=control.domain.xy)||any(id.xy>box.zw-box.xy))return;
    const uint2 pixel=box.xy+id.xy;const uint pixels=control.domain.x*control.domain.y,at=pixel.y*control.domain.x+pixel.x;
    uint packetBytes,identityBytes;packets.GetDimensions(packetBytes);identities.GetDimensions(identityBytes);
    if(packetBytes<16u+pixels*16u||identityBytes<pixels*4u||any(packets.Load4(0)!=uint4(0x4d544350u,control.domain.z,control.domain.xy)))return;
    const uint key=identities.Load(at*4u);if((key>>1u)==0u)return;
    const uint ordinal=(key>>1u)-1u;
    if(ordinal<raster.domain.z||ordinal-raster.domain.z>=raster.domain.w)return;
    if(any(raster.domain.xy!=control.domain.xy)||frame.control.x!=1u||raster.uv[2].w!=1u||(mtoonMaterial.modes.z>1u||(mtoonMaterial.shade.w!=0.f&&mtoonMaterial.shade.w!=1.f))){shade_decline(pixel,at,key,1);return;}
    const bool outline=control.draw.y==1u;NativeMToonReconstructedSurface surface;
    bool reconstructed;
    if(outline)reconstructed=native_mtoon_reconstruct_outline_surface(vertices,indices,surfaceVertices,rawUvs,legacy,batches,frame,raster,float2(pixel)+.5f,key,control.draw.z,control.draw.w,true,surface);
    else reconstructed=native_mtoon_reconstruct_surface(vertices,indices,surfaceVertices,rawUvs,frame,raster,float2(pixel)+.5f,key,surface);
    if(!reconstructed){shade_decline(pixel,at,key,2);return;}
    if((control.grants.y&64u)==0u||(control.grants.z&1024u)==0u){shade_decline(pixel,at,key,3);return;}
    // First borrowed accesses: exact main scalar/depth inputs, after the whole
    // owned generation/geometry/selection/budget checks and their actual masks.
    if(any(scene.words[2].xy!=control.domain.xy)){shade_decline(pixel,at,key,4);return;}
    uint width,height;nativeDepth.GetDimensions(width,height);
    if(any(uint2(width,height)!=control.domain.xy)){shade_decline(pixel,at,key,5);return;}
    const float depth=nativeDepth.Load(int3(pixel,0));NativeMToonScenePosition position;
    if(!native_mtoon_scene_position(scene,pixel,control.domain.xy,depth,position)){shade_decline(pixel,at,key,6);return;}
    NativeMToonSceneModulation water;uint waterDetail,waterFacts;
    if(!shade_water(position,pixel,water,waterDetail,waterFacts)){shade_decline(pixel,at,key,7,waterDetail,waterFacts);return;}
    NativeMToonScenePixelLight sampled;
    // Both own native raster paths write material occlusion1 in GBuffer0.a.
    if(!native_mtoon_scene_samples(scene,position,surface.normal,1.f,pixel,control.grants.z,control.grants.w,water,
        atmosphere,atmosphereSampler,fog,fogSampler,environment,environmentSampler,sunMask,ssao,ssaoSampler,sampled)){shade_decline(pixel,at,key,8);return;}
    if(!shade_sun_edge(pixel,outline,sampled)){shade_decline(pixel,at,key,8);return;}
    // Screen outlines move clip XY, not the posed surface's reference view.
    // Native world coordinates still drive native light/probe/fog sampling.
    const float3 view=evrm_mtoon_unit(-surface.relative_position,0);if(all(view==0.f)){shade_decline(pixel,at,key,9);return;}
    const EvrmMToonSamples samples=evrm_mtoon_samples(surface.raw_uv,raster.time.x,surface.normal,view,surface.camera_up);
    NativeMToonSceneRadiance state;
    if(!native_mtoon_begin_scene_radiance(scene,probes,position.world,surface.normal,view,sampled,samples,outline,state,surface.normal_dx,surface.normal_dy)){shade_decline(pixel,at,key,10);return;}
    const uint numeric=0u;
    uint pointDetail,pointFacts;
    if(!shade_points(state,pixel,depth,surface.normal,pointDetail,pointFacts)){shade_decline(pixel,at,key,11,pointDetail,pointFacts);return;}
    // All point lights are known: fold them into the main result here, so the
    // numeric record below and the finish see the same final colour.
    native_mtoon_resolve_points(state.toon);
    float4 hdr;float brightness,emission;uint4 packet;
    if(!native_mtoon_finish_scene_radiance(state,hdr,brightness)){shade_decline(pixel,at,key,12);return;}
    if(!shade_emission(samples,outline,emission)){shade_decline(pixel,at,key,13);return;}
    if(!(control.extra.w?native_mtoon_pack_owned_composite(hdr,brightness,emission,key,control.extra.w,outline,outline?surface.outline_coverage:1.f,packet):native_mtoon_pack_composite(hdr,brightness,emission,key,packet))){shade_decline(pixel,at,key,14);return;}
    // One all-or-none owned colour result. The separately admitted compositor
    // alone updates exact native HDR/brightness/emission views; diagnostic
    // markers retain the native results for these pixels.
    packets.Store4(16u+at*16u,packet);
}

#else
#include "native_mtoon_forward_records.hlsli"
struct ForwardConstants {float4 words[145];};
[[vk::binding(14,1)]] ConstantBuffer<ForwardConstants> forwardConstants;
[[vk::binding(15,1)]] Texture3D<float> forwardVisibility;
[[vk::binding(16,1)]] SamplerState forwardSampler;
void forward_decline(uint index,uint key,uint reason){if(control.extra.z==1u)packets.Store4(16u+index*16u,uint4(reason,0,0x46574432u,key));}
[numthreads(64,1,1)] void cs_shade(uint3 id:SV_DispatchThreadID){
    // All borrowed accesses stay at the original admitted COMPUTE dispatch.
    if(control.grants.x!=1u||!control.domain.z||control.domain.w!=control.domain.z||
       control.draw.y!=2u||any(control.domain.xy==0u)||any(control.domain.xy>16384u)||
       frame.control.x!=1u||raster.uv[2].w!=1u||(mtoonMaterial.modes.z!=2u||(mtoonMaterial.shade.w!=0.f&&mtoonMaterial.shade.w!=1.f)))return;
    uint bytes,packetBytes;identities.GetDimensions(bytes);packets.GetDimensions(packetBytes);if(bytes<32u||packetBytes<16u)return;
    if(control.draw.w==0u||control.draw.w>32u)return;
    const uint4 h=identities.Load4(0),count=identities.Load4(16);uint records;
    if(h.y!=control.domain.z||any(h.zw!=control.domain.xy)||count.y!=0u||count.w!=0u||count.x>count.z||
       !evrm_forward_shape(h,count.z,bytes,records)||id.x>=count.x||packetBytes<16u+count.z*128u||
       any(packets.Load3(0)!=uint3(0x4d544650u,h.y,count.z)))return;
    const uint4 record=identities.Load4(records+id.x*24u);const uint key=record.y;
    if(record.x>=h.z*h.w||(key>>1u)==0u||record.z!=control.draw.w)return;
    const uint ordinal=(key>>1u)-1u;if(ordinal<raster.domain.z||ordinal-raster.domain.z>=raster.domain.w)return;
    const uint2 pixel=uint2(record.x%h.z,record.x/h.z);NativeMToonReconstructedSurface surface;
    if(!native_mtoon_reconstruct_surface(vertices,indices,surfaceVertices,rawUvs,frame,raster,float2(pixel)+.5f,key,surface)){forward_decline(id.x,key,2);return;}
    // One 16-byte result packet per record, then one 112-byte owned record each:
    // five words of main-light inputs and three for the additional lights.
    // native_mtoon_forward.hpp sizes the buffer with the same 16+112 bytes.
    const uint scratch=16u+count.z*16u+id.x*112u;
#if !EVRM_MTOON_FORWARD_FINISH
    if((control.grants.y&64u)==0u||(control.grants.z&1024u)==0u)return;
    const float depth=asfloat(identities.Load(records+id.x*24u+20u));NativeMToonScenePosition position;
    if(!native_mtoon_scene_position(scene,pixel,control.domain.xy,depth,position)){forward_decline(id.x,key,6);return;}
    NativeMToonSceneModulation water;uint detail,facts;
    if(!shade_water(position,pixel,water,detail,facts)){forward_decline(id.x,key,7);return;}
    NativeMToonScenePixelLight sampled;
    if(!native_mtoon_scene_samples(scene,position,surface.normal,1.f,pixel,control.grants.z,control.grants.w,water,
        atmosphere,atmosphereSampler,fog,fogSampler,environment,environmentSampler,sunMask,ssao,ssaoSampler,sampled)){forward_decline(id.x,key,8);return;}
    if(!shade_sun_edge(pixel,false,sampled)){forward_decline(id.x,key,8);return;}
    // The coarse transparent volume can miss skin/hair shadows on alpha face
    // overlays. Unqualified depth retains the established10..12mm supplement;
    // a current same-actor winner permits the measured detached-eye interval.
    // A distant wall/sky must not shadow a foreground
    // transparent surface. Compare camera-relative positions, avoiding world
    // coordinate cancellation, and leave missing/nonfinite depth neutral.
    float overlayVisibility=1.f,numericGap=65504.f;bool sameActorShadow=false;uint opaqueWidth,opaqueHeight;
    nativeDepth.GetDimensions(opaqueWidth,opaqueHeight);
    if(all(uint2(opaqueWidth,opaqueHeight)==control.domain.xy)){
        NativeMToonScenePosition opaquePosition;
        const float opaqueDepth=nativeDepth.Load(int3(pixel,0));
        if(native_mtoon_scene_position(scene,pixel,control.domain.xy,opaqueDepth,opaquePosition)){
            const float3 separation=position.relative-opaquePosition.relative;
            numericGap=min(length(separation),65504.f);
            const float separationSquared=dot(separation,separation);
            float fullSquared=.0001f,maximumSquared=.000144f;
            // Actual detached iris/highlight gaps reach16.587mm. Only a
            // current same-actor opaque winner permits the wider20..24mm
            // window; other players, walls and missing IDs retain10..12mm.
            if(separationSquared>.0001f&&separationSquared<=.000576f&&
               native_mtoon_same_actor_shadow(work,summary,pixel,control.domain.xy,control.domain.z,control.draw.w)){
                sameActorShadow=true;fullSquared=.0004f;maximumSquared=.000576f;
            }
            if(separationSquared<=maximumSquared){
                // Keep the interior mask exact, then hand off continuously
                // to the native volume within the independently qualified window.
                const float transition=saturate((separationSquared-fullSquared)/(maximumSquared-fullSquared));
                const float release=transition*transition*(3.f-2.f*transition);
                overlayVisibility=lerp(sampled.sun_visibility*sampled.fog_shadow,1.f,release);
            }
        }
    }
    // Preserve the original Deferred input scope. Directional visibility is
    // produced later; retain the unoccluded sun, GI and exact point response
    // in OWNED storage and finish only when the owned volume is available.
    const float numericOpaqueVisibility=sampled.sun_visibility*sampled.fog_shadow;
    sampled.sun_visibility=1.f;sampled.fog_shadow=1.f;
    const float3 view=evrm_mtoon_unit(-surface.relative_position,0);if(all(view==0.f))return;
    const EvrmMToonSamples samples=evrm_mtoon_samples(surface.raw_uv,raster.time.x,surface.normal,view,surface.camera_up);
    NativeMToonSceneRadiance state;
    if(!native_mtoon_begin_scene_radiance(scene,probes,position.world,surface.normal,view,sampled,samples,false,state,surface.normal_dx,surface.normal_dy)){forward_decline(id.x,key,10);return;}
    const uint numeric=0u;
    // Validate the original unfiltered point sum before retaining the limited
    // response. No native reads late.
    const float3 sourceMain=evrm_mtoon_native_source_bound(state.toon.main_light,state.toon.samples,state.toon.exposure,.5f);
    state.toon.color=0;state.toon.source_color=0; // isolate point lights; emission/rim are main-only
    if(!shade_points(state,pixel,depth,surface.normal,detail,facts)){forward_decline(id.x,key,11);return;}
    // The main light is not final until the finish stage knows the visibility,
    // and the shared budget depends on it. So the additional lights are handed
    // over in three parts, exactly as held: the base colour they produced,
    // their untextured light, and their rim and MatCap extras. Nothing is
    // combined or scaled here.
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
    const float3 pointDiffuse=state.toon.point_diffuse,pointLight=state.toon.point_light,pointExtras=state.toon.point_extras;
#else
    const float3 pointDiffuse=state.toon.color,pointLight=0,pointExtras=0;
#endif
    const float3 sourceBound=(sourceMain+abs(state.toon.source_color))*state.toon.exposure;
    if(any(!isfinite(sourceBound))||any(abs(sourceBound)>65504.f)){forward_decline(id.x,key,11);return;}
    const EvrmMToonLighting light=state.toon.main_light;
    packets.Store4(scratch,asuint(float4(light.color,state.toon.exposure)));
    packets.Store4(scratch+16u,asuint(float4(light.direction,asfloat(scene.words[0].w))));
    packets.Store4(scratch+32u,uint4(asuint(light.indirect),key));
    // Actor1..32 remains exact in low16 bits; the upper half stores only the
    // bounded scalar shadow supplement.
    packets.Store4(scratch+48u,uint4(asuint(light.equalized),record.z|(numeric<<6u)|(f32tof16(overlayVisibility)<<16u)));
    packets.Store4(scratch+64u,uint4(asuint(pointDiffuse),control.domain.z));
    // Each added word carries its own tag, so a finish stage never reads a
    // record this stage did not complete.
    packets.Store4(scratch+80u,uint4(asuint(pointLight),control.domain.z));
    packets.Store4(scratch+96u,uint4(asuint(pointExtras),key));
#else
    // This stage has no native descriptors in its compiled SPIR-V. Native
    // scalar/images/point atlases may already have changed. Only immutable
    // owned geometry/materials, copied light values and our volume are read.
    const uint4 indirect=packets.Load4(scratch+32u),equalized=packets.Load4(scratch+48u),points=packets.Load4(scratch+64u);
    const uint4 pointLightWord=packets.Load4(scratch+80u),pointExtrasWord=packets.Load4(scratch+96u);
    if(indirect.w!=key||(equalized.w&((control.extra.z&4u)!=0u?63u:65535u))!=record.z||points.w!=control.domain.z||
       pointLightWord.w!=control.domain.z||pointExtrasWord.w!=key)return;
    const float3 pointDiffuse=asfloat(points.xyz),pointLight=asfloat(pointLightWord.xyz),pointExtras=asfloat(pointExtrasWord.xyz);
    if(any(!isfinite(pointLight))||any(pointLight<0.f)||any(!isfinite(pointExtras)))return;
    const float overlayVisibility=f16tof32(equalized.w>>16u);
    if(!isfinite(overlayVisibility)||overlayVisibility<0.f||overlayVisibility>1.f)return;
    const float4 sun=asfloat(packets.Load4(scratch)),direction=asfloat(packets.Load4(scratch+16u));
    if(any(!isfinite(sun))||sun.w<=0.f||any(sun.xyz<0.f)||any(!isfinite(direction))||direction.w<0.f||
       any(!isfinite(asfloat(indirect.xyz)))||any(!isfinite(asfloat(equalized.xyz)))||any(!isfinite(asfloat(points.xyz))))return;
    const float reciprocalW=asfloat(record.w);const float3 mapping=forwardConstants.words[114].xyz;
    if(!isfinite(reciprocalW)||reciprocalW<=0.f||any(!isfinite(mapping)))return;
    const float domain=rcp(reciprocalW)*mapping.x+mapping.y;if(!isfinite(domain)||domain<=0.f)return;
    const float3 uvw=float3((float2(pixel)+.5f)/float2(control.domain.xy),log2(domain)*mapping.z);
    if(any(!isfinite(uvw)))return;
    const float volumeVisibility=forwardVisibility.SampleLevel(forwardSampler,uvw,0);
    if(!isfinite(volumeVisibility)||volumeVisibility<0.f||volumeVisibility>1.f)return;
    const float visibility=min(volumeVisibility,overlayVisibility);
    const float3 view=evrm_mtoon_unit(-surface.relative_position,0);if(all(view==0.f))return;
    EvrmMToonSamples samples=evrm_mtoon_samples(surface.raw_uv,raster.time.x,surface.normal,view,surface.camera_up);
    samples.emission*=direction.w;
    EvrmMToonLighting light=(EvrmMToonLighting)0;light.normal=surface.normal;light.view=view;light.direction=direction.xyz;
    light.normal_dx=surface.normal_dx;light.normal_dy=surface.normal_dy;
    // Match opaque native filtering, including legacy colour attenuation.
    // The native adapter applies this owned visibility once after that step.
    light.color=sun.xyz;light.attenuation=visibility;light.indirect=asfloat(indirect.xyz);light.equalized=asfloat(equalized.xyz);
    if(!native_mtoon_legacy_ceiling_ready(samples,sun.w))return;
    const EvrmMToonReferenceResult mainResult=evrm_mtoon_reference_radiance(light,samples,sun.w,.5f,true,.5f);
    if(any(!isfinite(mainResult.native_source_color*sun.w))||any(abs(mainResult.native_source_color*sun.w)>65504.f))return;
#if EVRM_NATIVE_MTOON_DIFFUSE_BUDGET
    // The same join as the opaque path, now that this main result is final.
    const float3 joined=native_mtoon_join_lights(mainResult.color.rgb,mainResult.native_base,mainResult.native_light,pointLight,pointDiffuse,pointExtras,sun.w);
#else
    const float3 joined=mainResult.color.rgb+(pointDiffuse+pointExtras);
#endif
    const float4 hdr=float4(joined*sun.w,saturate(mainResult.color.a));
    if(mainResult.discard_fragment||any(!isfinite(hdr))||any(abs(hdr)>65504.f))return;
    const uint4 half=f32tof16(hdr);
    packets.Store4(16u+id.x*16u,uint4(half.x|(half.y<<16u),half.z|(half.w<<16u),0x46574431u,key));
    uint unused;packets.InterlockedAdd(12u,1u,unused);
#endif
}
#endif
