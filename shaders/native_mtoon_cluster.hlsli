#ifndef EVRM_NATIVE_MTOON_CLUSTER
#define EVRM_NATIVE_MTOON_CLUSTER
#include "native_mtoon_point_lights.hlsli"
// Exact64-byte DeferredLighting set2:22 layout. Borrowed buffers still require
// scene/source admission before these helpers run. This is numeric traversal,
// not a producer, lifetime, layout, shadow or source-permission certificate.
struct NativeMToonClusterParameters {uint4 words[4];};
struct NativeMToonClusterIterator {
    uint cell,offset,first,last,next_word,end_word,word_base,bits,count;
    bool empty,failed;
};
bool native_mtoon_cluster_begin(NativeMToonClusterParameters p,uint2 pixel,float depth,
    ByteAddressBuffer cells,ByteAddressBuffer visible,ByteAddressBuffer lights,out NativeMToonClusterIterator it) {
    it=(NativeMToonClusterIterator)0;it.empty=true;
    const uint3 counts=p.words[0].xyz;
    const float3 scale=asfloat(p.words[1].xyz);
    const uint stride=p.words[1].w;
    const float2 pixelScale=asfloat(p.words[3].xy),planes=asfloat(p.words[3].zw);
    if(any(counts==0u)||any(counts>4096u)||counts.x>16777216u/counts.y||counts.x*counts.y>16777216u/counts.z||
       any(!isfinite(scale))||any(scale<=0)||any(!isfinite(pixelScale))||any(pixelScale<=0)||
       any(!isfinite(planes))||any(planes<=0)||!isfinite(depth)||depth<0||depth>1||!stride||(stride&3u)||stride>8192u)return false;
    const float numerator=(depth-1.0f)*planes.x,denominator=numerator-planes.y*depth;
    if(!isfinite(numerator)||!isfinite(denominator)||denominator==0.0f)return false;
    const float3 coordinate=float3((float(pixel.x)+.5f)*pixelScale.x,1.0f-(float(pixel.y)+.5f)*pixelScale.y,numerator/denominator)*scale;
    if(any(!isfinite(coordinate)))return false;
    // Native out-of-grid pixels have no point-light cell. Avoid undefined
    // float-to-uint conversion for a negative/out-of-range coordinate.
    if(any(coordinate<0)||any(coordinate>=float3(counts)))return true;
    const uint3 grid=uint3(coordinate);
    it.cell=(grid.x*counts.y+grid.y)*counts.z+grid.z;
    uint cellBytes,bitBytes,lightBytes;cells.GetDimensions(cellBytes);visible.GetDimensions(bitBytes);lights.GetDimensions(lightBytes);
    if(cellBytes<4u||it.cell>(cellBytes-4u)/4u)return false;
    const uint range=cells.Load(it.cell*4u);it.first=range&65535u;it.last=range>>16u;
    if(it.first>it.last)return true;
    it.next_word=it.first/32u;it.end_word=it.last/32u;
    if(it.end_word>=stride/4u||bitBytes<stride||it.cell>(bitBytes-stride)/stride||lightBytes/52u<=it.last)return false;
    it.offset=it.cell*stride;it.empty=false;return true;
}
bool native_mtoon_cluster_next(inout NativeMToonClusterIterator it,ByteAddressBuffer visible,out uint index) {
    index=0;if(it.empty||it.failed)return false;
    [loop]while(it.bits==0u){
        if(it.next_word>it.end_word){it.empty=true;return false;}
        it.word_base=it.next_word*32u;
        it.bits=visible.Load(it.offset+it.next_word*4u);++it.next_word;
        const uint first=max(it.first,it.word_base)-it.word_base,last=min(it.last,it.word_base+31u)-it.word_base;
        it.bits&=(0xffffffffu<<first)&(0xffffffffu>>(31u-last));
    }
    if(it.count>=128u){it.failed=true;return false;}
    const uint bit=firstbitlow(it.bits);it.bits&=it.bits-1u;++it.count;index=it.word_base+bit;return true;
}
bool native_mtoon_cluster_light(ByteAddressBuffer lights,uint index,out NativeMToonPointLight emitter) {
    emitter=(NativeMToonPointLight)0;uint bytes;lights.GetDimensions(bytes);
    if(index>=bytes/52u)return false;
    const uint offset=index*52u;
    return native_mtoon_unpack_point(lights.Load4(offset),lights.Load4(offset+16u),lights.Load4(offset+32u),lights.Load(offset+48u),emitter);
}
#endif
