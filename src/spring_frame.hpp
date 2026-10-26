#pragma once
#include <array>
#include <cstdint>

namespace evrm {
// Same unsigned wrapping and fixed-point unit as the bridge's actor_matrix.
// Passing this origin delta lets physics retain camera-independent world tails.
inline std::array<double,3> spring_origin_delta(const std::array<std::uint32_t,3>& current,
                                               const std::array<std::uint32_t,3>& previous) {
    std::array<double,3> out{};
    for(unsigned i=0;i<3;++i) {
        const std::uint32_t wrapped=current[i]-previous[i];
        const std::int64_t delta=wrapped>2147483647u?static_cast<std::int64_t>(wrapped)-4294967296LL:wrapped;
        out[i]=static_cast<double>(delta)/131072.0;
    }
    return out;
}
} // namespace evrm
