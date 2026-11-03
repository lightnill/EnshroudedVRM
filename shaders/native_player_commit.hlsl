// Shared admission for every replacement subject in one native clipping union.
// Caller initializes the coherent gate to 0/1 and retains all buffer leases.
[[vk::binding(0,0)]] RWByteAddressBuffer frame;
[[vk::binding(1,0)]] RWByteAddressBuffer gate;
struct CommitMode { uint mode; };
[[vk::push_constant]] ConstantBuffer<CommitMode> control;

[numthreads(1,1,1)]
void main() {
    if(control.mode==0) {
        gate.InterlockedAnd(0,frame.Load(192)==1?1u:0u);
    } else {
        frame.Store(192,gate.Load(0));
    }
}
