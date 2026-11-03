#pragma once
#include <array>
#include <cstdint>
// Independent input facts from the original build26404966 image. The owned
// fixture intentionally does not read production layout constants for these
// changed fields. See docs/research/2026-11-03-game-26404966-compatibility.md.
namespace game_26404966_fixture {
using U=std::uint64_t;
inline constexpr U image_size=44298240;
inline constexpr std::uint32_t image_timestamp=1793700524;
inline constexpr std::array<unsigned char,32> image_sha256{{
    0xce,0x6f,0xdb,0xa6,0x3e,0x27,0x74,0x49,0x43,0xb6,0x95,0x92,0x13,0x83,0xa2,0x91,0x33,0x28,0x5d,0x35,0x9e,0x07,0x67,0x1f,0x6c,0xa3,0x3a,0x47,0xe2,0x14,0x76,0x18}};
inline constexpr U root=0x2006cb0,root_vtable=0x1d68b60,client_vtable=0x1d68b20,controller_vtable=0x1d68e68;
inline constexpr U controller_offset=0x352a0,entity_manager_offset=0xe179c0;
inline constexpr U systems=0x745a28,system_count=0xe16628,system_stride=0x1f28;
inline constexpr U normal_bindings=0x1718,normal_count=0x1b18,alternate_bindings=0x1b20,alternate_count=0x1f20;
inline constexpr U equipment_descriptor=0x1dfe7e0,equipment_function=0x3891e0;
inline constexpr U hide_descriptor=0x1dfe0f0,hide_function=0x388c20,ui_descriptor=0x1dfe340,ui_function=0x388d80;
inline constexpr U predicate=0x3bb660,gpu_vtable=0x1dc6cf0,buffer_constructor=0xfd30b0;
inline constexpr U client_slot=0x34428,inventory_return=0xbadb75,survivor_return=0xc47418,render_return=0x861d29;
inline constexpr U voice_encoder=0x719720,voice_call=0xaaeb5c;
}
