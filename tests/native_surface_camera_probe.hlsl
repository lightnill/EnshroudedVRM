// Actual production camera helpers under explicit generated cases; no authority.
#include "../shaders/native_surface_camera.hlsli"
struct CameraCase {
    uint4 root,origin;
    float4 cameraOffset,positionScale;
    float4 normalRows[3],projectionRows[4];
};
struct CameraResult {float4 normal,lit,unlit,rgba;};
[[vk::binding(19,0)]] StructuredBuffer<CameraCase> cases;
[[vk::binding(20,0)]] RWStructuredBuffer<CameraResult> results;
struct CameraControl {uint sampleCount,width,height,workload;float4 diffuse;};
[[vk::binding(21,0)]] ConstantBuffer<CameraControl> cameraControl;
[numthreads(64,1,1)]
void main(uint3 dispatch:SV_DispatchThreadID) {
    const uint index=dispatch.x;
    if(index>=cameraControl.width*cameraControl.height||cameraControl.sampleCount==0)return;
    const CameraCase c=cases[index%cameraControl.sampleCount];
    const float3x3 rotation=float3x3(c.normalRows[0].xyz,c.normalRows[1].xyz,c.normalRows[2].xyz);
    const float3 relative=evrm_surface_camera_relative(c.root.xyz,c.origin.xyz,c.cameraOffset.xyz,c.cameraOffset.w);
    const float3 position=evrm_surface_relative_position(rotation,c.positionScale.xyz,c.positionScale.w,relative);
    CameraResult result;
    result.normal=float4(relative,0);result.lit=float4(-position,0);
    result.unlit=float4(evrm_surface_camera_up(rotation,c.projectionRows[1],c.projectionRows[3]),0);
    result.rgba=float4(position,0);results[index]=result;
}
