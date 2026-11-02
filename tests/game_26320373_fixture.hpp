#pragma once
#include <array>
#include <cstdint>
// Independent input facts from the original build26320373 image. The owned
// fixture intentionally does not read production layout constants for these
// changed fields. See docs/research/2026-11-02-game-26320373-compatibility.md.
namespace game_26320373_fixture {
using U=std::uint64_t;
inline constexpr U image_size=44302336;
inline constexpr std::uint32_t image_timestamp=1793351710;
inline constexpr std::array<unsigned char,32> image_sha256{{
    0x04,0x03,0x98,0x1f,0xd2,0xe8,0xad,0x18,0xbd,0x8c,0x56,0x98,0xdf,0xf0,0x3c,0x8d,
    0x50,0x5b,0x5a,0x46,0x67,0x78,0x32,0xc4,0x96,0xdb,0x9c,0x64,0x55,0x13,0xf2,0xa6}};
inline constexpr U root=0x2007cb0,root_vtable=0x1d69b60,client_vtable=0x1d69b20,controller_vtable=0x1d69e68;
inline constexpr U controller_offset=0x352a0,entity_manager_offset=0xe179c0;
inline constexpr U systems=0x745a28,system_count=0xe16628,system_stride=0x1f28;
inline constexpr U normal_bindings=0x1718,normal_count=0x1b18,alternate_bindings=0x1b20,alternate_count=0x1f20;
inline constexpr U equipment_descriptor=0x1dff7a0,equipment_function=0x3891e0;
inline constexpr U hide_descriptor=0x1dff0b0,hide_function=0x388c20,ui_descriptor=0x1dff300,ui_function=0x388d80;
inline constexpr U predicate=0x3bb660,gpu_vtable=0x1dc7cf0,buffer_constructor=0xfd30d0;
inline constexpr U client_slot=0x34428,inventory_return=0xbadb95,survivor_return=0xc47438,render_return=0x861d49;
inline constexpr U voice_encoder=0x719720,voice_call=0xaaeb7c;
}
