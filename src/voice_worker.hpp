#pragma once
#include "voice_analyser.hpp"
#include "voice_clock.hpp"
#include <thread>

namespace evrm {
struct VoiceTimeSource {std::uint64_t (*now_us)(void*)noexcept=nullptr;void* context=nullptr;};
struct VoiceWorkerStats {std::uint64_t submitted=0,processed=0,discarded=0,dropped=0,resets=0,epoch=0;bool enabled=false;
    std::uint64_t analysis_nanoseconds=0;VoiceAnalysisDiagnostics diagnostics;};
// The producer never waits; a contended producer drops and invalidates its epoch.
// Fixture owners must stop producers before destruction. The game owner persists
// for the process lifetime independently of Vulkan devices.
class VoiceAnalysisWorker {
public:
    explicit VoiceAnalysisWorker(const VoiceProfiles* profiles=nullptr,VoiceTimeSource time={})noexcept;
    ~VoiceAnalysisWorker();
    VoiceAnalysisWorker(const VoiceAnalysisWorker&)=delete;
    VoiceAnalysisWorker& operator=(const VoiceAnalysisWorker&)=delete;
    bool ready()const noexcept{return ready_;}
    const char* diagnostic()const noexcept{return analyser_.diagnostic();}
    void set_enabled(bool)noexcept;
    void set_tuning(VoiceAnalysisTuning)noexcept;
    std::uint64_t begin_capture(std::uint64_t source_id,std::uint64_t timestamp_us)noexcept;
    bool submit(const std::array<float,480>&,std::uint64_t source_id,std::uint64_t timestamp_us)noexcept;
    bool submit(const std::array<float,480>&,std::uint64_t source_id,std::uint64_t timestamp_us,std::uint64_t token)noexcept;
    MouthSnapshot snapshot(std::uint64_t now_us=0)const noexcept;
    VoiceWorkerStats stats()const noexcept;
private:
    void run()noexcept;
    void invalidate()noexcept;
    std::uint64_t now_us()const noexcept;
    VoiceBlockQueue queue_;
    MouthSnapshotExchange published_;
    VoiceVowelAnalyser analyser_;
    VoiceClock clock_;
    VoiceTimeSource time_;
    void* event_=nullptr;
    std::thread thread_;
    bool ready_=false;
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint64_t> mode_{2},tuning_{0};
    std::atomic_flag producer_=ATOMIC_FLAG_INIT;
    // The following capture metadata is accessed only while producer_ is owned.
    std::uint64_t source_epoch_=0,source_id_=0,capture_timestamp_=0,capture_sequence_=0;
    std::atomic<std::uint64_t> submitted_{0},processed_{0},discarded_{0},dropped_{0},resets_{0};
    std::atomic<std::uint64_t> diagnostics_{0},analysis_nanoseconds_{0};
};
}
