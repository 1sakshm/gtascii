#include "TestHarness.h"

#include "Graphics/Framebuffer.h"

#include <cmath>

TEST_CASE("Framebuffer: construction and resize")
{
    av::Framebuffer fb(10, 4);
    CHECK_EQ(fb.width(), 10);
    CHECK_EQ(fb.height(), 4);
    CHECK_EQ(fb.cells().size(), std::size_t{40});

    fb.resize(3, 2);
    CHECK_EQ(fb.cells().size(), std::size_t{6});

    fb.resize(5, 0); // degenerate sizes collapse to empty
    CHECK(fb.empty());
    CHECK_EQ(fb.width(), 0);

    fb.resize(-3, 7);
    CHECK(fb.empty());
}

TEST_CASE("Framebuffer: bounds-checked set ignores out of range")
{
    av::Framebuffer fb(4, 3);
    av::Cell c;
    c.ch = U'X';
    fb.set(-1, 0, c);
    fb.set(4, 0, c);
    fb.set(0, 3, c);
    for (const av::Cell& cell : fb.cells()) {
        CHECK_EQ(cell.ch, U' ');
    }
    fb.set(3, 2, c);
    CHECK_EQ(fb.at(3, 2).ch, U'X');
    CHECK_EQ(fb.cells().back().ch, U'X'); // row-major: (3,2) is the last cell
}

TEST_CASE("Framebuffer: clear fills every cell and resets depth")
{
    av::Framebuffer fb(5, 5);
    av::Cell near;
    near.depth = 1.0f;
    fb.set(2, 2, near);

    av::Cell fill;
    fill.ch = U'#';
    fill.bg = {1, 2, 3};
    fb.clear(fill);
    for (const av::Cell& cell : fb.cells()) {
        CHECK_EQ(cell.ch, U'#');
        CHECK(cell.bg == av::Color(1, 2, 3));
        CHECK(std::isinf(cell.depth));
    }
}

TEST_CASE("Framebuffer: depth test keeps the nearest write")
{
    av::Framebuffer fb(2, 2);
    av::Cell far;
    far.ch = U'F';
    far.depth = 10.0f;
    av::Cell near;
    near.ch = U'N';
    near.depth = 2.0f;

    CHECK(fb.setDepthTested(1, 1, far));
    CHECK(fb.setDepthTested(1, 1, near));
    CHECK(!fb.setDepthTested(1, 1, far)); // farther: rejected
    CHECK_EQ(fb.at(1, 1).ch, U'N');

    av::Cell equal = near;
    equal.ch = U'E';
    CHECK(!fb.setDepthTested(1, 1, equal)); // strict less-than
    CHECK(!fb.setDepthTested(5, 5, near));  // out of bounds

    fb.resetDepth();
    CHECK(fb.setDepthTested(1, 1, far));
    CHECK_EQ(fb.at(1, 1).ch, U'F');
}

TEST_CASE("Cell: appearance ignores depth and material")
{
    av::Cell a;
    av::Cell b;
    b.depth = 3.0f;
    b.material = 7;
    CHECK(a.sameAppearance(b));
    b.fg = {1, 1, 1};
    CHECK(!a.sameAppearance(b));
}
