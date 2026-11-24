#pragma once
#include "mtoon_material.hpp"
#include <cstdint>
namespace evrm {
// Numeric storage/eligibility only; no native source or submission permission.
inline bool native_mtoon_forward_supported(const vrm::MToonMaterial& m)noexcept{
    return (m.version==vrm::MToonVersion::Vrm0||m.version==vrm::MToonVersion::Vrm1)&&
        m.render_mode==vrm::MToonRenderMode::Transparent&&!m.depth_write&&!m.transparent_with_zwrite&&!m.alpha_to_coverage&&
        (m.version==vrm::MToonVersion::Vrm1||(m.source_blend==5&&m.destination_blend==10));
}
// Per transparent record in the owned packet buffer: one 16-byte result packet
// and one owned record the prepare stage hands to the finish stage. That record
// is five 16-byte words of main-light inputs and three for the additional
// lights (base colour, untextured light, rim/MatCap extras), which the finish
// stage needs apart to apply the shared light budget. It was 80 bytes while
// the additional lights were handed over as one sum. The shade shader uses the
// same two numbers (native_mtoon_shade.hlsl, native_mtoon_numeric.hlsli).
inline constexpr std::uint32_t native_mtoon_forward_packet_bytes=16,native_mtoon_forward_owned_bytes=112;
struct NativeMToonForwardLayout {
    std::uint32_t head_offset=32,record_offset=0,capacity=0;
    std::uint64_t capture_bytes=0,packet_bytes=0;
    explicit operator bool()const noexcept{return capacity!=0;}
};
inline NativeMToonForwardLayout native_mtoon_forward_layout(std::uint32_t width,std::uint32_t height,std::uint32_t capacity)noexcept{
    const auto pixels=std::uint64_t(width)*height;
    if(!width||!height||width>16384||height>16384||pixels>(1u<<24)||!capacity||capacity>262144)return {};
    NativeMToonForwardLayout out;out.record_offset=32+std::uint32_t(pixels)*4;out.capacity=capacity;
    out.capture_bytes=out.record_offset+std::uint64_t(capacity)*24;out.packet_bytes=16+std::uint64_t(capacity)*(native_mtoon_forward_packet_bytes+native_mtoon_forward_owned_bytes);return out;
}
}
