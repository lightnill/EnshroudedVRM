#ifndef EVRM_NATIVE_MTOON_POINT_LIGHTS
#define EVRM_NATIVE_MTOON_POINT_LIGHTS
// Independent numeric adapter for the pinned native 52-byte light record and
// EVSM atlas contract. Handles alone never authorize reads of native resources.
struct NativeMToonPointLight {
    float3 position,color;
    float radius,radius_squared;
    float4 rotation;
    float spot_scale,spot_offset;
    uint id,flags,filter;
};
struct NativeMToonPointResponse {float3 color,toward;float attenuation;};
bool native_mtoon_unpack_point(uint4 first,uint4 second,uint4 third,uint filter,out NativeMToonPointLight light) {
    light=(NativeMToonPointLight)0;
    const float2 radius=f16tof32(uint2(first.w&65535u,first.w>>16u));
    const float3 color=f16tof32(uint3(second.x>>16u,second.x&65535u,second.y>>16u));
    const float3 offset=f16tof32(uint3(second.y&65535u,second.z>>16u,second.z&65535u));
    const float4 rotation=f16tof32(uint4(third.x&65535u,third.x>>16u,third.y&65535u,third.y>>16u));
    const float2 spot=f16tof32(uint2(third.z&65535u,third.z>>16u));
    const float3 position=asfloat(first.xyz)+offset;
    if(any(!isfinite(radius)) || any(radius<=0) || any(!isfinite(color)) || any(color<0) ||
       any(!isfinite(position)) || (third.w&~3u)!=0u)return false;
    if((third.w&2u)!=0u && (any(!isfinite(rotation)) || abs(dot(rotation,rotation)-1.0f)>0.02f ||
       any(!isfinite(spot)) || spot.y<0.0f))return false;
    light.position=position;light.color=color;light.radius_squared=radius.x;light.radius=radius.y;
    light.rotation=rotation;light.spot_offset=spot.x;light.spot_scale=spot.y;
    light.id=second.w;light.flags=third.w;light.filter=filter;
    return true;
}
bool native_mtoon_point_response(NativeMToonPointLight light,float3 position,float gain,float visibility,
                                out NativeMToonPointResponse response) {
    response=(NativeMToonPointResponse)0;
    if(any(!isfinite(position)) || !isfinite(gain) || gain<0.0f || !isfinite(visibility) || visibility<0.0f || visibility>1.0f)return false;
    const float3 delta=light.position-position;const float distanceSquared=dot(delta,delta);
    if(!isfinite(distanceSquared))return false;
    if(distanceSquared<=1e-20f)return true;
    response.toward=delta*rsqrt(distanceSquared);
    if(distanceSquared>=light.radius_squared || gain==0.0f || visibility==0.0f)return true;
    const float normalized=distanceSquared/light.radius_squared;
    const float cutoff=max(1.0f-normalized*normalized,0.0f);
    float attenuation=cutoff*cutoff/(distanceSquared+0.125f);
    if((light.flags&2u)!=0u) {
        const float3 z=float3(0,0,1);
        const float3 forward=z+2.0f*cross(light.rotation.xyz,cross(light.rotation.xyz,z)+light.rotation.w*z);
        const float spot=saturate(light.spot_offset+dot(-forward,response.toward)*light.spot_scale);
        attenuation*=spot*spot;
    }
    if(attenuation==0.0f)return true;
    // Filtered lights require the admitted native cookie array. This variant
    // declines an affecting filter; it never silently substitutes white.
    if(light.filter!=0u)return false;
    response.color=light.color*(gain/3.14159265358979323846f);
    response.attenuation=attenuation*visibility;
    return all(isfinite(response.color)) && isfinite(response.attenuation);
}
float native_mtoon_chebyshev(float mean,float second,float receiver,float minimumVariance,float bleed) {
    if(receiver<=mean)return 1.0f;
    const float variance=max(second-mean*mean,minimumVariance),delta=receiver-mean;
    const float probability=variance/(variance+delta*delta);
    return saturate((probability-bleed)/(1.0f-bleed));
}
bool native_mtoon_evsm(float4 moments,float2 exponents,float minimumVariance,float bleed,float receiver,out float visibility) {
    visibility=0.0f;
    if(any(!isfinite(moments)) || any(!isfinite(exponents)) || any(exponents<0.0f) ||
       !isfinite(minimumVariance) || minimumVariance<=0.0f || !isfinite(bleed) || bleed<0.0f || bleed>=1.0f || !isfinite(receiver))return false;
    const float2 warped=float2(exp(exponents.x*receiver),-exp(-exponents.y*receiver));
    if(any(!isfinite(warped)))return false;
    const float result=min(native_mtoon_chebyshev(moments.x,moments.y,warped.x,minimumVariance,bleed),
                           native_mtoon_chebyshev(moments.z,moments.w,warped.y,minimumVariance,bleed));
    if(!isfinite(result))return false;
    visibility=result;return true;
}
bool native_mtoon_shadow_receiver(float3 position,float3 normal,float3 lightPosition,float3 towardLight,
    uint2 tileCount,uint elementSize,uint border,uint index,float normalBias,float depthBias,out float2 uv,out float distance) {
    uv=0.0f;distance=0.0f;
    if(any(!isfinite(position)) || any(!isfinite(normal)) || any(!isfinite(lightPosition)) || any(!isfinite(towardLight)) ||
       !isfinite(normalBias) || normalBias<0.0f || !isfinite(depthBias) || depthBias<0.0f ||
       any(tileCount==0u) || any(tileCount>65536u) || tileCount.x>65536u/tileCount.y || index>=tileCount.x*tileCount.y ||
       elementSize==0u || elementSize>32768u || border>=elementSize || border>=elementSize-border)return false;
    const float normalLength=dot(normal,normal),lightLength=dot(towardLight,towardLight);
    if(normalLength<=1e-20f || lightLength<=1e-20f || !isfinite(normalLength) || !isfinite(lightLength))return false;
    normal*=rsqrt(normalLength);towardLight*=rsqrt(lightLength);
    // The native contract scales normal bias by atlas element size, not radius.
    const float3 delta=position+normal*(normalBias*saturate(1.0f-dot(towardLight,normal))*float(elementSize))-lightPosition;
    const float squared=dot(delta,delta);
    if(!isfinite(squared) || squared<=1e-20f)return false;
    const float length=sqrt(squared);const float3 direction=delta/length;
    float2 octa=direction.xy/(abs(direction.x)+abs(direction.y)+abs(direction.z));
    if(direction.z<0.0f)octa=(1.0f-abs(octa.yx))*float2(octa.x>=0.0f?1.0f:-1.0f,octa.y>=0.0f?1.0f:-1.0f);
    const uint2 tile=uint2(index%tileCount.x,index/tileCount.x);
    const float2 center=(float2(tile)+0.5f)*float(elementSize);
    const float halfInner=0.5f*float(elementSize-2u*border);
    uv=(center+octa*halfInner)/(float2(tileCount)*float(elementSize));distance=length-depthBias;
    return all(isfinite(uv)) && isfinite(distance);
}
#endif
