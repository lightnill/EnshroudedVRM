#ifndef EVRM_NATIVE_MTOON_RASTER
#define EVRM_NATIVE_MTOON_RASTER
// Matches the independently versioned 96-byte NativeMToonRasterUniform.
// Raw UV offsets are float2 element offsets, before authored transforms and
// animation. A false result never authorizes a fallback read at offset zero.
struct NativeMToonRasterUniform {
    uint4 domain,geometry;
    uint4 uv[3];
    float4 time;
};
bool native_mtoon_raster_uv(ByteAddressBuffer raw,NativeMToonRasterUniform draw,uint vertex,uint slot,out float2 uv) {
    uv=0;
    if(draw.uv[2].w!=1u || slot>=11u || vertex<draw.geometry.x || vertex-draw.geometry.x>=draw.geometry.y) return false;
    uint offset=draw.uv[slot/4u][slot%4u];
    if(offset==0xffffffffu) return false;
    uint bytes;raw.GetDimensions(bytes);uint count=bytes/8u,local=vertex-draw.geometry.x;
    if(offset>=count || local>=count-offset) return false;
    float2 value=asfloat(raw.Load2((offset+local)*8u));
    if(!all(isfinite(value))) return false;
    uv=value;return true;
}
#endif
