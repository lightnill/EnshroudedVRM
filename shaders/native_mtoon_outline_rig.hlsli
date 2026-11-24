#ifndef EVRM_NATIVE_MTOON_OUTLINE_RIG
#define EVRM_NATIVE_MTOON_OUTLINE_RIG
float native_mtoon_rig_largest(float3 value){value=abs(value);return max(value.x,max(value.y,value.z));}
// Independently derived from legacy MToon's unit object-normal outline:
// |W^-T n_object| * W n_object. The deformed shading normal n is already
// unit W^-T n_object, so the metric vector is W W^T n / |W^T n|^2.
// Use the mesh world W, not a bone blend. Common uniform scale cancels;
// actor height is intentionally absent. The parent's geometry domain and
// source/pose leases must match this exact frame, including previous W/n.
bool native_mtoon_legacy_metric_vector(float3 x,float3 y,float3 z,float3 normal,out float3 expansion){
    expansion=0;
    if(any(!isfinite(x))||any(!isfinite(y))||any(!isfinite(z))||any(!isfinite(normal)))return false;
    const float matrixScale=max(native_mtoon_rig_largest(x),max(native_mtoon_rig_largest(y),native_mtoon_rig_largest(z)));
    const float normalScale=native_mtoon_rig_largest(normal);
    if(matrixScale<=0.f||normalScale<=0.f)return false;
    x/=matrixScale;y/=matrixScale;z/=matrixScale;normal/=normalScale;normal=normalize(normal);
    const float determinant=dot(x,cross(y,z));if(!isfinite(determinant)||determinant==0.f)return false;
    float3 object=float3(dot(x,normal),dot(y,normal),dot(z,normal));
    const float objectScale=native_mtoon_rig_largest(object);if(!isfinite(objectScale)||objectScale<=0.f)return false;
    object/=objectScale;const float denominator=dot(object,object)*objectScale;
    if(!isfinite(denominator)||denominator<=0.f)return false;
    const float3 candidate=(x*object.x+y*object.y+z*object.z)/denominator;
    if(any(!isfinite(candidate))||all(candidate==0.f))return false;
    expansion=candidate;return true;
}
bool native_mtoon_legacy_rig_vector(ByteAddressBuffer sources,ByteAddressBuffer batches,uint vertex,float3 normal,out float3 expansion){
    expansion=0;uint sourceBytes,batchBytes;sources.GetDimensions(sourceBytes);batches.GetDimensions(batchBytes);
    // Immutable skin sources can append mouth records after their 64-byte
    // vertices. The caller must first bound vertex in the exact 44-byte
    // deformed geometry/draw, never derive that domain from sourceBytes.
    if(vertex>=sourceBytes/64u||batchBytes<80u||batchBytes%80u!=0u)return false;
    const uint batch=sources.Load(vertex*64u+56u);if(batch>=batchBytes/80u)return false;
    const uint at=batch*80u;
    const float4 x=asfloat(batches.Load4(at)),y=asfloat(batches.Load4(at+16u)),z=asfloat(batches.Load4(at+32u)),t=asfloat(batches.Load4(at+48u));
    if(any(!isfinite(x))||any(!isfinite(y))||any(!isfinite(z))||any(!isfinite(t))||abs(x.w)>1e-5f||abs(y.w)>1e-5f||abs(z.w)>1e-5f||abs(t.w-1.f)>1e-5f)return false;
    return native_mtoon_legacy_metric_vector(x.xyz,y.xyz,z.xyz,normal,expansion);
}
#endif
