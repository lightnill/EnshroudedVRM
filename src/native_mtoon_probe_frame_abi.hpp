#pragma once
#include <cstdint>
namespace evrm {
// Distinct from the incremental selected-list diagnostic snapshot ABI.
struct NativeMToonProbeFrameAbi {
    inline static constexpr std::uint32_t header_bytes=288,record_bytes=64,record_count=32768;
    inline static constexpr std::uint32_t bytes=header_bytes+record_bytes*record_count;
    inline static constexpr std::uint32_t units_offset=256,status_offset=272,magic=0x4d544650;
};
} // namespace evrm
