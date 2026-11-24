#ifndef EVRM_NATIVE_ACTOR_BASIS
#define EVRM_NATIVE_ACTOR_BASIS
// Actor rows contain R * diag(-forwardSign,1,forwardSign). Authored
// node/skin parity is already in tangent.w; compose actor parity once here.
// Deriving it from the rows also keeps proper diagnostic frames valid.
float evrm_actor_parity(float3x3 basis) {
    return dot(basis[0],cross(basis[1],basis[2]))<0.f?-1.f:1.f;
}
float4 evrm_actor_tangent(float3x3 basis,float4 tangent) {
    return float4(mul(basis,tangent.xyz),tangent.w*evrm_actor_parity(basis));
}
#endif
