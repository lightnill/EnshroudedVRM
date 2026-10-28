#pragma once
#include "native_owner.hpp"

namespace evrm {
enum class NativeMenuOwnerKind:std::uint8_t {frontend,character};
// Separate frontend and current-local-player Character renderer proofs. Neither
// grants gameplay ECS ownership or equipment writes. GPU qualifiers still gate.
struct NativeMenuOwnerCandidate {
    NativeOwnerImage image;
    std::uint64_t root=0,client=0,controller=0,scene_owner=0,scene_entries=0,scene_capacity=0;
    NativeMenuOwnerKind kind=NativeMenuOwnerKind::frontend;
    std::uint64_t renderer=0;
    NativeOwnerCandidate preview; // allocation only; never a gameplay candidate
    std::vector<NativeOwnerGuard> guards;
};
bool capture_native_menu_owner(const NativeOwnerMemory&,const NativeOwnerImage&,
    NativeMenuOwnerCandidate&,std::string&,const NativeOwnerLimits& = {}) noexcept;
bool capture_native_character_preview_owner(const NativeOwnerMemory&,const NativeOwnerImage&,
    NativeMenuOwnerCandidate&,std::string&,const NativeOwnerLimits& = {}) noexcept;
bool revalidate_native_menu_owner(const NativeOwnerMemory&,NativeMenuOwnerCandidate&,
    std::string&,const NativeOwnerLimits& = {}) noexcept;
bool same_native_menu_owner(const NativeMenuOwnerCandidate&,const NativeMenuOwnerCandidate&) noexcept;
}
