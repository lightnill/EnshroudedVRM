#pragma once
#include "voice_clock.hpp"
#include <cstddef>
#include <cstdint>
namespace evrm {
struct VoiceCopyTimings {std::uint64_t validation_ns=0,copy_ns=0,queries=0,copies=0;};
// Same-process owned read only. Bounds/protection checks precede a local SEH
// scope; no global exception handler or generic process-memory read is used.
bool copy_local_voice_bytes(const void*,void*,std::size_t,const VoiceClock* =nullptr,VoiceCopyTimings* =nullptr)noexcept;
// Only for a proven native caller's live borrowed buffer, after its successful
// encoder read. Uses the same bounded local fault scope without OS page queries.
bool copy_borrowed_voice_bytes(const void*,void*,std::size_t,const VoiceClock* =nullptr,VoiceCopyTimings* =nullptr)noexcept;
}
