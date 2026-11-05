#pragma once
#include <string_view>

namespace enshrouded::vrm::bootstrap_view {
// Bounded scalar operations keep attach independent of the STL's general
// vector-algorithm module and its runtime initialization requirements.
inline std::size_t find(std::wstring_view value, wchar_t character, std::size_t start=0) noexcept {
    for (auto index=start; index<value.size(); ++index) if (value[index]==character) return index;
    return std::wstring_view::npos;
}
inline std::size_t rfind(std::wstring_view value, wchar_t character) noexcept {
    for (auto index=value.size(); index; --index) if (value[index-1]==character) return index-1;
    return std::wstring_view::npos;
}
inline std::wstring_view bounded_z(const wchar_t* value, std::size_t limit) noexcept {
    if (!value) return {};
    for (std::size_t length=0; length<limit; ++length) if (!value[length]) return {value,length};
    return {};
}
template<std::size_t Size> constexpr std::wstring_view literal(const wchar_t (&value)[Size]) noexcept {
    return {value,Size-1};
}
}
