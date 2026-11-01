#include "TestHarness.h"

#include "Graphics/Color.h"

using av::Color;

TEST_CASE("Color: fromFloat clamps and rounds")
{
    CHECK(Color::fromFloat(1.0f, 0.0f, 0.5f) == Color(255, 0, 128));
    CHECK(Color::fromFloat(2.0f, -1.0f, 0.0f) == Color(255, 0, 0));
}

TEST_CASE("Color: fromHsv primary hues")
{
    CHECK(Color::fromHsv(0.0f, 1.0f, 1.0f) == Color(255, 0, 0));
    CHECK(Color::fromHsv(120.0f, 1.0f, 1.0f) == Color(0, 255, 0));
    CHECK(Color::fromHsv(240.0f, 1.0f, 1.0f) == Color(0, 0, 255));
    CHECK(Color::fromHsv(360.0f, 1.0f, 1.0f) == Color(255, 0, 0));  // wraps
    CHECK(Color::fromHsv(-120.0f, 1.0f, 1.0f) == Color(0, 0, 255)); // negative wraps
    CHECK(Color::fromHsv(77.0f, 0.0f, 1.0f) == Color(255, 255, 255));
    CHECK(Color::fromHsv(77.0f, 1.0f, 0.0f) == Color(0, 0, 0));
}

TEST_CASE("Color: lerp endpoints and midpoint")
{
    const Color a{0, 100, 200};
    const Color b{200, 100, 0};
    CHECK(av::lerp(a, b, 0.0f) == a);
    CHECK(av::lerp(a, b, 1.0f) == b);
    CHECK(av::lerp(a, b, 0.5f) == Color(100, 100, 100));
    CHECK(av::lerp(a, b, 5.0f) == b); // clamped
}

TEST_CASE("Color: scale and addSaturate clamp to 255")
{
    CHECK(av::scale(Color(100, 200, 50), 2.0f) == Color(200, 255, 100));
    CHECK(av::scale(Color(100, 200, 50), 0.0f) == Color(0, 0, 0));
    CHECK(av::scale(Color(100, 200, 50), -1.0f) == Color(0, 0, 0));
    CHECK(av::addSaturate(Color(200, 10, 0), Color(100, 10, 0)) == Color(255, 20, 0));
}

TEST_CASE("Color: quantize keeps black and white, reduces levels")
{
    CHECK(av::quantize(Color(0, 255, 128), 8) == Color(0, 255, 128));
    for (int bits = 1; bits <= 8; ++bits) {
        CHECK(av::quantize(Color(0, 0, 0), bits) == Color(0, 0, 0));
        CHECK(av::quantize(Color(255, 255, 255), bits) == Color(255, 255, 255));
    }
    CHECK(av::quantize(Color(0x7F, 0x80, 0), 1) == Color(0, 255, 0));
    // 4 bits: 16 levels, each a multiple of 17.
    const Color q = av::quantize(Color(0x12, 0x9A, 0xEE), 4);
    CHECK_EQ(q.r % 17, 0);
    CHECK_EQ(q.g % 17, 0);
    CHECK_EQ(q.b % 17, 0);
}

TEST_CASE("Color: luminance range")
{
    CHECK(av::luminance(Color(0, 0, 0)) == 0.0f);
    CHECK(av::luminance(Color(255, 255, 255)) > 0.999f);
    CHECK(av::luminance(Color(0, 255, 0)) > av::luminance(Color(255, 0, 0)));
}
