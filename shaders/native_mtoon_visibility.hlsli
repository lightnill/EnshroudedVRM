#ifndef EVRM_NATIVE_MTOON_VISIBILITY
#define EVRM_NATIVE_MTOON_VISIBILITY
// Matches NativeMToonVisibilityUniform. This is a surface identity, not a
// readiness token. The caller supplies a cleared, frame-owned buffer and an
// early-fragment-test entry point with EQUAL/read-only depth. Alpha rejection
// must happen before this call. Never use it to infer visibility with late Z.
struct NativeMToonVisibilityUniform { uint width,height,first,triangles; };
uint native_mtoon_record_surface(RWByteAddressBuffer winner,NativeMToonVisibilityUniform u,
    float4 position,uint primitive,bool front,bool ready) {
    if(!ready || !u.width || !u.height || !u.triangles || primitive>=u.triangles ||
        u.triangles>0x7fffffffu || u.first>0x7fffffffu-u.triangles ||
        !all(isfinite(position)) || any(position.xyz<0) || position.z>1 ||
        position.x>=float(u.width) || position.y>=float(u.height)) return 0;
    uint bytes;winner.GetDimensions(bytes);
    // Division keeps the capacity check safe even for hostile extents.
    if(u.height>(bytes/4u)/u.width) return 0;
    uint key=((u.first+primitive+1u)<<1u)|uint(front);
    uint offset=(uint(position.y)*u.width+uint(position.x))*4u,previous;
    winner.InterlockedMax(offset,key,previous);
    return key;
}
#endif
