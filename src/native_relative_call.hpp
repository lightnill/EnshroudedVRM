#pragma once
#include <array>
#include <cstdint>
namespace evrm {
// Observes one complete original E8 CALL. The near relay JMPs to a typed
// wrapper, preserving the original caller's stack/return address and ABI.
// Published relays are process-owned; never freed beneath a returning caller.
class NativeRelativeCall {
    std::uintptr_t site_=0,original_=0,wrapper_=0;
    std::array<unsigned char,5> before_{},after_{};
    bool installed_=false;
public:
    bool install(std::uintptr_t,const std::array<unsigned char,5>&,std::uintptr_t,std::uintptr_t)noexcept;
    bool restore()noexcept;
};
}
