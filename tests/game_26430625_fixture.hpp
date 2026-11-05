#pragma once
#include <array>
#include <cstdint>
// Independent input facts from the original build26430625 image. The owned
// fixture intentionally does not read production layout constants for these
// changed fields. See docs/research/2026-11-04-game-26430625-compatibility.md.
namespace game_26430625_fixture {
using U=std::uint64_t;
inline constexpr U image_size=44298240;
inline constexpr std::uint32_t image_timestamp=1793791052;
inline constexpr std::array<unsigned char,32> image_sha256{{
    0x5d,0x9e,0xf2,0xf1,0x40,0xb8,0xed,0x49,0xe3,0x0d,0xbe,0xe6,0x55,0x84,0x1b,0x89,0xd0,0x8b,0xae,0xc2,0x0f,0x79,0x9a,0x18,0x17,0xe1,0x71,0xa1,0x8a,0x85,0x0c,0x77}};
inline constexpr U root=0x2006cb0,root_vtable=0x1d68b60,client_vtable=0x1d68b20,controller_vtable=0x1d68e68;
inline constexpr U controller_offset=0x352a0,entity_manager_offset=0xe179c0;
inline constexpr U systems=0x745a28,system_count=0xe16628,system_stride=0x1f28;
inline constexpr U normal_bindings=0x1718,normal_count=0x1b18,alternate_bindings=0x1b20,alternate_count=0x1f20;
inline constexpr U equipment_descriptor=0x1dfe7e0,equipment_function=0x3892c0;
inline constexpr U hide_descriptor=0x1dfe0f0,hide_function=0x388d00,ui_descriptor=0x1dfe340,ui_function=0x388e60;
inline constexpr U predicate=0x3bb740,gpu_vtable=0x1dc6cf0,buffer_constructor=0xfd3190;
inline constexpr U client_slot=0x34428,inventory_return=0xbadc55,survivor_return=0xc474f8,render_return=0x861e09;
inline constexpr U voice_encoder=0x719800,voice_call=0xaaec3c;
}
