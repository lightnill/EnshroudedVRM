#pragma once
#include "avatar_service.hpp"
namespace evrm {
void initialize_avatar_runtime(std::shared_ptr<const PreparedAvatar> initial={})noexcept;
AvatarService* avatar_runtime_service()noexcept;
std::shared_ptr<const AvatarMenuSnapshot> avatar_runtime_snapshot()noexcept;
void apply_avatar_action(const AvatarMenuAction&,std::uintptr_t window)noexcept;
}
