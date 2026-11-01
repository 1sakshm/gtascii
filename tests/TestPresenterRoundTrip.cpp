// Feeds AnsiPresenter output into a minimal VT emulator and checks that the
// emulated screen matches the framebuffer after every frame. This verifies
// the diffing, cursor tracking and SGR state tracking end to end.
#include "TestHarness.h"

#include "Core/Utf8.h"
#include "Graphics/AnsiPresenter.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {

// Understands exactly the subset the presenter emits: CUP (H), CUF (C),
// ED 2 (J), SGR 0 / 38;2 / 48;2 / 49 (m), DEC private modes (ignored), and
// UTF-8 text with auto-wrap disabled.
class VtScreen {
public:
    VtScreen(int w, int h) : w_(w), h_(h), cells_(static_cast<std::size_t>(w * h)) {}

    void feed(const std::string& bytes)
    {
        const std::u32string text = av::decodeUtf8(bytes);
        std::size_t i = 0;
        while (i < text.size()) {
            if (text[i] == U'\x1b') {
                i = parseEscape(text, i);
            } else {
                put(text[i++]);
            }
        }
    }

    struct Cell {
        char32_t ch = U' ';
        av::Color fg;
        av::Color bg;
        bool bgDefault = true;
    };

    const Cell& at(int x, int y) const { return cells_[static_cast<std::size_t>(y * w_ + x)]; }
    bool sawUnknownSequence() const { return unknown_; }

private:
    void put(char32_t ch)
    {
        if (cx_ >= 0 && cx_ < w_ && cy_ >= 0 && cy_ < h_) {
            Cell& c = cells_[static_cast<std::size_t>(cy_ * w_ + cx_)];
            c.ch = ch;
            c.fg = fg_;
            c.bg = bg_;
            c.bgDefault = bgDefault_;
        }
        if (cx_ < w_ - 1) {
            ++cx_; // no auto-wrap: the cursor sticks at the last column
        }
    }

    std::size_t parseEscape(const std::u32string& s, std::size_t i)
    {
        ++i; // ESC
        if (i >= s.size() || s[i] != U'[') {
            unknown_ = true;
            return i;
        }
        ++i;
        bool privateMode = false;
        if (i < s.size() && s[i] == U'?') {
            privateMode = true;
            ++i;
        }
        std::vector<int> params;
        int current = -1;
        while (i < s.size() && ((s[i] >= U'0' && s[i] <= U'9') || s[i] == U';')) {
            if (s[i] == U';') {
                params.push_back(current < 0 ? 0 : current);
                current = -1;
            } else {
                current = (current < 0 ? 0 : current * 10) + static_cast<int>(s[i] - U'0');
            }
            ++i;
        }
        if (current >= 0) {
            params.push_back(current);
        }
        if (i >= s.size()) {
            unknown_ = true;
            return i;
        }
        const char32_t final = s[i++];
        auto param = [&](std::size_t n, int def) { return n < params.size() && params[n] > 0 ? params[n] : def; };

        if (privateMode) {
            return i; // mode set/reset; no effect on contents
        }
        switch (final) {
        case U'H':
            cy_ = param(0, 1) - 1;
            cx_ = param(1, 1) - 1;
            break;
        case U'C':
            cx_ = std::min(w_ - 1, cx_ + param(0, 1));
            break;
        case U'J':
            for (Cell& c : cells_) {
                c = Cell{};
                c.fg = fg_;
                c.bg = bg_;
                c.bgDefault = bgDefault_;
            }
            break;
        case U'm':
            applySgr(params);
            break;
        default:
            unknown_ = true;
            break;
        }
        return i;
    }

