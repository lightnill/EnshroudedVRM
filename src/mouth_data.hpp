#pragma once
#include "mouth_types.hpp"
#include <vector>
namespace vrm {
inline constexpr std::size_t max_mouth_bindings=160,max_mouth_channel_bindings=32;
inline constexpr std::size_t max_mouth_expanded_bindings=16384,max_mouth_ranges=4096;
inline constexpr std::size_t max_mouth_records=98304,max_mouth_source_visits=16000000;
struct MouthDeltaRecord {
    std::uint32_t vertex=0;
    std::array<std::array<float,3>,5> position{},normal{};
};
static_assert(sizeof(MouthDeltaRecord)==124,"Bounded imported mouth record ABI");
struct MouthPrimitiveRange {std::uint32_t node=0,primitive=0,first=0,count=0;};
struct MouthData {
    std::uint8_t available=0,binary=0;
    std::vector<MouthDeltaRecord> records;
    std::vector<MouthPrimitiveRange> ranges;
};
}
