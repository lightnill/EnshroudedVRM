#pragma once
#include "native_state.hpp"
namespace evrm {
// Optional metadata for DeferredLighting feature64 (water). Indices are the
// nine-layer water shadow map, surface flags, depth and mipmapped caustics.
// No source/read/queue/layout/lifetime or per-pixel availability is certified.
struct NativeMToonWaterMetadata {
    NativeBufferBinding parameters;
    std::array<NativeSceneImageDescriptor,4> images;
    std::array<NativeSceneImageFacts,4> views;
};
} // namespace evrm
