#pragma once
#include <array>
#include <cstdint>
namespace evrm {
// Immutable importer facts, distinct from uploaded material or native readiness.
enum class MToonMaterialSupport:std::uint8_t {None,Opaque,Cutout,Transparent,TransparentForward};
struct MToonMenuMaterial {MToonMaterialSupport support=MToonMaterialSupport::None;bool outline=false,legacy_world_outline=false;};
enum class MToonMenuState:std::uint8_t {NotRequested,Preparing,WorldUnavailable,WorldReady,RendererUnavailable};
// Read-only bounded menu diagnostics. These never authorize rendering. Bits
// describe actual selected/outlined materials in the latest submitted view.
struct MToonMenuStatus {
    MToonMenuState state=MToonMenuState::NotRequested;
    std::array<std::uint64_t,64> materials{},outlines{};
    bool material(std::uint32_t n)const noexcept{return n<4096&&(materials[n/64]&(std::uint64_t(1)<<(n%64)));}
    bool outline(std::uint32_t n)const noexcept{return n<4096&&(outlines[n/64]&(std::uint64_t(1)<<(n%64)));}
};
}
