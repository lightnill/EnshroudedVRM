#ifndef EVRM_NATIVE_MTOON_OUTLINE_SOURCE
#define EVRM_NATIVE_MTOON_OUTLINE_SOURCE
#include "native_mtoon_outline_geometry.hlsli"
#include "native_mtoon_outline_rig.hlsli"
bool native_mtoon_outline_point_source(ByteAddressBuffer vertices,ByteAddressBuffer legacy,ByteAddressBuffer batches,uint vertex,uint legacyMode,
    out NativeMToonOutlinePoint source){
    source=(NativeMToonOutlinePoint)0;uint bytes;vertices.GetDimensions(bytes);
    if(vertex>=bytes/44u)return false;
    source.position=asfloat(vertices.Load3(vertex*44u));source.normal=asfloat(vertices.Load3(vertex*44u+12u));
    if(mtoonMaterial.modes.y==0u && mtoonMaterial.flags.x==1u){
        if(legacyMode==1u){
            uint legacyBytes;legacy.GetDimensions(legacyBytes);if(vertex>=legacyBytes/12u)return false;
            source.legacy_metric_expansion=asfloat(legacy.Load3(vertex*12u));
        }else if(legacyMode==2u){
            if(!native_mtoon_legacy_rig_vector(legacy,batches,vertex,source.normal,source.legacy_metric_expansion))return false;
        }else return false;
        source.legacy_available=true;
    }
    return true;
}
#endif
