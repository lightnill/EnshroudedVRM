#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace evrm {

// Metadata for the exact supported ambient-probe producer. Recognition and
// address validation prove neither coverage nor GPU completion, ownership,
// lifetime, scene kind, or readiness for native MToon rendering. The producer
// updates a selected list of cells; a dispatch does not initialize the volume.
enum class NativeMToonProbeProducer { Unsupported, AmbientConverge };

inline NativeMToonProbeProducer native_mtoon_probe_producer(
    std::uint32_t bytes, const std::array<std::uint8_t, 32>& digest) noexcept {
    constexpr std::array<std::uint8_t, 32> ambient_converge{
        0x68, 0xac, 0xf7, 0x84, 0xda, 0x65, 0xcd, 0x3e,
        0x99, 0x58, 0x23, 0x12, 0xbb, 0x1d, 0xb5, 0x11,
        0xed, 0x0f, 0x6d, 0x7a, 0x7d, 0x62, 0x50, 0x07,
        0xbc, 0xf5, 0x52, 0x89, 0x96, 0x31, 0x32, 0x7d};
    return bytes == 11900 && digest == ambient_converge
        ? NativeMToonProbeProducer::AmbientConverge
        : NativeMToonProbeProducer::Unsupported;
}

struct NativeMToonProbeLayout {
    // All bindings belong to native set 0. Coefficients are RGBA16_SFLOAT
    // storage 3D images. The four-byte root selector is at compute offset 0:
    // 0 selects the primary list/counter; 1 selects the backup list/counter.
    inline static constexpr std::array<std::uint32_t, 3> image_extent{128, 16, 16};
    inline static constexpr std::array<std::uint32_t, 3> coefficient_bindings{3, 4, 5};
    inline static constexpr std::uint32_t parameter_binding = 0;
    inline static constexpr std::uint32_t state_binding = 2;
    inline static constexpr std::array<std::uint32_t, 2> list_bindings{6, 7};
    inline static constexpr std::array<std::uint32_t, 2> counter_bindings{8, 9};
    inline static constexpr std::uint32_t workgroup_x = 64;
    inline static constexpr std::uint32_t lanes_per_probe = 16;
    inline static constexpr std::uint32_t root_bytes = 4;
};

struct alignas(16) NativeMToonProbeParameters {
    std::array<std::array<float, 4>, 8> origins{}; // xyz origin, w spacing
    std::array<std::array<std::int32_t, 4>, 8> grid_offsets{};
    std::array<std::array<std::uint32_t, 4>, 8> prefilter_words{};
    // Raw, deliberately default-free native fields starting at byte 384.
    // Words 8/9/10 are previous correction/current correction/exposure floats.
    std::array<std::uint32_t, 24> control_words{};
};
static_assert(sizeof(NativeMToonProbeParameters) == 480);
static_assert(alignof(NativeMToonProbeParameters) == 16);
static_assert(offsetof(NativeMToonProbeParameters, origins) == 0);
static_assert(offsetof(NativeMToonProbeParameters, grid_offsets) == 128);
static_assert(offsetof(NativeMToonProbeParameters, prefilter_words) == 256);
static_assert(offsetof(NativeMToonProbeParameters, control_words) == 384);

struct NativeMToonProbeCell {
    bool valid = false;
    std::uint32_t cascade = 0;
    std::array<std::uint32_t, 3> source{};
    std::array<std::uint32_t, 3> logical{};
};

inline NativeMToonProbeCell native_mtoon_probe_cell(
    std::uint32_t index,
    const std::array<std::array<std::int32_t, 4>, 8>& offsets) noexcept {
    if (index >= 8 * 4096) return {};
    NativeMToonProbeCell cell;
    cell.cascade = index / 4096;
    const auto local = index % 4096;
    const std::array<std::uint32_t, 3> xyz{local % 16, (local / 16) % 16, local / 256};
    cell.logical = {16 * cell.cascade + xyz[0], xyz[1], xyz[2]};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        // Match native unsigned wrap without signed overflow at INT_MIN/MAX.
        cell.source[axis] = (xyz[axis] + static_cast<std::uint32_t>(offsets[cell.cascade][axis])) & 15u;
    }
    cell.source[0] += 16 * cell.cascade;
    cell.valid = true;
    return cell;
}

inline bool native_mtoon_probe_cascade_valid(
    const NativeMToonProbeParameters& parameters, std::uint32_t cascade) noexcept {
    if (cascade >= parameters.origins.size()) return false;
    const auto& origin = parameters.origins[cascade];
    for (const auto component : origin) if (!std::isfinite(component)) return false;
    if (origin[3] <= 0) return false;
    for (const auto word : {9u, 10u}) {
        float value = 0;
        static_assert(sizeof(value) == sizeof(parameters.control_words[word]));
        std::memcpy(&value, &parameters.control_words[word], sizeof(value));
        if (!std::isfinite(value) || value <= 0) return false;
    }
    return true; // Numeric metadata only; not probe content or scene validity.
}

} // namespace evrm
