#ifndef EVRM_NATIVE_MTOON_SCENE_SAMPLES
#define EVRM_NATIVE_MTOON_SCENE_SAMPLES
#include "native_mtoon_scene_position.hlsli"
#include "native_mtoon_scene_light_inputs.hlsli"
#include "native_surface_camera.hlsli"
bool native_mtoon_scene_sample_interval(float value){return isfinite(value)&&value>=0.f&&value<=1.f;}
float3 native_mtoon_scene_environment_direction(float3 direction,float4 rotation){
    // Native Deferred binding27 uses word117 as two rows over XZ, preserving Y.
    return float3(dot(direction.xz,rotation.xy),direction.y,dot(direction.xz,rotation.zw));
}
// Caller supplies actual admitted source masks and exact native sampler
// objects. Metadata availability alone is not sufficient. This function grants
// no initialization/layout/use/queue/lifetime or destination-write authority.
// Water modulation is separately computed from its own admitted producers.
bool native_mtoon_scene_samples(NativeMToonSceneLightParameters scene,NativeMToonScenePosition position,
    float3 normal,float materialOcclusion,uint2 pixel,uint images,uint samplers,NativeMToonSceneModulation water,
    Texture2D<float4> atmosphere,SamplerState atmosphereSampler,Texture3D<float> fog,SamplerState fogSampler,
    TextureCube<float4> environment,SamplerState environmentSampler,Texture2D<float> sunMask,
    Texture2D<float> ssao,SamplerState ssaoSampler,out NativeMToonScenePixelLight result){
    result=(NativeMToonScenePixelLight)0;
    const uint2 extent=scene.words[2].xy;const uint flags=scene.words[118].w;
    const float4 gains=asfloat(scene.words[0]);const float3 sunColor=asfloat(scene.words[111].xyz);
    if(any(extent==0u)||any(extent>16384u)||any(pixel>=extent)||(flags&~127u)!=0u||(images&~16383u)!=0u||(samplers&~127u)!=0u||
       any(!isfinite(position.world))||any(!isfinite(position.uv))||any(!isfinite(gains))||any(gains<0.f)||any(!isfinite(sunColor))||any(sunColor<0.f))return false;
    const float2 reciprocal=asfloat(scene.words[2].zw),expected=1.f/float2(extent);
    precise float2 pixelUv=(float2(pixel)+.5f)*reciprocal;
    if(any(!isfinite(reciprocal))||any(reciprocal<=0.f)||any(abs(reciprocal-expected)>expected*1e-5f)||
       any(abs(position.uv-pixelUv)>reciprocal*1e-5f))return false;
    const bool sunActive=gains.x>0.f&&any(sunColor!=0.f),probeGi=gains.z>0.f&&(flags&8u)!=0u;
    const bool useEnvironment=(flags&8u)==0u&&scene.words[118].z!=0u,indirect=probeGi||useEnvironment;
    NativeMToonScenePixelLight working=(NativeMToonScenePixelLight)0;
    working.transmittance=1;working.sun_visibility=working.fog_shadow=working.material_occlusion=working.ssao=1;
    if((flags&64u)!=0u&&(sunActive||probeGi)){
        if(!water.available)return false;
        if(sunActive){if(any(!isfinite(water.sunlight))||any(water.sunlight<0.f))return false;working.sunlight_modulation=water.sunlight;}
        if(probeGi){if(!isfinite(water.gi)||water.gi<0.f)return false;working.gi_modulation=water.gi;}
        working.native_modulation_available=true;
    }
    if(sunActive){
        if(!(images&(1u<<4)))return false;uint sunWidth,sunHeight;sunMask.GetDimensions(sunWidth,sunHeight);
        if(any(uint2(sunWidth,sunHeight)!=extent))return false;
        working.sun_visibility=sunMask.Load(int3(pixel,0));if(!native_mtoon_scene_sample_interval(working.sun_visibility))return false;
        if(scene.words[112].x!=0u){
            if(!(images&(1u<<1))||!(samplers&(1u<<1)))return false;
            const float sunY=asfloat(scene.words[110].y);const float2 heightParameters=asfloat(scene.words[112].zw);
            const float height=position.world.y*heightParameters.x+heightParameters.y;
            if(!isfinite(sunY)||any(!isfinite(heightParameters))||!isfinite(height))return false;
            uint width,heightTex;atmosphere.GetDimensions(width,heightTex);if(!width||!heightTex)return false;
            const float2 uv=float2(saturate((sunY+.15f)/1.15f),sqrt(saturate(height)));
            working.transmittance=atmosphere.SampleLevel(atmosphereSampler,uv,0).xyz;
            if(any(!isfinite(working.transmittance))||any(working.transmittance<0.f)||any(working.transmittance>1.f))return false;
        }
        if((flags&1u)==0u){
            if(!(images&(1u<<2))||!(samplers&(1u<<2)))return false;
            const float3 scale=asfloat(scene.words[119].xyz),offset=asfloat(scene.words[120].xyz);
            const float3 uvw=position.world*scale+offset;
            if(any(!isfinite(scale))||any(!isfinite(offset))||any(!isfinite(uvw)))return false;
            uint width,height,depth;fog.GetDimensions(width,height,depth);if(!width||!height||!depth)return false;
            working.fog_shadow=fog.SampleLevel(fogSampler,uvw,0);if(!native_mtoon_scene_sample_interval(working.fog_shadow))return false;
        }
    }
    if(indirect){
        if((flags&4u)!=0u){if(!native_mtoon_scene_sample_interval(materialOcclusion))return false;working.material_occlusion=materialOcclusion;}
        if((flags&2u)!=0u){
            if(!(images&(1u<<5))||!(samplers&(1u<<4)))return false;uint width,height;ssao.GetDimensions(width,height);
            if(any(uint2(width,height)!=extent))return false;
            working.ssao=ssao.SampleLevel(ssaoSampler,position.uv,0);if(!native_mtoon_scene_sample_interval(working.ssao))return false;
        }
    }
    if(useEnvironment){
        if(!(images&(1u<<3))||!(samplers&(1u<<3)))return false;
        const float3 direction=evrm_camera_unit(normal,0);if(all(direction==0.f))return false;
        const float4 rotation=asfloat(scene.words[117]);if(any(!isfinite(rotation)))return false;
        const float3 environmentNormal=native_mtoon_scene_environment_direction(direction,rotation);
        const float3 environmentUp=native_mtoon_scene_environment_direction(float3(0,1,0),rotation);
        const float3 environmentDown=native_mtoon_scene_environment_direction(float3(0,-1,0),rotation);
        if(any(!isfinite(environmentNormal))||all(environmentNormal==0.f)||
           any(!isfinite(environmentUp))||all(environmentUp==0.f)||
           any(!isfinite(environmentDown))||all(environmentDown==0.f))return false;
        uint width,height;environment.GetDimensions(width,height);if(!width||width!=height)return false;
        working.environment_normal=environment.SampleLevel(environmentSampler,environmentNormal,0).xyz;
        working.environment_up=environment.SampleLevel(environmentSampler,environmentUp,0).xyz;
        working.environment_down=environment.SampleLevel(environmentSampler,environmentDown,0).xyz;
        if(any(!isfinite(working.environment_normal))||any(working.environment_normal<0.f)||any(!isfinite(working.environment_up))||
           any(working.environment_up<0.f)||any(!isfinite(working.environment_down))||any(working.environment_down<0.f))return false;
    }
    working.available_images=images;result=working;return true;
}
#endif
