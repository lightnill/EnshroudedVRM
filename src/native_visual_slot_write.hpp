#pragma once
#include <cstddef>
#include <cstdint>
namespace evrm {
// Copies only one approved48/96byte visual matrix group. Ownership, native
// success and pose authority remain the caller's responsibility.
bool write_native_visual_slot_data(std::uint64_t destination,const float* source,
                                  std::size_t bytes) noexcept;
}
