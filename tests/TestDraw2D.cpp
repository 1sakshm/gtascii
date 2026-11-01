#include "TestHarness.h"

#include "Graphics/Draw2D.h"

namespace {
int countChar(const av::Framebuffer& fb, char32_t ch)
{
    int n = 0;
    for (const av::Cell& c : fb.cells()) {
        n += c.ch == ch ? 1 : 0;
    }
    return n;
}

av::Cell glyph(char32_t ch)
{
    av::Cell c;
    c.ch = ch;
    return c;
}
} // namespace

TEST_CASE("Draw2D: text clips and keeps background when not given")
{
    av::Framebuffer fb(5, 2);
    av::Cell bgCell;
    bgCell.bg = {9, 9, 9};
    fb.clear(bgCell);

    CHECK_EQ(av::draw::text(fb, 3, 0, "hello", {255, 0, 0}), 2);
    CHECK_EQ(fb.at(3, 0).ch, U'h');
    CHECK_EQ(fb.at(4, 0).ch, U'e');
    CHECK(fb.at(3, 0).bg == av::Color(9, 9, 9));

    CHECK_EQ(av::draw::text(fb, -2, 1, "abcd", {0, 0, 0}, av::Color{1, 1, 1}), 2);
    CHECK_EQ(fb.at(0, 1).ch, U'c');
    CHECK(fb.at(0, 1).bg == av::Color(1, 1, 1));

    CHECK_EQ(av::draw::text(fb, 0, 5, "x", {0, 0, 0}), 0);
}

TEST_CASE("Draw2D: text decodes UTF-8 into single cells")
{
    av::Framebuffer fb(4, 1);
    CHECK_EQ(av::draw::text(fb, 0, 0, "\xE2\x96\x91\xE2\x96\x88", {0, 0, 0}), 2);
    CHECK_EQ(fb.at(0, 0).ch, U'░');
    CHECK_EQ(fb.at(1, 0).ch, U'█');
}

TEST_CASE("Draw2D: fillRect clips to framebuffer")
{
    av::Framebuffer fb(4, 4);
    av::draw::fillRect(fb, -2, -2, 4, 4, glyph(U'#'));
    CHECK_EQ(countChar(fb, U'#'), 4); // 2x2 visible
    CHECK_EQ(fb.at(1, 1).ch, U'#');
    CHECK_EQ(fb.at(2, 2).ch, U' ');
}

TEST_CASE("Draw2D: line includes both endpoints and is connected")
{
    av::Framebuffer fb(10, 10);
    av::draw::line(fb, 1, 1, 8, 5, glyph(U'*'));
    CHECK_EQ(fb.at(1, 1).ch, U'*');
    CHECK_EQ(fb.at(8, 5).ch, U'*');
    CHECK_EQ(countChar(fb, U'*'), 8); // x-major: one cell per column

    av::Framebuffer fb2(5, 5);
    av::draw::line(fb2, 2, 2, 2, 2, glyph(U'o'));
    CHECK_EQ(countChar(fb2, U'o'), 1);

    av::Framebuffer fb3(5, 5);
    av::draw::line(fb3, -5, 2, 10, 2, glyph(U'-')); // clipped, no crash
    CHECK_EQ(countChar(fb3, U'-'), 5);
}

TEST_CASE("Draw2D: fillCircle respects cell aspect")
{
    av::Framebuffer fb(40, 40);
    av::draw::fillCircle(fb, 20.0f, 20.0f, 10.0f, 2.0f, glyph(U'@'));
    CHECK_EQ(fb.at(20, 20).ch, U'@');
    // Horizontal radius 10 columns, vertical radius 5 rows.
    CHECK_EQ(fb.at(11, 20).ch, U'@');
    CHECK_EQ(fb.at(8, 20).ch, U' ');
    CHECK_EQ(fb.at(20, 16).ch, U'@');
    CHECK_EQ(fb.at(20, 26).ch, U' ');
}
