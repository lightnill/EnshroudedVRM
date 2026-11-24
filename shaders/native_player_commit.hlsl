// Individual prepared flags remain authoritative for their own VRM draws.
// Shared clipping suppresses only bodies whose replacements actually prepared.
[[vk::binding(0,0)]] ByteAddressBuffer frame;
[[vk::binding(1,0)]] ByteAddressBuffer subject_mask;
[[vk::binding(2,0)]] RWByteAddressBuffer effective_mask;
[[vk::binding(3,0)]] RWByteAddressBuffer clip_control;
struct CommitMode { uint mode; };
[[vk::push_constant]] ConstantBuffer<CommitMode> control;

[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=4096) return;
    if(control.mode==0) {
        effective_mask.Store(id.x*4,0);
        if(id.x==0) clip_control.Store(192,0);
    } else if(frame.Load(192)==1) {
        effective_mask.InterlockedOr(id.x*4,subject_mask.Load(id.x*4));
        if(id.x==0) clip_control.InterlockedOr(192,1);
    }
}
