#pragma once
#include <cstdint>
#include <vector>
namespace evrm {
// Private in-memory identity. Never serialize into diagnostics or packages.
struct NativePlayerIdentity {
    std::uint64_t account=0;
    std::uint32_t entity_id=0,connection=0;
    bool local=false;
};
struct NativePlayerRoster {
    std::vector<NativePlayerIdentity> players;
    std::uint64_t observed_milliseconds=0;
};
}
