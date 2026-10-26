#pragma once
#include "retarget.hpp"
#include <memory>

namespace vrm {
// Runtime preference only; imported/authored VRM settings stay immutable.
enum class SpringMotion { Authored, Relaxed };
struct SpringFrame {
    std::uint64_t timestamp_ms=0,source_epoch=0,actor_id=0;
    Matrix model_to_world{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    // Current floating origin minus previous floating origin, in world metres.
    // Rebase stored world tails by subtracting this; centered tails are local.
    std::array<double,3> origin_delta{};
};
struct SpringUpdate {
    bool applied=false,reset=false;
    unsigned steps=0;
    double max_offset=0;
    const char* error=nullptr;
};
// Avatar must remain alive and immutable. CPU-only; owns no game/GPU pointers.
// An optional failure preserves the incoming pose and clears simulation history.
class SpringBones {
public:
    explicit SpringBones(const Avatar&,SpringMotion=SpringMotion::Authored);
    ~SpringBones();
    bool ready() const noexcept;
    const std::string& error() const noexcept;
    std::size_t joint_count() const noexcept;
    SpringUpdate update(RetargetResult&,const SpringFrame&) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Optional simulation allocation must not prevent the base avatar from loading.
std::unique_ptr<SpringBones> make_optional_springs(const Avatar&,SpringMotion=SpringMotion::Authored) noexcept;
} // namespace vrm
