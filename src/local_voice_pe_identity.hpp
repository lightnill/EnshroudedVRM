#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace evrm::detail {
inline bool voice_pe_headers_match(const IMAGE_NT_HEADERS64& disk,const IMAGE_NT_HEADERS64& loaded,std::uintptr_t actual_base)noexcept {
    if(!actual_base||(loaded.OptionalHeader.ImageBase!=disk.OptionalHeader.ImageBase&&
        loaded.OptionalHeader.ImageBase!=actual_base))return false;
    // Match the existing native-owner guard: normalize only a trusted load base
    // in this local copy. File hashes, other header bytes and code stay exact.
    auto normalized=loaded;
    normalized.OptionalHeader.ImageBase=disk.OptionalHeader.ImageBase;
    return std::memcmp(&disk,&normalized,sizeof disk)==0;
}
}
