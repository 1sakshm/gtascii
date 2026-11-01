#include "TestHarness.h"

#include "Graphics/CharRamp.h"

#include <limits>
#include <stdexcept>

TEST_CASE("CharRamp: extremes map to first and last glyph")
{
    const av::CharRamp ramp("test", " .:-=+*#%@");
    CHECK_EQ(ramp.size(), std::size_t{10});
    CHECK_EQ(ramp.pick(0.0f), U' ');
    CHECK_EQ(ramp.pick(1.0f), U'@');
    CHECK_EQ(ramp.pick(-5.0f), U' ');
    CHECK_EQ(ramp.pick(5.0f), U'@');
}

TEST_CASE("CharRamp: equal-width buckets")
{
    const av::CharRamp ramp("test", "abcd");
    CHECK_EQ(ramp.pick(0.24f), U'a');
    CHECK_EQ(ramp.pick(0.26f), U'b');
    CHECK_EQ(ramp.pick(0.51f), U'c');
    CHECK_EQ(ramp.pick(0.99f), U'd');
}

TEST_CASE("CharRamp: monotonic in intensity")
{
    const av::CharRamp ramp("test", " .:-=+*#%@");
    std::size_t lastIndex = 0;
    for (int i = 0; i <= 100; ++i) {
        const char32_t c = ramp.pick(static_cast<float>(i) / 100.0f);
        std::size_t idx = 0;
        while (ramp[idx] != c) {
            ++idx;
        }
        CHECK(idx >= lastIndex);
        lastIndex = idx;
    }
}

TEST_CASE("CharRamp: NaN intensity is safe")
{
    const av::CharRamp ramp("test", "ab");
    CHECK_EQ(ramp.pick(std::numeric_limits<float>::quiet_NaN()), U'a');
}

TEST_CASE("CharRamp: UTF-8 glyphs and built-ins")
{
    const av::CharRamp* blocks = av::findRamp("blocks");
    CHECK(blocks != nullptr);
    if (blocks) {
        CHECK_EQ(blocks->size(), std::size_t{5});
        CHECK_EQ(blocks->pick(1.0f), U'█');
    }
    CHECK(av::findRamp("standard") != nullptr);
    CHECK(av::findRamp("detailed") != nullptr);
    CHECK(av::findRamp("minimal") != nullptr);
    CHECK(av::findRamp("nope") == nullptr);
}

TEST_CASE("CharRamp: empty ramp throws")
{
    bool threw = false;
    try {
        av::CharRamp ramp("empty", "");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}
