#pragma once
#include <filesystem>
namespace evrm {
enum class SettingsImportStatus {Missing,Existing,Imported,Declined,Unavailable};
struct SettingsImportResult {
    SettingsImportStatus avatar=SettingsImportStatus::Unavailable,mod=SettingsImportStatus::Unavailable;
};
SettingsImportResult migrate_standalone_settings(const std::filesystem::path& import_root,const std::filesystem::path& state_root)noexcept;
}