    void applySgr(const std::vector<int>& p)
    {
        if (p.empty()) {
            reset();
            return;
        }
        for (std::size_t k = 0; k < p.size(); ++k) {
            if (p[k] == 0) {
                reset();
            } else if ((p[k] == 38 || p[k] == 48) && k + 4 < p.size() && p[k + 1] == 2) {
                const av::Color c(static_cast<std::uint8_t>(p[k + 2]), static_cast<std::uint8_t>(p[k + 3]),
                                  static_cast<std::uint8_t>(p[k + 4]));
                if (p[k] == 38) {
                    fg_ = c;
                } else {
                    bg_ = c;
                    bgDefault_ = false;
                }
                k += 4;
            } else if (p[k] == 49) {
                bgDefault_ = true;
            } else {
                unknown_ = true;
            }
        }
    }

    void reset()
    {
        fg_ = {255, 255, 255}; // arbitrary "default" so stale state would be detected
        bg_ = {};
        bgDefault_ = true;
    }

    int w_, h_;
    std::vector<Cell> cells_;
    int cx_ = 0, cy_ = 0;
    av::Color fg_{255, 255, 255};
    av::Color bg_;
    bool bgDefault_ = true;
    bool unknown_ = false;
};

// Random frames with spatially clustered changes, a small palette (so SGR
// reuse happens) and occasional sparse updates (so cursor jumps happen).
void mutate(av::Framebuffer& fb, std::mt19937& rng, int frame)
{
    static const av::Color palette[] = {{0, 0, 0}, {255, 0, 0}, {0, 128, 255}, {250, 250, 250}, {30, 200, 90}};
    static const char32_t glyphs[] = {U' ', U'.', U'#', U'@', U'▓', U'x'};
    std::uniform_int_distribution<int> pick5(0, 4);
    std::uniform_int_distribution<int> pick6(0, 5);
    std::uniform_int_distribution<int> px(0, fb.width() - 1);
    std::uniform_int_distribution<int> py(0, fb.height() - 1);

    const int changes = frame % 5 == 0 ? fb.width() * fb.height() : 1 + frame % 13;
    for (int n = 0; n < changes; ++n) {
        av::Cell& c = fb.at(px(rng), py(rng));
        c.ch = glyphs[pick6(rng)];
        c.fg = palette[pick5(rng)];
        c.bg = palette[pick5(rng)];
    }
}

void runRoundTrip(const av::PresenterOptions& options, std::uint32_t seed)
{
    constexpr int kW = 23;
    constexpr int kH = 7;
    av::Framebuffer fb(kW, kH);
    av::AnsiPresenter presenter(options);
    VtScreen screen(kW, kH);
    std::mt19937 rng(seed);

    for (int frame = 0; frame < 200; ++frame) {
        mutate(fb, rng, frame);
        screen.feed(presenter.encode(fb));

        bool match = true;
        for (int y = 0; y < kH && match; ++y) {
            for (int x = 0; x < kW && match; ++x) {
                const av::Cell& want = fb.at(x, y);
                const VtScreen::Cell& got = screen.at(x, y);
                match = got.ch == want.ch && got.fg == av::quantize(want.fg, options.colorBits);
                if (options.useBackground) {
                    match = match && !got.bgDefault && got.bg == av::quantize(want.bg, options.colorBits);
                } else {
                    match = match && got.bgDefault;
                }
                if (!match) {
                    std::printf("    mismatch at frame %d cell (%d,%d)\n", frame, x, y);
                }
            }
        }
        CHECK(match);
        if (!match) {
            return;
        }
    }
    CHECK(!screen.sawUnknownSequence());
}

} // namespace

TEST_CASE("AnsiPresenter round trip: default options")
{
    runRoundTrip(av::PresenterOptions{}, 1234u);
}

TEST_CASE("AnsiPresenter round trip: no background, no sync")
{
    av::PresenterOptions o;
    o.useBackground = false;
    o.synchronizedOutput = false;
    runRoundTrip(o, 99u);
}

TEST_CASE("AnsiPresenter round trip: 3-bit colour")
{
    av::PresenterOptions o;
    o.colorBits = 3;
    runRoundTrip(o, 7u);
}
