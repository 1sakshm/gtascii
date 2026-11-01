#pragma once

#include <string>
#include <string_view>

namespace av {

inline constexpr char32_t kReplacementChar = U'�';

// Appends the UTF-8 encoding of a code point. Invalid code points
// (surrogates, > U+10FFFF) are encoded as U+FFFD.
void appendUtf8(std::string& out, char32_t codePoint);

// Decodes UTF-8 into code points. Malformed sequences decode to U+FFFD.
std::u32string decodeUtf8(std::string_view utf8);

} // namespace av
