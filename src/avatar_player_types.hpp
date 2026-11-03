#pragma once
#include "native_player_identity.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace evrm {
struct AvatarCandidate;
struct AvatarMenuSnapshot;
struct PreparedAvatar;
struct AvatarPlayerChoice {std::string subject,label;bool local=false;};
// Process-private immutable exports. Subject tokens never belong in reports.
struct AvatarSubjectSelection {
    std::string subject;
    NativePlayerIdentity identity;
    std::uint64_t desired_generation=0;
    bool replacement_enabled=false;
    std::shared_ptr<const PreparedAvatar> model;
    std::shared_ptr<const AvatarCandidate> candidate,selection;
    std::shared_ptr<const AvatarMenuSnapshot> menu;
};
}
