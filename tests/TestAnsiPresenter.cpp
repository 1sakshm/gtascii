#include "TestHarness.h"

#include "Graphics/AnsiPresenter.h"

namespace {

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::size_t countOf(const std::string& haystack, const std::string& needle)
{
    std::size_t n = 0;
    for (std::size_t pos = haystack.find(needle); pos != std::string::npos; pos = haystack.find(needle, pos + 1)) {
        ++n;
    }
    return n;
}

av::PresenterOptions noSync()
{
    av::PresenterOptions o;
    o.synchronizedOutput = false;
    return o;
}

} // namespace

TEST_CASE("AnsiPresenter: first frame is a full redraw of every cell")
{
    av::Framebuffer fb(4, 3);
    av::AnsiPresenter presenter(noSync());
    const std::string out = presenter.encode(fb);
    CHECK(presenter.lastStats().fullRedraw);
    CHECK_EQ(presenter.lastStats().cellsChanged, std::size_t{12});
    CHECK_EQ(presenter.lastStats().bytes, out.size());
    CHECK(contains(out, "\x1b[2J"));
    CHECK(contains(out, "\x1b[1;1H"));
    // Uniform colours: exactly one SGR sequence for the whole frame.
    CHECK_EQ(countOf(out, "38;2;"), std::size_t{1});
}

TEST_CASE("AnsiPresenter: unchanged frame produces no output")
{
    av::Framebuffer fb(8, 4);
    av::AnsiPresenter presenter;
    presenter.encode(fb);
    const std::string out = presenter.encode(fb);
    CHECK(out.empty());
    CHECK_EQ(presenter.lastStats().cellsChanged, std::size_t{0});
    CHECK(!presenter.lastStats().fullRedraw);
}

TEST_CASE("AnsiPresenter: single changed cell is addressed directly")
{
    av::Framebuffer fb(10, 5);
    av::AnsiPresenter presenter(noSync());
    presenter.encode(fb);

    fb.at(4, 2).ch = U'X';
    fb.at(4, 2).fg = {255, 0, 0};
    const std::string out = presenter.encode(fb);
    CHECK_EQ(presenter.lastStats().cellsChanged, std::size_t{1});
    CHECK(contains(out, "\x1b[3;5H")); // 1-based row;col
    CHECK(contains(out, "38;2;255;0;0"));
    CHECK(contains(out, "X"));
    CHECK(!contains(out, "\x1b[2J"));
}

TEST_CASE("AnsiPresenter: depth and material changes are not redrawn")
{
    av::Framebuffer fb(3, 3);
    av::AnsiPresenter presenter;
    presenter.encode(fb);
    fb.at(1, 1).depth = 0.5f;
    fb.at(1, 1).material = 3;
    CHECK(presenter.encode(fb).empty());
}

TEST_CASE("AnsiPresenter: small same-colour gaps are rewritten, large gaps use cursor forward")
{
    av::Framebuffer fb(20, 1);
    av::AnsiPresenter presenter(noSync());
    presenter.encode(fb);

    fb.at(0, 0).ch = U'a';
    fb.at(2, 0).ch = U'b';  // gap of 1 unchanged cell -> rewrite " "
    fb.at(10, 0).ch = U'c'; // gap of 7 -> ESC[7C
    const std::string out = presenter.encode(fb);
    CHECK_EQ(presenter.lastStats().cellsChanged, std::size_t{3});
    CHECK(contains(out, "a b"));
    CHECK(contains(out, "\x1b[7Cc"));
}

TEST_CASE("AnsiPresenter: SGR emitted only when colours change")
{
    av::Framebuffer fb(6, 1);
    for (int x = 0; x < 6; ++x) {
        fb.at(x, 0).fg = x < 3 ? av::Color{10, 20, 30} : av::Color{40, 50, 60};
    }
    av::AnsiPresenter presenter(noSync());
    const std::string out = presenter.encode(fb);
    CHECK_EQ(countOf(out, "38;2;10;20;30"), std::size_t{1});
    CHECK_EQ(countOf(out, "38;2;40;50;60"), std::size_t{1});
    CHECK_EQ(countOf(out, "48;2;"), std::size_t{1}); // background constant
}

