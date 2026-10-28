#pragma once
#include "tool_grip.hpp"
#include <memory>
namespace evrm {
enum class SecondaryHandContactMode { None, TwoHanded, BowDrawn };
inline SecondaryHandContactMode classify_secondary_hand_contact(
    std::array<std::uint8_t,2> equipment,std::array<std::uint8_t,2> ranged) noexcept {
    // The second equipment byte remembers an unused off-hand animation set
    // (0 after load,1 after bow,3 after shield). It is not this item's identity.
    if(equipment[0]==4&&ranged==std::array<std::uint8_t,2>{0,0})return SecondaryHandContactMode::TwoHanded;
    if(equipment[0]==5&&ranged==std::array<std::uint8_t,2>{2,0})return SecondaryHandContactMode::BowDrawn;
    return SecondaryHandContactMode::None;
}
// Do not apply a newly observed CPU state to an older buffered GPU pose.
struct SecondaryContactActivation {
    SecondaryHandContactMode current=SecondaryHandContactMode::None;
    std::uint64_t since=0,owner=0,epoch=0;
    SecondaryHandContactMode observe(SecondaryHandContactMode mode,std::uint64_t sampled,
        std::uint64_t now,std::uint64_t owned_slots,std::uint64_t source_epoch) noexcept {
        if(mode==SecondaryHandContactMode::None||!sampled||now<sampled||!owned_slots||!source_epoch||now<since){*this={};return SecondaryHandContactMode::None;}
        if(current!=mode||owner!=owned_slots||epoch!=source_epoch){current=mode;since=now;owner=owned_slots;epoch=source_epoch;}
        return sampled>=since?mode:SecondaryHandContactMode::None;
    }
};
struct SecondaryHandContactResult {bool applied=false,clamped=false;double residual=0;};
// Metadata is compiled once. update only rotates the supporting arm's subtree;
// failure is atomic, and neither native memory nor the Avatar asset is changed.
class SecondaryHandContact {
public:
    explicit SecondaryHandContact(const vrm::Avatar&) noexcept;
    ~SecondaryHandContact();
    bool ready()const noexcept;
    SecondaryHandContactResult update(const vrm::SourcePose&,
        const std::array<ToolHandCorrection,2>&,double height_scale,double basis_xz,
        SecondaryHandContactMode,vrm::RetargetResult&) noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
