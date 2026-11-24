#include "native_actor_basis.hlsli"
// Independently authored native opaque/masked outline coverage and auxiliary
// outputs. Native MToon lighting/composition is a separate admitted consumer.
// No expanded shadow caster. Resources/control belong to one retained draw.
#define EVRM_MTOON_DESCRIPTOR_SET 1
#include "native_mtoon_outline_source.hlsli"
#include "native_mtoon_visibility.hlsli"
struct NativeOutlineMaterial {
    float4 base,emissive,surface;
    uint4 instance;
    float4 alpha;
};
struct NativeOutlineControl {uint4 facts;float4 time;};
[[vk::binding(0,0)]] ConstantBuffer<NativeMToonPreparedFrame> outlineFrame;
[[vk::binding(3,0)]] ConstantBuffer<NativeOutlineMaterial> outlineNativeMaterial;
[[vk::binding(4,0)]] ByteAddressBuffer outlineVertices;
[[vk::binding(5,0)]] ByteAddressBuffer outlineIndices;
[[vk::binding(11,0)]] ByteAddressBuffer outlineSurfaceVertices;
[[vk::binding(0,2)]] ConstantBuffer<NativeMToonRasterUniform> outlineDraw;
[[vk::binding(1,2)]] RWByteAddressBuffer outlineIdentity;
[[vk::binding(2,2)]] ByteAddressBuffer outlineRawUvs;
[[vk::binding(3,2)]] ConstantBuffer<NativeMToonPreparedFrame> outlinePreviousFrame;
[[vk::binding(4,2)]] ByteAddressBuffer outlinePreviousVertices;
[[vk::binding(5,2)]] ByteAddressBuffer outlineLegacy;
[[vk::binding(6,2)]] ByteAddressBuffer outlinePreviousLegacy;
[[vk::binding(7,2)]] ConstantBuffer<NativeOutlineControl> outlineControl;
[[vk::binding(8,2)]] ByteAddressBuffer outlineBatches;
[[vk::binding(9,2)]] ByteAddressBuffer outlinePreviousBatches;
struct NativeOutlineVertexOut {
    float4 position:SV_Position;
    [[vk::location(0)]] float3 normal:NORMAL0;
    [[vk::location(1)]] float2 baseUv:TEXCOORD0;
    [[vk::location(2)]] float2 maskUv:TEXCOORD1;
    [[vk::location(3)]] float3 previousUvW:TEXCOORD2;
    [[vk::location(4)]] nointerpolation uint valid:TEXCOORD3;
    [[vk::location(5)]] float3 relativePosition:TEXCOORD4;
    [[vk::location(6)]] float4 tangent:TEXCOORD5;
    [[vk::location(7)]] float2 normalUv:TEXCOORD6;
};
bool native_outline_active(){return outlineControl.facts.w==1u && outlineFrame.control.x==1u &&
    outlineNativeMaterial.instance.w==1u && outlineNativeMaterial.alpha.w==0.f;}