TEST_CASE("AnsiPresenter: resize forces a full redraw")
{
    av::Framebuffer fb(5, 5);
    av::AnsiPresenter presenter;
    presenter.encode(fb);
    fb.resize(6, 5);
    presenter.encode(fb);
    CHECK(presenter.lastStats().fullRedraw);
    CHECK_EQ(presenter.lastStats().cellsChanged, std::size_t{30});
}

TEST_CASE("AnsiPresenter: invalidate and setOptions force a full redraw")
{
    av::Framebuffer fb(3, 2);
    av::AnsiPresenter presenter;
    presenter.encode(fb);
    presenter.invalidate();
    presenter.encode(fb);
    CHECK(presenter.lastStats().fullRedraw);

    presenter.encode(fb);
    CHECK(!presenter.lastStats().fullRedraw);
    presenter.setOptions(av::PresenterOptions{});
    presenter.encode(fb);
    CHECK(presenter.lastStats().fullRedraw);
}

TEST_CASE("AnsiPresenter: colour quantisation hides sub-threshold changes")
{
    av::Framebuffer fb(2, 1);
    av::PresenterOptions o = noSync();
    o.colorBits = 4;
    av::AnsiPresenter presenter(o);
    fb.at(0, 0).fg = {0x10, 0x10, 0x10};
    presenter.encode(fb);
    fb.at(0, 0).fg = {0x1F, 0x1F, 0x1F}; // same 4-bit level
    CHECK(presenter.encode(fb).empty());
    fb.at(0, 0).fg = {0x20, 0x20, 0x20}; // next level
    CHECK_EQ(presenter.encode(fb).empty(), false);
}

TEST_CASE("AnsiPresenter: background disabled uses default background")
{
    av::Framebuffer fb(3, 1);
    av::PresenterOptions o = noSync();
    o.useBackground = false;
    av::AnsiPresenter presenter(o);
    const std::string out = presenter.encode(fb);
    CHECK(contains(out, ";49m"));
    CHECK(!contains(out, "48;2;"));

    fb.at(1, 0).bg = {200, 0, 0}; // invisible without backgrounds
    CHECK(presenter.encode(fb).empty());
}

TEST_CASE("AnsiPresenter: control characters are sanitised, UTF-8 is encoded")
{
    av::Framebuffer fb(3, 1);
    fb.at(0, 0).ch = U'\x1b';
    fb.at(1, 0).ch = U'\n';
    fb.at(2, 0).ch = U'▓';
    av::AnsiPresenter presenter(noSync());
    const std::string out = presenter.encode(fb);
    CHECK(contains(out, "m  \xE2\x96\x93"));
    // The only ESC bytes are the ones we generated: clear, reset, CUP, SGR.
    CHECK_EQ(countOf(out, "\x1b"), std::size_t{4});
}

TEST_CASE("AnsiPresenter: synchronized output wraps non-empty frames")
{
    av::Framebuffer fb(2, 2);
    av::AnsiPresenter presenter;
    const std::string out = presenter.encode(fb);
    CHECK_EQ(out.rfind("\x1b[?2026h", 0), std::size_t{0});
    CHECK(out.size() >= 8 && out.compare(out.size() - 8, 8, "\x1b[?2026l") == 0);
}

TEST_CASE("AnsiPresenter: cursor is re-addressed after the last column")
{
    av::Framebuffer fb(3, 2);
    av::AnsiPresenter presenter(noSync());
    presenter.encode(fb);
    fb.at(2, 0).ch = U'a';
    fb.at(0, 1).ch = U'b';
    const std::string out = presenter.encode(fb);
    CHECK(contains(out, "\x1b[1;3H"));
    CHECK(contains(out, "\x1b[2;1H"));
}
