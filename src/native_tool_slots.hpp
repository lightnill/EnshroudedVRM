#pragma once
#include "tool_grip.hpp"
#include "native_owner.hpp"
#include "secondary_hand_contact.hpp"
#include <memory>
namespace evrm {
struct NativeVisualSlots {
    std::uint64_t active=0;
    std::array<std::int64_t,3> origin{};
    std::array<std::array<float,12>,36> matrices{}; // packed column-major 3x4
};
static_assert(sizeof(NativeVisualSlots)==1760,"pinned client visual slot ABI");
struct NativeToolSlotEdits {
    // Keep the established hand/palm pair order; slot12 is the proven shield
    // attachment. Slot13 has no validated correction and remains native.
    static constexpr std::array<unsigned,5> slots{8,9,14,15,12};
    std::array<bool,slots.size()> enabled{};
    std::array<std::array<float,12>,slots.size()> matrices{};
};
bool make_native_tool_slot_edits(const NativeVisualSlots&,
    const std::array<float,4>& actor_quaternion,const std::array<float,3>& actor_scale,
    const std::array<ToolHandCorrection,2>&,std::uint64_t sampled,std::uint64_t now,
    NativeToolSlotEdits&) noexcept;
struct NativeToolSlotTarget {
    std::uint64_t client_slots=0,render_transform=0,system_function_slot=0,original_function=0,publish_call_site=0;
    std::uint32_t entity_id=0;
    // Optional local equipment state. Absence disables secondary contact only.
    std::uint64_t equipment_animation_sets=0,client_ranged_weapon=0;
    std::vector<NativeOwnerGuard> guards;
};
// Consumes an already fully captured/revalidated owner; never broad-scans memory.
bool resolve_native_tool_slot_target(const NativeOwnerMemory&,const NativeOwnerCandidate&,
                                    NativeToolSlotTarget&,std::string&) noexcept;
struct NativeAvatarPublication;
SecondaryHandContactMode read_native_hand_contact_mode(const NativeAvatarPublication&,
    std::uint64_t now,std::uint64_t& owned_slots,std::uint64_t* observed_milliseconds=nullptr) noexcept;
class NativeToolSlots {
public:
    NativeToolSlots() noexcept;
    ~NativeToolSlots();
    void publish(const NativeOwnerMemory&,const NativeOwnerCandidate&,
                 std::shared_ptr<const NativeAvatarPublication>,std::uint64_t source_generation) noexcept;
    // May update only an existing, unexpired submission authority.
    void advance(const NativeOwnerMemory&,std::shared_ptr<const NativeAvatarPublication>,
                 std::uint64_t source_generation) noexcept;
    void clear() noexcept;
    void disable() noexcept; // transient gate: keep the immutable forwarding relay
private:
    bool enabled_=false;
    NativeToolSlotTarget target_;
    std::array<std::uint64_t,8> identity_{};
    unsigned advance_notices_=0;
};
#ifdef EVRM_NATIVE_TOOL_SLOTS_TESTING
bool test_install_native_tool_slots(const NativeToolSlotTarget&,std::shared_ptr<const NativeAvatarPublication>) noexcept;
void test_clear_native_tool_slots() noexcept;
void test_invoke_native_tool_slot_success(void*) noexcept;
void test_with_native_tool_snapshot_lock(void(*)(void*),void*);
void test_observe_native_tool_slot_writes(void(*)(std::uint64_t,std::size_t)) noexcept;
SecondaryHandContactMode test_read_native_hand_contact_mode(const NativeOwnerMemory&,
    const NativeAvatarPublication&,std::uint64_t now,std::uint64_t& owned_slots,
    std::uint64_t* observed_milliseconds=nullptr) noexcept;
#endif
}
