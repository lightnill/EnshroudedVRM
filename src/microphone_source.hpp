#pragma once
#include "microphone_backend.hpp"
#include "voice_worker.hpp"
namespace evrm {
class MicrophoneSource {
public:
    explicit MicrophoneSource(MicrophoneBackendFactory factory=nullptr,void* context=nullptr,VoiceTimeSource time={},unsigned refresh_ms=2000)noexcept;
    ~MicrophoneSource();
    MicrophoneSource(const MicrophoneSource&)=delete;
    MicrophoneSource& operator=(const MicrophoneSource&)=delete;
    bool ready()const noexcept;
    bool request(bool enabled,VoiceAnalysisTuning,MicrophoneDeviceId)noexcept;
    void refresh()noexcept;
    MicrophoneState state()const noexcept;
    std::shared_ptr<const MicrophoneDeviceList> devices()const noexcept;
    MouthSnapshot snapshot()const noexcept;
    VoiceWorkerStats stats()const noexcept;
    void poll(std::uint64_t now_ms)noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
