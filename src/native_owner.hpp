#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace evrm {
struct NativeOwnerMemory {
    virtual ~NativeOwnerMemory() = default;
    virtual bool read(std::uint64_t, void*, std::size_t) const noexcept = 0;
    virtual std::uint64_t milliseconds() const noexcept = 0;
    // Presentation/pose timestamps keep the adapter's original clock above.
    // Elapsed work may use a separate monotonic epoch; never publish this value
    // as a pose timestamp. Existing adapters keep their original behavior.
    virtual std::uint64_t work_milliseconds() const noexcept {return milliseconds();}
    // Optional current-pass read-ahead. Reserve worst-case physical requests,
    // including exact-read fallback; unspecified adapters remain exact-only.
    virtual bool permit_read_ahead(std::size_t,std::size_t) const noexcept {return false;}
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
// Capture transfers its local guards once. Consumers share const storage;
// copying a candidate never copies thousands of individually allocated spans.
class NativeOwnerGuards {
    using Storage=std::vector<NativeOwnerGuard>;
    std::shared_ptr<const Storage> saved_;
public:
    NativeOwnerGuards()=default;
    NativeOwnerGuards(Storage guards):saved_(guards.empty()?nullptr:std::make_shared<const Storage>(std::move(guards))){}
    const Storage& view()const noexcept {static const Storage empty;return saved_?*saved_:empty;}
    bool empty()const noexcept{return !saved_||saved_->empty();}
    std::size_t size()const noexcept{return saved_?saved_->size():0;}
    auto begin()const noexcept{return view().begin();}
    auto end()const noexcept{return view().end();}
    const NativeOwnerGuard& operator[](std::size_t index)const noexcept{return view()[index];}
    const NativeOwnerGuard& back()const noexcept{return view().back();}
};
struct NativeOwnerModel {
    std::uint32_t equipment_slot=0, handle=0, index=0, generation=0;
    std::uint64_t record=0;
};
// Owned observations, never an atomic lifetime lease or proof of GPU mapping.
struct NativeOwnerCandidate {
    NativeOwnerImage image;
    std::uint64_t root=0, client=0, controller=0, main=0, execution=0, registry=0, entity_manager=0;
    std::uint64_t local_player_data=0;
    std::uint64_t entity=0, entity_layout=0, entity_storage=0, equipment=0;
    std::uint32_t entity_id=0, entity_generation=0, entity_slot=0, entity_row=0;
    std::uint32_t input_registration=0, equipment_registration=0;
    std::uint64_t render_scene=0, model_manager=0, writer=0, resource_object=0;
    std::uint64_t native_buffer=0, native_buffer_bytes=0;
    std::uint32_t instance_capacity=0, instance_extra=0;
    std::vector<NativeOwnerModel> models;
    NativeOwnerGuards guards;
    std::size_t read_calls=0, read_bytes=0;
    std::uint64_t elapsed_milliseconds=0;
};
bool capture_native_owner(const NativeOwnerMemory&, const NativeOwnerImage&, NativeOwnerCandidate&,
                          std::string&, const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;
// Clears candidate on failure. Checks every retained owned guard; no payload reads.
bool revalidate_native_owner(const NativeOwnerMemory&, NativeOwnerCandidate&, std::string&,
                             const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;

// Externally synchronized. Retains owned immutable guards, never an ownership
// lease: all guards are freshly read before reuse and again at submission.
struct NativeOwnerCachePrefix;
class NativeOwnerCache {
    NativeOwnerCandidate cached_;
    // Decoded metadata only; usable after this call verifies its whole prefix.
    std::shared_ptr<const NativeOwnerCachePrefix> prefix_;
    const NativeOwnerMemory* source_=nullptr;
public:
    bool capture(const NativeOwnerMemory&,const NativeOwnerImage&,NativeOwnerCandidate&,std::string&,
                 const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr,bool* reused=nullptr) noexcept;
    void clear() noexcept {cached_={};prefix_.reset();source_=nullptr;}
};
}
