#pragma once
#include "native_owner.hpp"
#include "native_menu_owner.hpp"
#include <filesystem>
#include <mutex>

namespace evrm {
// Current process only. Exact main-executable attestation is cached per object.
// Unsupported hosts can use read() for ordinary bounded local reads; owner
// capture/revalidation always decline until private main attestation succeeds.
class NativeOwnerProcess final:public NativeOwnerMemory {
public:
    NativeOwnerProcess() noexcept;
    bool read(std::uint64_t,void*,std::size_t) const noexcept override;
    std::uint64_t milliseconds() const noexcept override;
    const NativeOwnerImage& image() const noexcept{return image_;}
    bool supported() const noexcept{return supported_;}
    const std::string& reason() const noexcept{return reason_;}
    bool capture(NativeOwnerCandidate&,std::string&,const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) const noexcept;
    bool revalidate(NativeOwnerCandidate&,std::string&,const NativeOwnerLimits& = {},NativeOwnerObservationStats* stats=nullptr) const noexcept;
    bool capture_menu(NativeMenuOwnerCandidate&,std::string&,const NativeOwnerLimits& = {}) const noexcept;
    bool revalidate_menu(NativeMenuOwnerCandidate&,std::string&,const NativeOwnerLimits& = {}) const noexcept;
private:
    NativeOwnerImage image_{};
    std::string reason_;
    bool supported_=false;
    mutable std::mutex cache_mutex_;
    mutable NativeOwnerCache cache_;
};
namespace detail {
// Pure comparison seam; it grants no loaded-image/process authority. Production
// supplies actual_base privately from GetModuleHandleW(nullptr).
struct NativeOwnerHeaderBytes {
    std::array<std::uint8_t,64> dos{};
    std::array<std::uint8_t,264> nt{};
};
bool match_native_owner_headers(const NativeOwnerHeaderBytes& disk,const NativeOwnerHeaderBytes& loaded,
                                std::uint64_t actual_base,std::string&) noexcept;
// Read-only disk helper for focused tests. On success base stays zero. It
// cannot attest a loaded module or alter NativeOwnerProcess's supported state.
bool verify_native_owner_executable(const std::filesystem::path&,NativeOwnerImage& header_only,
                                    std::string&) noexcept;
}
}
