// Original, independent offscreen Instance128 scale-branch proof.
// This is not a game shader, ownership detector, or runtime replacement.
// Eligibility and source tokens are CPU-supplied test inputs. Inputs to the
// final transform must be finite; the primitive topology is triangle list.
struct PatchControl {
    uint4 counts;      // records, selected IDs, immutable commit, schema (=1)
    uint4 provenance;  // eligible (=1), source generation, expected generation, source identity
    uint4 expected;    // expected identity, reserved x3 (=0)
    uint4 camera;      // packed camera origin.xyz, reserved (=0)
};
[[vk::binding(0,0)]] ByteAddressBuffer sourceInstances;
[[vk::binding(1,0)]] RWByteAddressBuffer privateInstances;
[[vk::binding(2,0)]] StructuredBuffer<uint> selectedInstances;
[[vk::binding(3,0)]] ConstantBuffer<PatchControl> control;

[numthreads(1,1,1)]
void cs_patch(uint3 thread : SV_DispatchThreadID) {
    // One invocation validates the whole bounded transaction before any store.
    // The caller first copies the complete source into this private buffer.
    if(any(thread!=0u)||control.counts.z!=1u||control.counts.w!=1u)return;
    if(control.provenance.x!=1u||control.provenance.y==0u||control.provenance.w==0u)return;
    if(control.provenance.y!=control.provenance.z||control.provenance.w!=control.expected.x)return;
    if(any(control.expected.yzw!=0u)||control.camera.w!=0u)return;
    uint sourceBytes,privateBytes,selectionCount,selectionStride;
    sourceInstances.GetDimensions(sourceBytes);
    privateInstances.GetDimensions(privateBytes);
    selectedInstances.GetDimensions(selectionCount,selectionStride);
    const uint records=control.counts.x;
    const uint selected=control.counts.y;
    if(records==0u||records>256u||selected==0u||selected>64u)return;
    if(sourceBytes!=records*128u||privateBytes!=sourceBytes||selectionStride!=4u||selected>selectionCount)return;
    for(uint i=0u;i<selected;++i) {
        const uint id=selectedInstances[i];
        if(id>=records)return;
        for(uint j=0u;j<i;++j)if(selectedInstances[j]==id)return;
    }
    for(uint i=0u;i<selected;++i) {
        const uint offset=selectedInstances[i]*128u;
        privateInstances.Store(offset+12u,0u);
        privateInstances.Store3(offset+48u,uint3(0u,0u,0u));
    }
}

struct RasterOut {
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint tag : TEXCOORD0;
};
RasterOut vs_scale(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    const float3 vertices[3] = {
        float3(-.12f,-.10f,0), float3(.12f,-.10f,0), float3(0,.16f,0)
    };
    const uint offset=instance*128u;
    const uint flags=privateInstances.Load(offset+44u);
    const float3 factor=(flags&1u)!=0u
        ?asfloat(privateInstances.Load3(offset+48u))
        :asfloat(privateInstances.Load(offset+12u)).xxx;
    const float4 quaternion=asfloat(privateInstances.Load4(offset+16u));
    const float3 scaled=vertices[vertex]*factor;
    const float3 rotated=scaled+2.f*cross(quaternion.xyz,cross(quaternion.xyz,scaled)+quaternion.w*scaled);
    const float3 root=float3(asint(privateInstances.Load3(offset)-control.camera.xyz))/131072.f;
    const float3 world=rotated+root;
    RasterOut result;
    result.position=float4(world.x,-world.y,world.z,1);
    result.tag=instance+1u;
    return result;
}
uint ps_tag(RasterOut input) : SV_Target0 { return input.tag; }
