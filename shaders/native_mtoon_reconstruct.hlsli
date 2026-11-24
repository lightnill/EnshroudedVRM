#include "native_actor_basis.hlsli"
#ifndef EVRM_NATIVE_MTOON_RECONSTRUCT
#define EVRM_NATIVE_MTOON_RECONSTRUCT
#include "native_mtoon_surface.hlsli"
#include "native_mtoon_raster.hlsli"
#include "native_surface_camera.hlsli"
#include "mtoon_material.hlsli"
struct NativeMToonPreparedFrame {
    float4 currentRows[4],previousUvRows[4],normalRows[3];
    float4 screen;
    uint4 control,rootScale;
};
struct NativeMToonReconstructedSurface {
    float3 normal,relative_position,camera_up,normal_dx,normal_dy;
    float outline_coverage; // owned projected-width estimate, not native coverage authority
    float2 raw_uv[11];
};
// Shared interpolation for the admitted main or actually expanded outline
// triangle. Geometry/culling/rank ownership is established by each caller.
void native_mtoon_normal_differentials(float4 clips[3],float3 weights,float3 normals[3],float3x3 rotation,float2 extent,
    float3 shadingNormal,out float3 dx,out float3 dy){
    dx=dy=0;
    const float2 a=clips[0].xy/clips[0].w,b=clips[1].xy/clips[1].w,c=clips[2].xy/clips[2].w;
    const float2 e=b-a,f=c-a;const float determinant=e.x*f.y-e.y*f.x;
    if(!isfinite(determinant)||abs(determinant)<1e-20f||any(extent<=0.f))return;
    const float2 ux=float2(f.y,-e.y)*(2.f/(extent.x*determinant));
    const float2 uy=float2(-f.x,e.x)*(2.f/(extent.y*determinant));
    const float3 affineX=float3(-ux.x-ux.y,ux.x,ux.y),affineY=float3(-uy.x-uy.y,uy.x,uy.y);
    const float3 inverseW=rcp(float3(clips[0].w,clips[1].w,clips[2].w));
    const float normalization=dot(weights,float3(clips[0].w,clips[1].w,clips[2].w));
    const float3 wx=(affineX*inverseW-weights*dot(affineX,inverseW))*normalization;
    const float3 wy=(affineY*inverseW-weights*dot(affineY,inverseW))*normalization;
    const float3 raw=mul(rotation,normals[0]*weights.x+normals[1]*weights.y+normals[2]*weights.z);
    const float lengthRaw=length(raw);if(!isfinite(lengthRaw)||lengthRaw<=1e-20f)return;
    const float3 n=raw/lengthRaw;
    const float3 rx=mul(rotation,normals[0]*wx.x+normals[1]*wx.y+normals[2]*wx.z);
    const float3 ry=mul(rotation,normals[0]*wy.x+normals[1]*wy.y+normals[2]*wy.z);
    const float polarity=dot(n,shadingNormal)<0.f?-1.f:1.f;
    dx=(rx-n*dot(n,rx))*(polarity/lengthRaw);dy=(ry-n*dot(n,ry))*(polarity/lengthRaw);
    // Preserve normal-map texel detail: only the smooth geometry contribution
    // sets this footprint. No additional material texture samples are taken.
    if(any(!isfinite(dx))||any(!isfinite(dy)))dx=dy=0;
}
bool native_mtoon_interpolate_surface(ByteAddressBuffer surfaceVertices,ByteAddressBuffer rawUvs,
    NativeMToonPreparedFrame frame,NativeMToonRasterUniform draw,float2 pixel,bool front,uint3 vertexIds,
    float3 positions[3],float3 normals[3],float4 clips[3],out NativeMToonReconstructedSurface result){
    result=(NativeMToonReconstructedSurface)0;
    const float scale=asfloat(frame.rootScale.w);
    if(!isfinite(scale) || scale<=0.0f)return false;
    [unroll] for(uint row=0u;row<4u;++row)if(any(!isfinite(frame.currentRows[row])))return false;
    [unroll] for(uint rowN=0u;rowN<3u;++rowN)if(any(!isfinite(frame.normalRows[rowN])))return false;
    const float3x3 rotation=float3x3(frame.normalRows[0].xyz,frame.normalRows[1].xyz,frame.normalRows[2].xyz);
    const float3 relative=float3(frame.normalRows[0].w,frame.normalRows[1].w,frame.normalRows[2].w);
    float3 weights;
    if(!native_mtoon_surface_weights(clips[0],clips[1],clips[2],pixel,float4(0,0,draw.domain.xy),weights))return false;
    const float3 position=positions[0]*weights.x+positions[1]*weights.y+positions[2]*weights.z;
    const float3 interpolated=normals[0]*weights.x+normals[1]*weights.y+normals[2]*weights.z;
    float3 normal=evrm_mtoon_unit(mul(rotation,interpolated),0);
    if(all(normal==0.0f))return false;
    const float3 relativePosition=evrm_surface_relative_position(rotation,position,scale,relative);
    if(any(!isfinite(relativePosition)) || all(relativePosition==0.0f))return false;
    // Interpolate raw UVs before every authored transform/animation. Absent
    // maps never authorize a read from offset zero (matcap uses view normals).
    [unroll] for(uint slot=0u;slot<11u;++slot) {
        if(slot==7u || mtoonMaterial.textures[slot].info.y==0u)continue;
        float2 a,b,c;
        if(!native_mtoon_raster_uv(rawUvs,draw,vertexIds.x,slot,a) ||
           !native_mtoon_raster_uv(rawUvs,draw,vertexIds.y,slot,b) ||
           !native_mtoon_raster_uv(rawUvs,draw,vertexIds.z,slot,c))return false;
        result.raw_uv[slot]=a*weights.x+b*weights.y+c*weights.z;
        if(any(!isfinite(result.raw_uv[slot])))return false;
    }
    // VRM1 reverses the geometric normal before its tangent-space transform.
    // Legacy MToon instead faces the mapped normal toward the current view.
    if(mtoonMaterial.modes.y==1u && !front)normal=-normal;
    if(mtoonMaterial.textures[2].info.y!=0u) {
        uint surfaceBytes;surfaceVertices.GetDimensions(surfaceBytes);
        if(any(vertexIds>=surfaceBytes/32u))return false;
        float4 tangents[3];
        [unroll] for(uint t=0u;t<3u;++t) {
            tangents[t]=asfloat(surfaceVertices.Load4(vertexIds[t]*32u+16u));
            if(any(!isfinite(tangents[t])) || (tangents[t].w!=0.f&&abs(tangents[t].w)!=1.0f))return false;
        }
        if(tangents[0].w!=tangents[1].w || tangents[0].w!=tangents[2].w)return false;
        // Import explicitly splits all three corners of a degenerate-UV face
        // onto zero bases. Match raster/ordinary rendering's geometry normal.
        if(tangents[0].w!=0.f){
            const float3 tangent=mul(rotation,tangents[0].xyz*weights.x+tangents[1].xyz*weights.y+tangents[2].xyz*weights.z);
            if(dot(tangent,tangent)<=1e-20f || any(!isfinite(tangent)))return false;
            const float mask=evrm_mtoon_mask(9u,evrm_mtoon_sample_static(9u,result.raw_uv[9]));
            const float3 encoded=evrm_mtoon_sample(2u,result.raw_uv[2],draw.time.x,mask).xyz;
            normal=evrm_mtoon_normal(normal,float4(tangent,tangents[0].w*evrm_actor_parity(rotation)),encoded,mtoonMaterial.indirect.w);
        }
    }
    if(mtoonMaterial.modes.y==0u && dot(-relativePosition,normal)<0.0f)normal=-normal;
    result.normal=normal;result.relative_position=relativePosition;result.outline_coverage=1.f;
    native_mtoon_normal_differentials(clips,weights,normals,rotation,float2(draw.domain.xy),normal,result.normal_dx,result.normal_dy);
    result.camera_up=evrm_surface_camera_up(rotation,frame.currentRows[1],frame.currentRows[3]);
    return all(isfinite(normal)) && all(isfinite(result.camera_up));
}
bool native_mtoon_reconstruct_surface(ByteAddressBuffer vertices,ByteAddressBuffer indices,ByteAddressBuffer surfaceVertices,
    ByteAddressBuffer rawUvs,NativeMToonPreparedFrame frame,NativeMToonRasterUniform draw,float2 pixel,uint key,
    out NativeMToonReconstructedSurface result) {
    result=(NativeMToonReconstructedSurface)0;
    if(frame.control.x==0u || draw.uv[2].w!=1u || mtoonMaterial.modes.x!=1u || mtoonMaterial.modes.y>1u ||
       mtoonMaterial.modes.w>2u || (key>>1u)==0u || any(draw.domain.xy==0u) || any(!isfinite(pixel)) ||
       !isfinite(draw.time.x) || draw.geometry.y==0u || draw.geometry.w==0u || draw.geometry.w%3u!=0u ||
       draw.domain.w!=draw.geometry.w/3u)return false;
    const bool front=(key&1u)!=0u;
    if((mtoonMaterial.modes.w==1u && front) || (mtoonMaterial.modes.w==2u && !front))return false;
    const uint ordinal=(key>>1u)-1u;
    if(ordinal<draw.domain.z || ordinal-draw.domain.z>=draw.domain.w)return false;
    uint vertexBytes,indexBytes;vertices.GetDimensions(vertexBytes);indices.GetDimensions(indexBytes);
    const uint vertexCount=vertexBytes/44u,indexCount=indexBytes/4u;
    if(draw.geometry.x>=vertexCount || draw.geometry.y>vertexCount-draw.geometry.x ||
       draw.geometry.z>=indexCount || draw.geometry.w>indexCount-draw.geometry.z)return false;
    const uint first=draw.geometry.z+(ordinal-draw.domain.z)*3u;
    const uint3 vertexIds=indices.Load3(first*4u);
    if(any(vertexIds<draw.geometry.x) || any(vertexIds-draw.geometry.x>=draw.geometry.y))return false;
    [unroll] for(uint row=0u;row<4u;++row)if(any(!isfinite(frame.currentRows[row])))return false;
    float3 positions[3],normals[3];float4 clips[3];
    [unroll] for(uint i=0u;i<3u;++i) {
        const uint at=vertexIds[i]*44u;
        positions[i]=asfloat(vertices.Load3(at));normals[i]=asfloat(vertices.Load3(at+12u));
        if(any(!isfinite(positions[i])) || any(!isfinite(normals[i])))return false;
        const float4 position=float4(positions[i],1);
        clips[i]=float4(dot(frame.currentRows[0],position),-dot(frame.currentRows[1],position),
                        dot(frame.currentRows[2],position),dot(frame.currentRows[3],position));
    }
    return native_mtoon_interpolate_surface(surfaceVertices,rawUvs,frame,draw,pixel,front,vertexIds,positions,normals,clips,result);
}
#endif
