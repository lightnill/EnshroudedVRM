// Owned surface identities -> per-draw visible rectangles and bounded indirect
// compute work. This grants no native scene/readiness or resource ownership.
struct Parameters {uint width,height,draws,budget;};
[[vk::binding(0,0)]] ByteAddressBuffer identities;
[[vk::binding(1,0)]] StructuredBuffer<uint4> ranges; // first ordinal,count,selected,reserved0
[[vk::binding(2,0)]] RWByteAddressBuffer records; // bbox uint4,dispatch uint4 (32 bytes)
[[vk::binding(3,0)]] RWByteAddressBuffer summary; // bounded group work,declined,reserved0
[[vk::binding(4,0)]] ConstantBuffer<Parameters> parameters;

[numthreads(64,1,1)] void cs_init(uint3 tid:SV_DispatchThreadID) {
    if(tid.x>=parameters.draws)return;
    records.Store4(tid.x*32u,uint4(0xffffffffu,0xffffffffu,0,0));
    records.Store4(tid.x*32u+16u,uint4(0,1,1,0));
}
groupshared uint boundsDraws[64];
[numthreads(8,8,1)] void cs_bounds(uint3 tid:SV_DispatchThreadID,uint3 group:SV_GroupID,uint local:SV_GroupIndex) {
    uint draw=0xffffffffu;
    if(tid.x<parameters.width && tid.y<parameters.height){
        uint rank=identities.Load((tid.y*parameters.width+tid.x)*4u)>>1u;
        if(rank){
            uint ordinal=rank-1u,low=0,high=parameters.draws;
            while(low<high){uint middle=low+(high-low)/2u;if(ranges[middle].x<=ordinal)low=middle+1u;else high=middle;}
            if(low){uint candidate=low-1u;uint4 range=ranges[candidate];
                if(range.z==1u && ordinal-range.x<range.y)draw=candidate;
            }
        }
    }
    // Every lane, including viewport tails and rejected identities, reaches
    // this barrier. Only exact selected pixels contribute to the same integer
    // min/max as before; shading, coverage and work-budget math are unchanged.
    boundsDraws[local]=draw;
    GroupMemoryBarrierWithGroupSync();
    if(draw==0xffffffffu)return;
    // One leader per draw in each 8x2 region. Bounding the search to 16 lanes
    // also bounds overhead when thin geometry mixes many materials in a tile.
    uint first=local&~15u;
    [loop] for(uint i=first;i<local;++i)if(boundsDraws[i]==draw)return;
    uint2 lo=tid.xy,hi=tid.xy;
    [loop] for(uint i=local+1u;i<first+16u;++i)if(boundsDraws[i]==draw){
        uint2 pixel=group.xy*8u+uint2(i&7u,i>>3u);lo=min(lo,pixel);hi=max(hi,pixel);
    }
    uint ignored,at=draw*32u;
    records.InterlockedMin(at,lo.x,ignored);records.InterlockedMin(at+4u,lo.y,ignored);
    records.InterlockedMax(at+8u,hi.x,ignored);records.InterlockedMax(at+12u,hi.y,ignored);
}
groupshared uint groupWork;
[numthreads(64,1,1)] void cs_arguments(uint3 tid:SV_DispatchThreadID,uint local:SV_GroupIndex) {
    if(local==0u)groupWork=0;
    GroupMemoryBarrierWithGroupSync();
    uint work=0;
    if(tid.x<parameters.draws){
        uint4 box=records.Load4(tid.x*32u);uint2 groups=0;
        if(box.x!=0xffffffffu && box.y!=0xffffffffu && box.z>=box.x && box.w>=box.y &&
            box.z<parameters.width && box.w<parameters.height)
            groups=(box.zw-box.xy+1u+7u)/8u;
        // The constructor bounds dimensions so a group of 64 sums cannot wrap.
        work=groups.x*groups.y;records.Store4(tid.x*32u+16u,uint4(groups.x,groups.y?groups.y:1u,1,0));
    }
    InterlockedAdd(groupWork,work);
    GroupMemoryBarrierWithGroupSync();
    if(local==0u){uint ignored;
        // At most 128 groups, each contributing at most budget+1, so the final
        // counter cannot wrap even for thousands of overlapping materials.
        summary.InterlockedAdd(0,min(groupWork,parameters.budget+1u),ignored);
    }
}
[numthreads(64,1,1)] void cs_finish(uint3 tid:SV_DispatchThreadID) {
    bool declined=summary.Load(0)>parameters.budget;
    if(tid.x==0u)summary.Store(4u,declined?1u:0u);
    if(tid.x>=parameters.draws)return;
    if(declined)records.Store4(tid.x*32u+16u,uint4(0,1,1,0));
}
