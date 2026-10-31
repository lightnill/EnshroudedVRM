#pragma once
#include "mod_settings.hpp"
namespace evrm {
void initialize_mod_settings_service(const std::filesystem::path& state_directory={})noexcept;
ModSettings mod_settings_snapshot()noexcept;
bool request_mod_settings(const ModSettings&)noexcept;
bool mod_settings_loaded()noexcept;
bool mod_settings_save_failed()noexcept;
}
