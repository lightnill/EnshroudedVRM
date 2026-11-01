#include "standalone_settings.hpp"
#include "standalone_files.hpp"
#include "avatar_settings.hpp"
#include "mod_settings.hpp"

namespace evrm {
namespace {
template<class Read,class Create,class Status>
SettingsImportStatus import_one(const std::filesystem::path& source,const std::filesystem::path& destination,Read read,Create create,Status missing,Status loaded)noexcept {
    try {
        // Any existing destination wins, including invalid files/links. Never
        // delete/replace a user's settings in an attempt to repair an import.
        if(GetFileAttributesW(destination.c_str())!=INVALID_FILE_ATTRIBUTES)return SettingsImportStatus::Existing;
        const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)return SettingsImportStatus::Unavailable;
        const auto source_attributes=GetFileAttributesW(source.c_str());
        if(source_attributes==INVALID_FILE_ATTRIBUTES) {
            const auto source_error=GetLastError();return source_error==ERROR_FILE_NOT_FOUND || source_error==ERROR_PATH_NOT_FOUND ? SettingsImportStatus::Missing : SettingsImportStatus::Declined;
        }
        detail::StandaloneFile directory(source.parent_path(),true),file(source);
        const auto parsed=read(source);
        if(parsed.status==missing)return SettingsImportStatus::Missing;
        if(parsed.status!=loaded)return SettingsImportStatus::Declined;
        if(create(destination,parsed))return SettingsImportStatus::Imported;
        return GetFileAttributesW(destination.c_str())!=INVALID_FILE_ATTRIBUTES ? SettingsImportStatus::Existing : SettingsImportStatus::Unavailable;
    }catch(...){return SettingsImportStatus::Declined;}
}
}
SettingsImportResult migrate_standalone_settings(const std::filesystem::path& import_root,const std::filesystem::path& state_root)noexcept {
    SettingsImportResult result;
    try {
        if(import_root.empty() || state_root.empty() || !detail::local_standalone_path(state_root))return result;
        detail::StandaloneFile directory(state_root,true);
        result.avatar=import_one(import_root/L"avatar-settings.json",state_root/L"avatar-settings.json",read_avatar_preferences,
            [](const auto& path,const AvatarSettingsLoad& parsed){return create_avatar_preferences(path,parsed.preferences);},AvatarSettingsStatus::Missing,AvatarSettingsStatus::Loaded);
        result.mod=import_one(import_root/L"mod-settings.json",state_root/L"mod-settings.json",read_mod_settings,
            [](const auto& path,const ModSettingsLoad& parsed){return create_mod_settings(path,parsed.settings);},ModSettingsStatus::Missing,ModSettingsStatus::Loaded);
    }catch(...){}
    return result;
}
}
