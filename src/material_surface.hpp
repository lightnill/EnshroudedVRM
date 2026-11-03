#pragma once
#include <array>
#include <cstdint>
namespace vrm {
enum class SurfaceKind : std::uint32_t { Gltf=0, MToon0=1, MToon1=2 };
struct SurfaceTexture {
    int texture=-1,texcoord=0;
    std::array<float,2> offset{0,0},scale{1,1};
    float rotation=0;
};
struct MaterialSurface {
    SurfaceKind kind=SurfaceKind::Gltf;
    SurfaceTexture normal,matcap,rim;
    float normal_scale=1;
    std::array<float,3> matcap_factor{1,1,1},rim_color{};
    float rim_power=5,rim_lift=0,rim_lighting_mix=1;
};
struct SurfaceVertex {
    float normal_uv[2]{},rim_uv[2]{},tangent[4]{};
};
static_assert(sizeof(SurfaceVertex)==32,"Optional surface vertex ABI");
}
