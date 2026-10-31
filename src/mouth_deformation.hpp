#pragma once
#include "vrm_asset.hpp"
namespace vrm {
inline constexpr std::uint64_t max_mouth_bytes=64ull*1024*1024;
MouthWeights mouth_expression_weights(const MouthData&,MouthWeights,float strength)noexcept;
bool apply_mouth_delta(Vertex&,const MouthDeltaRecord&,const MouthWeights&)noexcept;
struct MouthPositionInterval {std::array<double,3> low{},high{};bool valid=false;};
MouthPositionInterval mouth_position_interval(const Vertex&,const MouthDeltaRecord*)noexcept;
struct MouthStorageLayout {
    std::uint32_t lookup_offset=0,records_offset=0;
    std::uint64_t source_bytes=0,optional_peak_bytes=0;
    bool valid=false;
};
MouthStorageLayout mouth_storage_layout(std::size_t vertices,std::size_t records,std::size_t cpu_record_capacity,
    std::size_t range_capacity,std::size_t influence_bytes=0)noexcept;
}