NativeOutlineVertexOut vs_outline(uint vertexId:SV_VertexID){
    NativeOutlineVertexOut output=(NativeOutlineVertexOut)0;output.position=float4(2,2,0,1);output.previousUvW.z=-1;
    if(!native_outline_active() || outlineDraw.uv[2].w!=1u || outlineDraw.geometry.w==0u || outlineDraw.geometry.w%3u!=0u ||
       outlineDraw.domain.w!=outlineDraw.geometry.w/3u || vertexId<outlineDraw.geometry.z || vertexId-outlineDraw.geometry.z>=outlineDraw.geometry.w)return output;
    uint indexBytes;outlineIndices.GetDimensions(indexBytes);if(vertexId>=indexBytes/4u)return output;
    const uint vertex=outlineIndices.Load(vertexId*4u);
    if(vertex<outlineDraw.geometry.x || vertex-outlineDraw.geometry.x>=outlineDraw.geometry.y)return output;
    NativeMToonOutlinePoint current,previous=(NativeMToonOutlinePoint)0;
    if(!native_mtoon_outline_point_source(outlineVertices,outlineLegacy,outlineBatches,vertex,outlineControl.facts.x,current))return output;
    float2 widthUv=0,maskUv=0,baseUv=0;
    if(mtoonMaterial.textures[8].info.y!=0u && !native_mtoon_raster_uv(outlineRawUvs,outlineDraw,vertex,8u,widthUv))return output;
    if(mtoonMaterial.textures[9].info.y!=0u && !native_mtoon_raster_uv(outlineRawUvs,outlineDraw,vertex,9u,maskUv))return output;
    if(mtoonMaterial.textures[0].info.y!=0u && !native_mtoon_raster_uv(outlineRawUvs,outlineDraw,vertex,0u,baseUv))return output;
    current.width_uv=widthUv;current.mask_uv=maskUv;
    bool joined=outlineControl.facts.y==1u && outlineFrame.control.y==1u && isfinite(outlineControl.time.x);
    NativeMToonPreparedFrame old=(NativeMToonPreparedFrame)0;
    if(joined){
        old=outlinePreviousFrame;
        joined=native_mtoon_outline_point_source(outlinePreviousVertices,outlinePreviousLegacy,outlinePreviousBatches,vertex,outlineControl.facts.z,previous);
        previous.width_uv=widthUv;previous.mask_uv=maskUv;
    }
    NativeMToonOutlineGeometry geometry;
    if(!native_mtoon_outline_geometry(outlineFrame,old,outlineDraw,current,previous,outlineControl.time.x,joined,geometry))return output;
    if(mtoonMaterial.textures[2].info.y!=0u){
        uint surfaceBytes;outlineSurfaceVertices.GetDimensions(surfaceBytes);if(vertex>=surfaceBytes/32u)return output;
        const float4 tangent=asfloat(outlineSurfaceVertices.Load4(vertex*32u+16u));
        if(any(!isfinite(tangent))||(tangent.w!=0.f&&abs(tangent.w)!=1.f)||!native_mtoon_raster_uv(outlineRawUvs,outlineDraw,vertex,2u,output.normalUv))return output;
        const float3x3 rotation=float3x3(outlineFrame.normalRows[0].xyz,outlineFrame.normalRows[1].xyz,outlineFrame.normalRows[2].xyz);
        output.tangent=evrm_actor_tangent(rotation,tangent);
    }
    output.position=geometry.clip;output.normal=geometry.normal;output.baseUv=baseUv;output.maskUv=maskUv;output.relativePosition=geometry.relative_position;
    output.previousUvW=geometry.previous_uv_w;output.valid=1u;return output;
}
float native_outline_alpha(NativeOutlineVertexOut input){
    clip(native_outline_active() && input.valid==1u?1.f:-1.f);
    const float animation=evrm_mtoon_mask(9u,evrm_mtoon_sample_static(9u,input.maskUv));
    const float alpha=evrm_mtoon_sample(0u,input.baseUv,outlineDraw.time.x,animation).a*mtoonMaterial.base.a;
    if(!isfinite(alpha))discard;
    if(mtoonMaterial.modes.z==1u)clip(alpha-mtoonMaterial.animation.w);
    return animation;
}
uint ps_outline_visibility(NativeOutlineVertexOut input):SV_Target0{
    native_outline_alpha(input);return 0x08000000u|(outlineNativeMaterial.instance.x&255u);
}
struct NativeOutlineGBuffer {
    float4 base:SV_Target0;float4 normal:SV_Target1;uint4 params:SV_Target2;
    uint emissive:SV_Target3;float2 motion:SV_Target4;float instance:SV_Target5;
};
[earlydepthstencil]
NativeOutlineGBuffer ps_outline_gbuffer(NativeOutlineVertexOut input,bool front:SV_IsFrontFace,uint primitive:SV_PrimitiveID){
    const float animation=native_outline_alpha(input);
    // Match the pinned versioned normal contract: VRM1 reverses the geometric
    // normal before tangent mapping; legacy faces the final mapped normal
    // toward the view. Expanded backfaces still use authored normal maps.
    float3 normal=normalize(mtoonMaterial.modes.y==1u&&!front?-input.normal:input.normal);
    if(mtoonMaterial.textures[2].info.y!=0u){
        const float3 encoded=evrm_mtoon_sample(2u,input.normalUv,outlineDraw.time.x,animation).xyz;
        normal=evrm_mtoon_normal(normal,input.tangent,encoded,mtoonMaterial.indirect.w);
    }
    if(mtoonMaterial.modes.y==0u&&dot(-input.relativePosition,normal)<0.f)normal=-normal;
    const float3 folded=normal/(abs(normal.x)+abs(normal.y)+abs(normal.z));
    NativeOutlineGBuffer output;
    output.base=float4(mtoonMaterial.outline.rgb,1);
    output.normal=float4(.5f*(folded.x+folded.y)+.5f,.5f*(folded.y-folded.x)+.5f,1,normal.z>0.f?2.f/3.f:0.f);
    output.params=uint4(0,0,0,(outlineNativeMaterial.instance.z&100u)|8u);output.emissive=0;
    output.motion=input.previousUvW.z>0.f?input.previousUvW.xy/input.previousUvW.z-(input.position.xy*outlineFrame.screen.xy+outlineFrame.screen.zw):float2(0,0);
    output.instance=(outlineNativeMaterial.instance.y&255u)!=0u?1.f:0.f;
    NativeMToonVisibilityUniform identity={outlineDraw.domain.x,outlineDraw.domain.y,outlineDraw.domain.z,outlineDraw.domain.w};
    native_mtoon_record_surface(outlineIdentity,identity,input.position,primitive,front,true);
    return output;
}
