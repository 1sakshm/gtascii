#include "TestHarness.h"

#include "Core/Utf8.h"

TEST_CASE("Utf8: encodes 1-4 byte sequences")
{
    std::string s;
    av::appendUtf8(s, U'A');
    av::appendUtf8(s, U'é');     // é
    av::appendUtf8(s, U'░');     // ░
    av::appendUtf8(s, U'\U0001F600'); // emoji
    CHECK_EQ(s, std::string("A\xC3\xA9\xE2\x96\x91\xF0\x9F\x98\x80"));
}

TEST_CASE("Utf8: invalid code points encode as replacement")
{
    std::string s;
    av::appendUtf8(s, static_cast<char32_t>(0xD800));
    av::appendUtf8(s, static_cast<char32_t>(0x110000));
    CHECK_EQ(s, std::string("\xEF\xBF\xBD\xEF\xBF\xBD"));
}

TEST_CASE("Utf8: round trip")
{
    const std::u32string original = U" .:░▒▓█\U0001F600z";
    std::string encoded;
    for (char32_t c : original) {
        av::appendUtf8(encoded, c);
    }
    CHECK(av::decodeUtf8(encoded) == original);
}

TEST_CASE("Utf8: malformed input decodes to replacement characters")
{
    CHECK(av::decodeUtf8("\xFF") == std::u32string(1, av::kReplacementChar));
    CHECK(av::decodeUtf8("\xE2\x96") == std::u32string(1, av::kReplacementChar));      // truncated
    CHECK(av::decodeUtf8("\xC0\xAF") == std::u32string(1, av::kReplacementChar));      // overlong
    CHECK(av::decodeUtf8("\xE2x") == (std::u32string{av::kReplacementChar, U'x'}));    // bad continuation
}
