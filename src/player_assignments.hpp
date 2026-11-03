#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace evrm {
// Private preferences. Never copy these fields into diagnostic output.
struct PlayerAssignment {std::string token,path;bool replacement_enabled=false;};
struct PlayerAssignments {std::array<std::uint8_t,32> secret{};std::vector<PlayerAssignment> entries;};
enum class PlayerAssignmentsStatus:std::uint8_t {Missing,Loaded,Invalid,Unavailable,UnsupportedVersion};
struct PlayerAssignmentsLoad {PlayerAssignments preferences;PlayerAssignmentsStatus status=PlayerAssignmentsStatus::Unavailable;};
bool make_player_assignment_secret(std::array<std::uint8_t,32>&)noexcept;
std::string player_assignment_token(const std::array<std::uint8_t,32>&,std::uint64_t account)noexcept;
bool valid_player_assignments(const PlayerAssignments&)noexcept;
bool set_player_assignment(PlayerAssignments&,const PlayerAssignment&)noexcept;
PlayerAssignmentsLoad read_player_assignments(const std::filesystem::path&)noexcept;
bool create_player_assignments(const std::filesystem::path&,const PlayerAssignments&)noexcept;
bool write_player_assignments(const std::filesystem::path&,const PlayerAssignments&)noexcept;
}
