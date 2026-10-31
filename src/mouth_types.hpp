#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vrm {
enum class MouthPreset:std::uint8_t { A,I,U,E,O };
using MouthWeights=std::array<float,5>;
inline MouthWeights bounded_mouth_weights(MouthWeights weights)noexcept {
    double sum=0;
    for(auto& w:weights){if(!std::isfinite(w))return {};w=std::clamp(w,0.f,1.f);sum+=w;}
    if(sum>1)for(auto& w:weights)w=static_cast<float>(w/sum);
    return weights;
}
}
