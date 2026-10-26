#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace evrm {
struct NativeOwnerMemory {
    virtual ~NativeOwnerMemory() = default;
    virtual bool read(std::uint64_t, void*, std::size_t) const noexcept = 0;
    virtual std::uint64_t milliseconds() const noexcept = 0;
};
struct NativeOwnerImage {
    std::uint64_t base=0, size=0;
    std::uint32_t timestamp=0;
    std::array<std::uint8_t,32> sha256{};
};
struct NativeOwnerLimits {
    std::size_t max_read=65536, max_bytes=64*1024*1024, max_calls=100000;
    std::uint64_t max_milliseconds=15000;
};
enum class NativeOwnerObservationPhase:std::uint8_t {
    none,adapter,image,owners,registry,census,equipment,services,models,capture_verify,submission_verify
};
enum class NativeOwnerFailureLocation:std::uint8_t {
    none,validation,precheck,range,budget,read,postcheck,compare,finalcheck,internal
};
// Output-only progress, never ownership authority. No addresses or allocations.
// Completed counts require a successful full read. During verification, ordinal
// equal to guard_total denotes the final check/completion. Elapsed uses only
// existing clock observations; clock_regressed means a value below phase start.
struct NativeOwnerObservationStats {
    NativeOwnerObservationPhase phase=NativeOwnerObservationPhase::none;
    NativeOwnerFailureLocation failure_location=NativeOwnerFailureLocation::none;
    std::size_t attempted_calls=0,completed_calls=0,attempted_bytes=0,completed_bytes=0;
    std::size_t guard_ordinal=0,guard_total=0;
    NativeOwnerObservationPhase guard_capture_phase=NativeOwnerObservationPhase::none;
    std::size_t guard_span_bytes=0,guard_difference_offset=0;
    std::uint64_t elapsed_milliseconds=0;
    bool clock_regressed=false;
};
struct NativeOwnerGuard {
    std::uint64_t address=0;
    std::vector<std::uint8_t> bytes;
    // Diagnostic provenance only. It never affects guard acceptance.
    NativeOwnerObservationPhase capture_phase=NativeOwnerObservationPhase::none;
};
struct NativeOwnerModel {
    std::uint32_t equipment_slot=0, handle=0, index=0, generation=0;
    std::uint64_t record=0;
};
// Owned observations, never an atomic lifetime lease or proof of GPU mapping.
struct NativeOwnerCandidate {
    NativeOwnerImage image;
    std::uint64_t root=0, client=0, controller=0, main=0, execution=0, registry=0, entity_manager=0;
    std::uint64_t entity=0, entity_layout=0, entity_storage=0, equipment=0;
    std::uint32_t entity_id=0, entity_generation=0, entity_slot=0, entity_row=0;
    std::uint32_t input_registration=0, equipment_registration=0;
    std::uint64_t render_scene=0, model_manager=0, writer=0, resource_object=0;
    std::uint64_t native_buffer=0, native_buffer_bytes=0;
    std::uint32_t instance_capacity=0, instance_extra=0;
    std::vector<NativeOwnerModel> models;
    std::vector<NativeOwnerGuard> guards;
    std::size_t read_calls=0, read_bytes=0;
    std::uint64_t elapsed_milliseconds=0;
};
bool capture_native_owner(const NativeOwnerMemory&, const NativeOwnerImage&, NativeOwnerCandidate&,
                          std::string&, const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;
// Clears candidate on failure. Checks every retained owned guard; no payload reads.
bool revalidate_native_owner(const NativeOwnerMemory&, NativeOwnerCandidate&, std::string&,
                             const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;

// Externally synchronized. Retains a private deep copy, never an ownership
// lease: all guards are freshly read before reuse and again at submission.
class NativeOwnerCache {
    NativeOwnerCandidate cached_;
    const NativeOwnerMemory* source_=nullptr;
public:
    bool capture(const NativeOwnerMemory&,const NativeOwnerImage&,NativeOwnerCandidate&,std::string&,
                 const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr,bool* reused=nullptr) noexcept;
    void clear() noexcept {cached_={};source_=nullptr;}
};
}
