#include "mouth_deformation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace vrm {
MouthWeights mouth_expression_weights(const MouthData& data,MouthWeights weights,float strength)noexcept {
    if(!std::isfinite(strength)||(data.available&~31u)||(data.binary&~data.available))return {};
    weights=bounded_mouth_weights(weights);strength=std::clamp(strength,0.f,1.f);
    for(unsigned n=0;n<5;++n){weights[n]=(data.available&(1u<<n))?weights[n]*strength:0;
        if(data.binary&(1u<<n))weights[n]=weights[n]>.5f?1.f:0.f;}
    return weights;
}
bool apply_mouth_delta(Vertex& vertex,const MouthDeltaRecord& delta,const MouthWeights& weights)noexcept {
    Vertex result=vertex;bool changed_normal=false;
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(vertex.position[axis])||!std::isfinite(vertex.normal[axis]))return false;
    for(unsigned n=0;n<5;++n){const auto weight=weights[n];if(!std::isfinite(weight)||weight<0||weight>1)return false;
        for(unsigned axis=0;axis<3;++axis){if(!std::isfinite(delta.position[n][axis])||!std::isfinite(delta.normal[n][axis]))return false;
            result.position[axis]+=delta.position[n][axis]*weight;result.normal[axis]+=delta.normal[n][axis]*weight;
            changed_normal|=weight!=0&&delta.normal[n][axis]!=0;}}
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(result.position[axis])||!std::isfinite(result.normal[axis]))return false;
    if(changed_normal){auto length=std::hypot(double(result.normal[0]),double(result.normal[1]),double(result.normal[2]));
        if(length==0){result.normal=vertex.normal;length=std::hypot(double(result.normal[0]),double(result.normal[1]),double(result.normal[2]));}
        if(!std::isfinite(length)||length<=0)return false;
        for(auto& value:result.normal)value=float(value/length);
    }
    vertex=result;return true;
}
MouthPositionInterval mouth_position_interval(const Vertex& vertex,const MouthDeltaRecord* delta)noexcept {
    MouthPositionInterval result;
    constexpr double unit=0x1p-24,gamma=10*unit/(1-10*unit),tiny=std::numeric_limits<float>::min();
    for(unsigned axis=0;axis<3;++axis){const auto base=double(vertex.position[axis]);if(!std::isfinite(base))return {};
        double low=base,high=base,work=std::abs(base);
        if(delta){
            for(unsigned n=0;n<5;++n){const auto change=double(delta->position[n][axis]);if(!std::isfinite(change))return {};
                low+=std::min(0.,change);high+=std::max(0.,change);work+=std::abs(change);}
            if(work>std::numeric_limits<float>::max()/8.)return {};
            // Five multiplies/adds, with or without FMA, plus subnormal flushing.
            // Absolute intermediate work covers cancellation at either endpoint.
            const auto error=gamma*work+12*tiny;low-=error;high+=error;
        }
        result.low[axis]=low;result.high[axis]=high;
    }
    result.valid=true;return result;
}
MouthStorageLayout mouth_storage_layout(std::size_t vertices,std::size_t records,std::size_t capacity,
    std::size_t ranges,std::size_t influence_bytes)noexcept {
    if(!vertices||vertices>2000000||!records||records>max_mouth_records||capacity<records||capacity>max_mouth_records||
       !ranges||ranges>max_mouth_ranges||influence_bytes>max_mouth_bytes)return {};
    const auto prefix=std::uint64_t(vertices)*64,lookup=std::uint64_t(vertices)*4;
    const auto record_offset=(prefix+lookup+15)&~std::uint64_t(15),source=record_offset+std::uint64_t(records)*128;
    const auto optional_blob=source-prefix;
    const auto peak=std::uint64_t(capacity)*sizeof(MouthDeltaRecord)+std::uint64_t(ranges)*sizeof(MouthPrimitiveRange)+
        lookup+2*optional_blob+influence_bytes;
    if(source>std::numeric_limits<std::uint32_t>::max()||peak>max_mouth_bytes)return {};
    return {std::uint32_t(prefix),std::uint32_t(record_offset),source,peak,true};
}
}
