#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include "voice_input_types.hpp"
namespace evrm {
struct ModSettings {
    bool voice_enabled=false;
    std::uint32_t strength=85,sensitivity=50,smoothing_ms=60,hotkey=0x77;
    VoiceInputSource voice_source=VoiceInputSource::Game;
    MicrophoneDeviceId microphone_device{};
};
inline const std::string& microphone_device_id(const ModSettings& settings)noexcept {
    static const std::string empty;return settings.microphone_device?*settings.microphone_device:empty;
}
inline bool equal_mod_settings(const ModSettings& a,const ModSettings& b)noexcept {
    return a.voice_enabled==b.voice_enabled&&a.strength==b.strength&&a.sensitivity==b.sensitivity&&
        a.smoothing_ms==b.smoothing_ms&&a.hotkey==b.hotkey&&a.voice_source==b.voice_source&&microphone_device_id(a)==microphone_device_id(b);
}
bool valid_mod_settings(const ModSettings&)noexcept;
// Scalar compatibility helpers: endpoint IDs are carried by immutable tickets.
std::uint32_t pack_mod_settings(const ModSettings&)noexcept;
ModSettings unpack_mod_settings(std::uint32_t)noexcept;
enum class ModSettingsStatus:std::uint8_t {Missing,Loaded,Invalid,Unavailable,UnsupportedVersion};
struct ModSettingsLoad {ModSettings settings{};ModSettingsStatus status=ModSettingsStatus::Unavailable;};
ModSettingsLoad read_mod_settings(const std::filesystem::path&)noexcept;
bool write_mod_settings(const std::filesystem::path&,const ModSettings&)noexcept;
class ModSettingsMailbox {
public:
    struct Ticket {ModSettings settings{};std::uint64_t generation=0;};
    using TicketPtr=std::shared_ptr<const Ticket>;
    ModSettingsMailbox()noexcept=default;
    ModSettings snapshot()const noexcept{const auto value=ticket();return value?value->settings:ModSettings{};}
    TicketPtr ticket()const noexcept{return std::atomic_load_explicit(&word,std::memory_order_acquire);}
    bool update(const ModSettings&)noexcept;
    bool accept_loaded(const ModSettings&)noexcept;
private:
    TicketPtr word;
};
}
