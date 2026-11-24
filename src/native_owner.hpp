#pragma once
#include "native_player_identity.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
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
    std::uint32_t steam_build_id=0; // observed installed build; zero is unavailable
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
// Geometry only: no current bytes, read results, budget or ownership permission.
// A sealed bank can share this layout; each verification owns its fresh scratch.
struct NativeOwnerReadBatchPlan {
    static constexpr std::uint16_t absent=65535;
    struct Group {std::uint64_t address=0;std::size_t size=0,offset=0;};
    struct Request {std::uint64_t address=0;std::size_t ordinal=0;};
    std::vector<std::uint16_t> lookup;
    std::vector<Group> groups;
    std::size_t exact_bytes=0,data_bytes=0;
    bool valid=false;
    // ordered must contain every guard in ascending address order. The bank's
    // existing address index supplies this without another sort.
    template<class Entries>
    static NativeOwnerReadBatchPlan prepare(const std::vector<NativeOwnerGuard>& saved,
                                            std::size_t max_read,const Entries& ordered){
        if(saved.size()<64||saved.size()>16384)return {};
        NativeOwnerReadBatchPlan plan;
        for(const auto& guard:saved){const auto size=guard.bytes.size();
            // A malformed later guard must not preempt an earlier mismatch.
            if(!size||size>max_read||guard.address<0x10000||guard.address>=0x800000000000ULL||
               size>0x800000000000ULL-guard.address||size>64*1024*1024-plan.exact_bytes)return {};
            plan.exact_bytes+=size;
        }
        plan.lookup.assign(saved.size(),absent);plan.groups.reserve(512);
        const auto window=std::min<std::size_t>(max_read,65536);
        std::size_t first=0,count=0;std::uint64_t start=0,end=0;
        auto finish=[&](std::size_t last){
            if(count>1&&plan.groups.size()<512){
                const auto group=static_cast<std::uint16_t>(plan.groups.size());const auto size=std::size_t(end-start);
                plan.groups.push_back({start,size,plan.data_bytes});plan.data_bytes+=size;
                for(auto i=first;i<last;++i){const auto& entry=ordered[i];const auto& guard=saved[entry.ordinal];
                    if(guard.bytes.size()<=512&&guard.address/65536==(guard.address+guard.bytes.size()-1)/65536)plan.lookup[entry.ordinal]=group;
                }
            }
        };
        for(std::size_t i=0;i<ordered.size();++i){const auto& entry=ordered[i];const auto& guard=saved[entry.ordinal];
            const auto tail=guard.address+guard.bytes.size();
            if(guard.bytes.size()>512||guard.address/65536!=(tail-1)/65536)continue;
            if(count&&(guard.address/65536!=start/65536||std::max(end,tail)-start>window)){finish(i);count=0;}
            if(!count){first=i;start=guard.address;end=tail;}else end=std::max(end,tail);
            ++count;
        }
        finish(ordered.size());plan.valid=true;return plan;
    }
};
// Capture transfers its local guards once. Consumers share const storage;
// copying a candidate never copies thousands of individually allocated spans.
class NativeOwnerGuards {
    using Storage=std::vector<NativeOwnerGuard>;
    struct AddressIndex {
        struct Entry {std::uint64_t address=0,prefix_end=0;std::size_t ordinal=0;};
        std::vector<Entry> entries;
        NativeOwnerReadBatchPlan batches;
    };
    std::shared_ptr<const Storage> saved_;
    std::shared_ptr<const AddressIndex> index_;
public:
    NativeOwnerGuards()=default;
    NativeOwnerGuards(Storage guards):saved_(guards.empty()?nullptr:std::make_shared<const Storage>(std::move(guards))){
        if(!saved_)return;
        try{
            auto index=std::make_shared<AddressIndex>();index->entries.reserve(saved_->size());
            constexpr auto maximum=(std::numeric_limits<std::uint64_t>::max)();
            for(std::size_t i=0;i<saved_->size();++i){const auto& guard=(*saved_)[i];if(guard.bytes.empty())continue;
                const auto tail=static_cast<std::uint64_t>(guard.bytes.size()-1);
                index->entries.push_back({guard.address,tail>maximum-guard.address?maximum:guard.address+tail,i});
            }
            std::sort(index->entries.begin(),index->entries.end(),[](const auto& a,const auto& b){return a.address<b.address;});
            std::uint64_t end=0;for(auto& entry:index->entries){if(entry.prefix_end>end)end=entry.prefix_end;entry.prefix_end=end;}
            // Publish the completed lookup before optional batch allocations.
            // Failure to plan must retain both the owned bank and its index.
            index_=index;
            index->batches=NativeOwnerReadBatchPlan::prepare(*saved_,65536,index->entries);
        }catch(...){/* Optional acceleration never changes the owned byte bank. */}
    }
    const Storage& view()const noexcept {static const Storage empty;return saved_?*saved_:empty;}
    bool empty()const noexcept{return !saved_||saved_->empty();}
    std::size_t size()const noexcept{return saved_?saved_->size():0;}
    auto begin()const noexcept{return view().begin();}
    auto end()const noexcept{return view().end();}
    const NativeOwnerGuard& operator[](std::size_t index)const noexcept{return view()[index];}
    const NativeOwnerGuard& back()const noexcept{return view().back();}
    const NativeOwnerReadBatchPlan* read_batch_plan(std::size_t max_read)const noexcept{
        return max_read==65536&&index_&&index_->batches.valid?&index_->batches:nullptr;
    }
    // First original guard covering all requested owned bytes. This lookup
    // grants no live address validity; zero-sized requests have no result.
    const NativeOwnerGuard* covering(std::uint64_t address,std::size_t count)const noexcept{
        if(!count||!saved_)return nullptr;
        if(!index_){for(const auto& guard:*saved_)if(address>=guard.address&&count<=guard.bytes.size()&&address-guard.address<=guard.bytes.size()-count)return &guard;return nullptr;}
        // Saturation is only an index bound. The original subtraction predicate
        // remains decisive even for malformed/overflow owned metadata.
        constexpr auto maximum=(std::numeric_limits<std::uint64_t>::max)();const auto tail=static_cast<std::uint64_t>(count-1);
        const auto end=tail>maximum-address?maximum:address+tail;
        auto at=std::lower_bound(index_->entries.begin(),index_->entries.end(),end,[](const auto& entry,std::uint64_t value){return entry.prefix_end<value;});
        const NativeOwnerGuard* result=nullptr;std::size_t first=(std::numeric_limits<std::size_t>::max)();
        for(;at!=index_->entries.end()&&at->address<=address;++at){const auto& guard=(*saved_)[at->ordinal];
            if(at->ordinal<first&&count<=guard.bytes.size()&&address-guard.address<=guard.bytes.size()-count){result=&guard;first=at->ordinal;if(!first)break;}
        }return result;
    }
};
struct NativeOwnerModel {
    std::uint32_t equipment_slot=0, handle=0, index=0, generation=0;
    std::uint64_t record=0;
};
struct NativePlayerExclusionProof;
// Owned observations, never an atomic lifetime lease or proof of GPU mapping.
struct NativeOwnerCandidate {
    NativeOwnerImage image;
    std::uint64_t root=0, client=0, controller=0, main=0, execution=0, registry=0, entity_manager=0;
    std::uint64_t local_player_data=0;
    std::uint64_t account=0,player_infos=0,player_record=0;
    std::uint32_t connection=0;
    std::uint64_t entity=0, entity_layout=0, entity_storage=0, equipment=0;
    std::uint32_t entity_id=0, entity_generation=0, entity_slot=0, entity_row=0;
    std::uint32_t input_registration=0, equipment_registration=0;
    std::uint64_t render_scene=0, model_manager=0, writer=0, resource_object=0;
    std::uint64_t native_buffer=0, native_buffer_bytes=0;
    std::uint32_t instance_capacity=0, instance_extra=0;
    std::vector<NativeOwnerModel> models;
    NativeOwnerGuards guards;
    // Immutable captured metadata, never a current ownership permission.
    std::shared_ptr<const NativePlayerExclusionProof> exclusions;
    std::size_t read_calls=0, read_bytes=0;
    std::uint64_t elapsed_milliseconds=0;
};
// Availability is an observation only. Unavailable rows carry no owner candidate.
enum class NativePlayerOwnerStatus { Ready, EntityAbsent, AssociationUnavailable, OwnerUnavailable };
struct NativePlayerOwnerResult {
    NativePlayerIdentity identity;
    NativePlayerOwnerStatus status=NativePlayerOwnerStatus::AssociationUnavailable;
    NativeOwnerCandidate candidate;
    NativeOwnerObservationStats observation;
};
struct NativePlayerOwnerBatch {
    bool valid=false;
    std::vector<NativePlayerOwnerResult> players;
    // Legacy capture shares the full bank. Independent capture retains only
    // shared exclusion evidence here; each Ready candidate adds its own bank.
    // Neither form grants permission without fresh candidate verification.
    NativeOwnerGuards proof;
    std::shared_ptr<const NativePlayerExclusionProof> exclusions;
};
// Reconcile a previously verified positive selection with fresh shared claims.
// No live reads or lifetime permission; ordinary verification remains required.
bool native_player_owner_matches_exclusions(const NativeOwnerCandidate&,const NativePlayerExclusionProof&) noexcept;
// Classifies an actual submission read failure overlapping retained shared
// evidence, including aliases in the positive tail. Timing/budget stay local.
bool native_player_owner_shared_read_failed(const NativeOwnerCandidate&,const NativeOwnerObservationStats&) noexcept;
// Only the actual changed byte covered by shared evidence requests a refresh;
// a nonshared byte in a wider overlapping positive guard remains local.
bool native_player_owner_shared_compare_failed(const NativeOwnerCandidate&,const NativeOwnerObservationStats&) noexcept;
bool capture_native_players(const NativeOwnerMemory&,const NativeOwnerImage&,NativePlayerRoster&,std::string&,const NativeOwnerLimits& = {}) noexcept;
bool capture_native_player_owner(const NativeOwnerMemory&,const NativeOwnerImage&,const NativePlayerIdentity&,NativeOwnerCandidate&,
                                 std::string&,const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;
bool capture_native_owner(const NativeOwnerMemory&, const NativeOwnerImage&, NativeOwnerCandidate&,
                          std::string&, const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;
// Clears candidate on failure. Checks every retained owned guard; no payload reads.
bool revalidate_native_player_owners(const NativeOwnerMemory&,std::vector<NativeOwnerCandidate>&,std::string&,
                                     const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr,
                                     std::vector<std::uint8_t>* read_workspace=nullptr) noexcept;
bool revalidate_native_owner(const NativeOwnerMemory&, NativeOwnerCandidate&, std::string&,
                             const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;

// Externally synchronized. Retains owned immutable guards, never an ownership
// lease: all guards are freshly read before reuse and again at submission.
struct NativeOwnerCachePrefix;
class NativeOwnerCache {
    NativeOwnerCandidate cached_;
    NativeOwnerCandidate player_prefix_;
    std::shared_ptr<const NativeOwnerCachePrefix> player_decoded_;
    const NativeOwnerMemory* player_source_=nullptr;
    // Externally synchronized allocation space only. No read result or
    // ownership acceptance survives a verification in this buffer.
    std::vector<std::uint8_t> player_read_scratch_;
    // Decoded metadata only; usable after this call verifies its whole prefix.
    std::shared_ptr<const NativeOwnerCachePrefix> prefix_;
    const NativeOwnerMemory* source_=nullptr;
public:
    bool capture(const NativeOwnerMemory&,const NativeOwnerImage&,NativeOwnerCandidate&,std::string&,
                 const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr,bool* reused=nullptr) noexcept;
    bool players(const NativeOwnerMemory&,const NativeOwnerImage&,NativePlayerRoster&,std::string&,const NativeOwnerLimits& = {}) noexcept;
    // One capped 40ms proof for the whole set; output order follows requested identities.
    // Any stale, duplicate or ambiguous association clears the entire output.
    bool capture_players(const NativeOwnerMemory&,const NativeOwnerImage&,const std::vector<NativePlayerIdentity>&,
                         std::vector<NativeOwnerCandidate>&,std::string&,const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;
    NativePlayerOwnerBatch capture_available_players(const NativeOwnerMemory&,const NativeOwnerImage&,
                         const std::vector<NativePlayerIdentity>&,std::string&,const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;
    // Shared exclusion evidence plus independently budgeted positive rows.
    // Each row charges shared work and its own work, never another row's work.
    // stats counts all physical attempts, including failed positive rows.
    NativePlayerOwnerBatch capture_independent_players(const NativeOwnerMemory&,const NativeOwnerImage&,
                         const std::vector<NativePlayerIdentity>&,std::string&,const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;
    bool capture_player(const NativeOwnerMemory&,const NativeOwnerImage&,const NativePlayerIdentity&,NativeOwnerCandidate&,std::string&,
                        const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) noexcept;
    void clear() noexcept {cached_={};prefix_.reset();source_=nullptr;player_prefix_={};player_decoded_.reset();player_source_=nullptr;std::vector<std::uint8_t>{}.swap(player_read_scratch_);}
};
}
