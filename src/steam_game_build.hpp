#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace evrm {
inline constexpr std::uint32_t enshrouded_steam_app_id=1203620;
struct SteamGameBuild {
    std::uint32_t app_id=0,build_id=0;
    std::string install_directory;
    explicit operator bool()const noexcept{return app_id==enshrouded_steam_app_id&&build_id&&!install_directory.empty();}
};
// Local installed metadata only, never executable layout or hook attestation.
// TargetBuildID and nested depot values cannot substitute for AppState.buildid.
SteamGameBuild parse_steam_game_build(std::string_view)noexcept;
// Read the corresponding steamapps/appmanifest_1203620.acf, bound to its
// common/<installdir>/enshrouded.exe path. No Steam API/init/network operation.
SteamGameBuild read_steam_game_build(const std::filesystem::path& executable)noexcept;
}
