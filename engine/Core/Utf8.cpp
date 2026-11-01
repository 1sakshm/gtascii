#include "Core/Utf8.h"

namespace av {

void appendUtf8(std::string& out, char32_t cp)
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = kReplacementChar;
    }

    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::u32string decodeUtf8(std::string_view utf8)
{
    std::u32string result;
    result.reserve(utf8.size());

    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[i]);

        int length = 0;
        char32_t cp = 0;
        char32_t minValue = 0;
        if (lead < 0x80) {
            result.push_back(lead);
            ++i;
            continue;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2; cp = lead & 0x1F; minValue = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3; cp = lead & 0x0F; minValue = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4; cp = lead & 0x07; minValue = 0x10000;
        } else {
            result.push_back(kReplacementChar);
            ++i;
            continue;
        }

        // Validate continuation bytes one at a time so a truncated or broken
        // sequence becomes a single U+FFFD and decoding resumes at the first
        // byte that did not belong to it.
        std::size_t k = 1;
        for (; k < static_cast<std::size_t>(length); ++k) {
            if (i + k >= utf8.size()) {
                break;
            }
            const auto cont = static_cast<unsigned char>(utf8[i + k]);
            if ((cont & 0xC0) != 0x80) {
                break;
            }
            cp = (cp << 6) | (cont & 0x3F);
        }

        if (k < static_cast<std::size_t>(length)) {
            result.push_back(kReplacementChar);
            i += k;
            continue;
        }

        const bool overlong = cp < minValue;
        const bool outOfRange = cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF);
        result.push_back(overlong || outOfRange ? kReplacementChar : cp);
        i += static_cast<std::size_t>(length);
    }
    return result;
}

} // namespace av
