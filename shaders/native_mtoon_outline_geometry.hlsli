#ifndef EVRM_NATIVE_MTOON_OUTLINE_GEOMETRY
#define EVRM_NATIVE_MTOON_OUTLINE_GEOMETRY
#include "native_mtoon_reconstruct.hlsli"
#include "mtoon_outline.hlsl"
// Each geometryPoint is from that frame's already deformed geometry. Legacy expansion
// is an independently supplied *metric model-space* vector, including authored
// nonuniform object/rig scale. A normalized shading normal cannot replace it.
struct NativeMToonOutlinePoint {
    float3 position,normal,legacy_metric_expansion;
    float2 width_uv,mask_uv;
    bool legacy_available;
};
struct NativeMToonOutlineGeometry {
    float4 clip;
    float3 previous_uv_w,model_position,normal,relative_position;
    float width;
    bool history;
};
// A zero-width texel must be raster-invariant with the main mesh. Undoing
// actor scale in the rows and reapplying it to the point changes rounding.
float4 native_mtoon_outline_main_clip(NativeMToonPreparedFrame source,float3 position){
    const float4 p=float4(position,1.f);
    precise float4 clip=float4(dot(source.currentRows[0],p),-dot(source.currentRows[1],p),dot(source.currentRows[2],p),dot(source.currentRows[3],p));
    return clip;
}
bool native_mtoon_outline_frame(NativeMToonPreparedFrame source,uint2 extent,out EvrmMToonOutlineFrame frame) {
    frame=(EvrmMToonOutlineFrame)0;
    const float scale=asfloat(source.rootScale.w);
    if(source.control.x!=1u || any(extent==0u) || any(extent>16384u) || !isfinite(scale) || scale<=0.f ||
       any(!isfinite(source.screen)) || any(source.screen.xy<=0.f))return false;
    const float2 expected=1.f/float2(extent);
    if(any(abs(source.screen.xy-expected)>expected*1e-5f))return false;
    [unroll]for(uint n=0;n<4u;++n){
        if(any(!isfinite(source.currentRows[n])))return false;
        frame.clip_rows[n]=float4(source.currentRows[n].xyz/scale,source.currentRows[n].w);
        if(any(!isfinite(frame.clip_rows[n])))return false;
    }
    frame.clip_rows[1]=-frame.clip_rows[1];frame.extent=float2(extent);return true;
}
bool native_mtoon_outline_point(EvrmMToonOutlineFrame frame,float scale,NativeMToonOutlinePoint source,
                               out EvrmMToonOutlinePoint geometryPoint) {
    geometryPoint=(EvrmMToonOutlinePoint)0;
    if(any(!isfinite(source.position)) || any(!isfinite(source.normal)))return false;
    geometryPoint.normal=evrm_mtoon_unit(source.normal,0);if(all(geometryPoint.normal==0.f))return false;
    geometryPoint.position=source.position*scale;if(any(!isfinite(geometryPoint.position)))return false;
    if(mtoonMaterial.textures[8].info.y!=0u && any(!isfinite(source.width_uv)))return false;
    if(mtoonMaterial.textures[9].info.y!=0u && any(!isfinite(source.mask_uv)))return false;
    geometryPoint.width_uv=source.width_uv;geometryPoint.mask_uv=source.mask_uv;
    if(mtoonMaterial.modes.y==0u && mtoonMaterial.flags.x==1u){
        if(!source.legacy_available || any(!isfinite(source.legacy_metric_expansion)) || all(source.legacy_metric_expansion==0.f))return false;
        geometryPoint.legacy_expansion=source.legacy_metric_expansion;
    }
    if(mtoonMaterial.flags.x==2u){
        // Projection direction covectors are in model metric coordinates. The
        // cross product also handles an orthographic W row without inventing a
        // world-axis camera direction. Only the view-normal Z enters width.
        float3 forward=evrm_mtoon_unit(frame.clip_rows[3].xyz,0);
        if(all(forward==0.f))forward=evrm_mtoon_unit(cross(frame.clip_rows[0].xyz,frame.clip_rows[1].xyz),0);
        const float3 up=evrm_mtoon_unit(frame.clip_rows[1].xyz-forward*dot(frame.clip_rows[1].xyz,forward),0);
        const float3 right=evrm_mtoon_unit(cross(up,forward),0);
        if(all(forward==0.f)||all(up==0.f)||all(right==0.f))return false;
        geometryPoint.view_normal=float3(dot(geometryPoint.normal,right),dot(geometryPoint.normal,up),dot(geometryPoint.normal,forward));
    }
    return true;
}
// Numeric geometry/history adapter only. Parent retains actual frame, deformed
// geometryPoint/UV/legacy streams and matching material through every GPU consumer;
// this function supplies no native scene, resource-use or source authority.
bool native_mtoon_outline_geometry(NativeMToonPreparedFrame currentFrame,NativeMToonPreparedFrame previousFrame,
    NativeMToonRasterUniform draw,NativeMToonOutlinePoint currentPoint,NativeMToonOutlinePoint previousPoint,
    float previousSeconds,bool historyJoined,out NativeMToonOutlineGeometry result) {
    result=(NativeMToonOutlineGeometry)0;result.previous_uv_w=float3(0,0,-1);
    if(draw.uv[2].w!=1u || !isfinite(draw.time.x) || mtoonMaterial.modes.x!=1u || mtoonMaterial.modes.y>1u ||
       mtoonMaterial.modes.z>1u || mtoonMaterial.flags.x==0u || mtoonMaterial.flags.x>2u ||
       !isfinite(mtoonMaterial.outline_settings.x) || mtoonMaterial.outline_settings.x<=0.f)return false;
    EvrmMToonOutlineFrame now;EvrmMToonOutlinePoint geometryPoint;
    const float scale=asfloat(currentFrame.rootScale.w);
    if(!native_mtoon_outline_frame(currentFrame,draw.domain.xy,now)||!native_mtoon_outline_point(now,scale,currentPoint,geometryPoint))return false;
    const EvrmMToonOutlineVertex expanded=evrm_mtoon_outline_vertex(now,geometryPoint,draw.time.x);
    if(any(!isfinite(expanded.clip))||any(!isfinite(expanded.world)))return false;
    [unroll]for(uint n=0u;n<3u;++n)if(any(!isfinite(currentFrame.normalRows[n])))return false;
    const float3x3 rotation=float3x3(currentFrame.normalRows[0].xyz,currentFrame.normalRows[1].xyz,currentFrame.normalRows[2].xyz);
    result.normal=evrm_mtoon_unit(mul(rotation,geometryPoint.normal),0);if(all(result.normal==0.f))return false;
    result.model_position=expanded.width==0.f?currentPoint.position:expanded.world/scale;
    result.relative_position=evrm_surface_relative_position(rotation,result.model_position,scale,
        float3(currentFrame.normalRows[0].w,currentFrame.normalRows[1].w,currentFrame.normalRows[2].w));
    if(any(!isfinite(result.model_position))||any(!isfinite(result.relative_position)))return false;
    result.clip=expanded.width==0.f?native_mtoon_outline_main_clip(currentFrame,currentPoint.position):expanded.clip;result.width=expanded.width;
    if(any(!isfinite(result.clip)))return false;
    if(currentFrame.control.y!=1u || !historyJoined || !isfinite(previousSeconds))return true;
    EvrmMToonOutlineFrame old;EvrmMToonOutlinePoint oldPoint;
    if(!native_mtoon_outline_frame(previousFrame,draw.domain.xy,old)||
       !native_mtoon_outline_point(old,asfloat(previousFrame.rootScale.w),previousPoint,oldPoint))return true;
    const EvrmMToonOutlineVertex prior=evrm_mtoon_outline_vertex(old,oldPoint,previousSeconds);
    const float4 priorClip=prior.width==0.f?native_mtoon_outline_main_clip(previousFrame,previousPoint.position):prior.clip;
    if(any(!isfinite(priorClip))||priorClip.w<=0.f)return true;
    // Identical to native_prepare's previous_uv_rows: old native raster XY,
    // half viewport mapping, and the *previous* frame's own jitter bias.
    const float3 uvw=float3(priorClip.xy*.5f+(.5f+previousFrame.screen.zw)*priorClip.w,priorClip.w);
    if(any(!isfinite(uvw)))return true;
    result.previous_uv_w=uvw;result.history=true;return true;
}
#endif
