#ifndef EVRM_NATIVE_MTOON_RECONSTRUCT_OUTLINE
#define EVRM_NATIVE_MTOON_RECONSTRUCT_OUTLINE
#include "native_mtoon_outline_source.hlsli"
// The exact expanded triangle determines perspective weights and relative
// position. Shade with original posed normals/tangents and authored raw UVs,
// matching the native outline raster pair. The separately supplied cull mode
// must match that actual retained outline pipeline/material policy; a main
// material's cull mode cannot stand in for legacy outline culling.
// This helper grants no scene/source/submit or destination-write authority.
bool native_mtoon_reconstruct_outline_surface(ByteAddressBuffer vertices,ByteAddressBuffer indices,
    ByteAddressBuffer surfaceVertices,ByteAddressBuffer rawUvs,ByteAddressBuffer legacy,ByteAddressBuffer batches,
    NativeMToonPreparedFrame frame,NativeMToonRasterUniform draw,float2 pixel,uint key,uint legacyMode,
    uint outlineCull,bool active,out NativeMToonReconstructedSurface result){
    result=(NativeMToonReconstructedSurface)0;
    if(!active||frame.control.x!=1u||draw.uv[2].w!=1u||mtoonMaterial.modes.x!=1u||mtoonMaterial.modes.y>1u||
       mtoonMaterial.modes.z>1u||mtoonMaterial.modes.w>2u||outlineCull>2u||(key>>1u)==0u||any(draw.domain.xy==0u)||
       any(!isfinite(pixel))||!isfinite(draw.time.x)||draw.geometry.y==0u||draw.geometry.w==0u||draw.geometry.w%3u!=0u||
       draw.domain.w!=draw.geometry.w/3u)return false;
    if(mtoonMaterial.modes.y==1u&&outlineCull!=1u)return false;
    const bool front=(key&1u)!=0u;
    if((outlineCull==1u&&front)||(outlineCull==2u&&!front))return false;
    const uint ordinal=(key>>1u)-1u;
    if(ordinal<draw.domain.z||ordinal-draw.domain.z>=draw.domain.w)return false;
    uint vertexBytes,indexBytes;vertices.GetDimensions(vertexBytes);indices.GetDimensions(indexBytes);
    const uint vertexCount=vertexBytes/44u,indexCount=indexBytes/4u;
    if(draw.geometry.x>=vertexCount||draw.geometry.y>vertexCount-draw.geometry.x||draw.geometry.z>=indexCount||draw.geometry.w>indexCount-draw.geometry.z)return false;
    const uint first=draw.geometry.z+(ordinal-draw.domain.z)*3u;const uint3 vertexIds=indices.Load3(first*4u);
    if(any(vertexIds<draw.geometry.x)||any(vertexIds-draw.geometry.x>=draw.geometry.y))return false;
    float3 positions[3],normals[3],pixelWidths;float4 clips[3];
    [unroll]for(uint n=0u;n<3u;++n){
        NativeMToonOutlinePoint source;
        if(!native_mtoon_outline_point_source(vertices,legacy,batches,vertexIds[n],legacyMode,source))return false;
        if(mtoonMaterial.textures[8].info.y!=0u&&!native_mtoon_raster_uv(rawUvs,draw,vertexIds[n],8u,source.width_uv))return false;
        if(mtoonMaterial.textures[9].info.y!=0u&&!native_mtoon_raster_uv(rawUvs,draw,vertexIds[n],9u,source.mask_uv))return false;
        NativeMToonOutlineGeometry expanded;
        if(!native_mtoon_outline_geometry(frame,(NativeMToonPreparedFrame)0,draw,source,(NativeMToonOutlinePoint)0,0.f,false,expanded))return false;
        positions[n]=expanded.model_position;normals[n]=evrm_mtoon_unit(source.normal,0);clips[n]=expanded.clip;
        // A subpixel shell must not contribute a full pixel of fixed-colour
        // radiance. Measure the actual deformed, masked, scaled projection;
        // preserve geometry/history and the authored colour/width themselves.
        const float4 mainClip=native_mtoon_outline_main_clip(frame,source.position);
        pixelWidths[n]=1.f;
        if(mainClip.w>0.f&&expanded.clip.w>0.f){
            const float2 shift=(expanded.clip.xy/expanded.clip.w-mainClip.xy/mainClip.w)*float2(draw.domain.xy)*.5f;
            const float width=length(shift);if(isfinite(width))pixelWidths[n]=width;
        }
    }
    if(!native_mtoon_interpolate_surface(surfaceVertices,rawUvs,frame,draw,pixel,front,vertexIds,positions,normals,clips,result))return false;
    float3 weights;if(!native_mtoon_surface_weights(clips[0],clips[1],clips[2],pixel,float4(0,0,draw.domain.xy),weights))return false;
    result.outline_coverage=saturate(dot(pixelWidths,weights));return isfinite(result.outline_coverage);
}
#endif
