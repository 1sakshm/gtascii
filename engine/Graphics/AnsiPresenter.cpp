#include "Graphics/AnsiPresenter.h"

#include "Core/Utf8.h"

#include <algorithm>
#include <cstdint>

namespace av {

namespace {
// Skipping this many unchanged cells by rewriting them is cheaper than a
// cursor-forward sequence (ESC [ n C is at least 4 bytes).
constexpr int kMaxRewriteGap = 3;

char32_t sanitize(char32_t ch)
{
    // C0/C1 control characters and DEL would be interpreted by the terminal.
    if (ch < 0x20 || (ch >= 0x7F && ch < 0xA0)) {
        return U' ';
    }
    if (ch > 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF)) {
        return U'?';
    }
    return ch;
}

// xterm 256-colour palette: a 6x6x6 cube (16-231) and 24 greys (232-255).
// The palette is coarse in the darks, so each colour is drawn as an ordered
// (screen-locked 4x4 Bayer) mix of its nearest palette entry and the entry on
// the other side of it, which keeps hue and brightness on average. A lookup
// table over 5-bit RGB holds both entries and the mix.
constexpr int kCubeLevel[6] = {0, 95, 135, 175, 215, 255};
constexpr int kBayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

Color paletteColor(int index);

struct PaletteLut {
    std::uint8_t near[32768], far[32768], mix[32768];
    PaletteLut()
    {
        Color pal[240];
        for (int i = 0; i < 240; ++i) pal[i] = paletteColor(16 + i);
        auto dist = [](int r, int g, int b, Color p) {
            const int dr = r - p.r, dg = g - p.g, db = b - p.b, rm = (r + p.r) / 2;
            return (512 + rm) * dr * dr + 1024 * dg * dg + (767 - rm) * db * db; // "redmean" weighting
        };
        auto nearest = [&](int r, int g, int b) {
            int best = 0;
            long long bestD = -1;
            for (int i = 0; i < 240; ++i) {
                const long long d = dist(r, g, b, pal[i]);
                if (bestD < 0 || d < bestD) { bestD = d; best = i; }
            }
            return best;
        };
        for (int k = 0; k < 32768; ++k) {
            const int r = ((k >> 10) & 31) * 255 / 31, g = ((k >> 5) & 31) * 255 / 31, b = (k & 31) * 255 / 31;
            const int a = nearest(r, g, b);
            const Color A = pal[a];
            // The entry on the far side of the colour from A.
            const int f = nearest(std::clamp(2 * r - A.r, 0, 255), std::clamp(2 * g - A.g, 0, 255), std::clamp(2 * b - A.b, 0, 255));
            const Color B = pal[f];
            const int ex = B.r - A.r, ey = B.g - A.g, ez = B.b - A.b;
            const int len2 = ex * ex + ey * ey + ez * ez;
            int t = 0;
            if (len2 > 0) t = std::clamp(((r - A.r) * ex + (g - A.g) * ey + (b - A.b) * ez) * 16 / len2, 0, 8);
            near[k] = static_cast<std::uint8_t>(a);
            far[k] = static_cast<std::uint8_t>(f);
            mix[k] = static_cast<std::uint8_t>(t);
        }
    }
};

int paletteIndex(Color c, int x, int y)
{
    static const PaletteLut lut;
    const int k = ((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3);
    const bool useFar = kBayer4[(y & 3) * 4 + (x & 3)] < lut.mix[k];
    return 16 + (useFar ? lut.far[k] : lut.near[k]);
}

Color paletteColor(int index)
{
    if (index >= 232) {
        const auto v = static_cast<std::uint8_t>(8 + (index - 232) * 10);
        return {v, v, v};
    }
    const int i = index - 16;
    return {static_cast<std::uint8_t>(kCubeLevel[i / 36]), static_cast<std::uint8_t>(kCubeLevel[(i / 6) % 6]),
            static_cast<std::uint8_t>(kCubeLevel[i % 6])};
}

// The palette entry a resolved (already snapped) colour came from.
int exactIndex(Color c)
{
    if (c.r == c.g && c.g == c.b && c.r >= 8 && c.r <= 238 && (c.r - 8) % 10 == 0) return 232 + (c.r - 8) / 10;
    auto level = [](int v) { int i = 0; while (i < 5 && kCubeLevel[i] != v) ++i; return i; };
    return 16 + 36 * level(c.r) + 6 * level(c.g) + level(c.b);
}
} // namespace

AnsiPresenter::AnsiPresenter(PresenterOptions options)
    : options_(options)
{
    options_.colorBits = std::clamp(options_.colorBits, 1, 8);
}

void AnsiPresenter::setOptions(const PresenterOptions& options)
{
    options_ = options;
    options_.colorBits = std::clamp(options_.colorBits, 1, 8);
    forceFull_ = true;
}

AnsiPresenter::Glyph AnsiPresenter::resolve(const Cell& cell, int x, int y) const
{
    Glyph g;
    g.ch = sanitize(cell.ch);
    g.fg = quantize(cell.fg, options_.colorBits);
    // Without backgrounds, bg is irrelevant; normalise it so it never
    // registers as a change.
    g.bg = options_.useBackground ? quantize(cell.bg, options_.colorBits) : Color{};
    if (options_.palette256) {
        // Snap to the palette so unchanged-looking cells really compare equal.
        g.fg = paletteColor(paletteIndex(g.fg, x, y));
        if (options_.useBackground) g.bg = paletteColor(paletteIndex(g.bg, x + 2, y + 1));
    }
    return g;
}

bool AnsiPresenter::matchesCurrentSgr(const Glyph& g) const
{
    return sgrKnown_ && g.fg == sgrFg_ && g.bg == sgrBg_;
}

void AnsiPresenter::appendNumber(unsigned value)
{
    char buf[10];
    int len = 0;
    do {
        buf[len++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (len > 0) {
        out_.push_back(buf[--len]);
    }
}

void AnsiPresenter::moveTo(int x, int y, int rowStart)
{
    if (cursorKnown_ && cursorY_ == y && cursorX_ == x) {
        return;
    }

    if (cursorKnown_ && cursorY_ == y && x > cursorX_) {
        const int gap = x - cursorX_;
        // The skipped cells are unchanged, so front_ holds what they show.
        bool rewrite = gap <= kMaxRewriteGap;
        for (int k = cursorX_; rewrite && k < x; ++k) {
            rewrite = matchesCurrentSgr(front_[static_cast<std::size_t>(rowStart + k)]);
        }
        if (rewrite) {
            for (int k = cursorX_; k < x; ++k) {
                appendUtf8(out_, front_[static_cast<std::size_t>(rowStart + k)].ch);
            }
        } else {
            out_ += "\x1b[";
            if (gap > 1) {
                appendNumber(static_cast<unsigned>(gap));
            }
            out_.push_back('C');
        }
    } else {
        // CUP is 1-based: ESC [ row ; col H
        out_ += "\x1b[";
        appendNumber(static_cast<unsigned>(y + 1));
        out_.push_back(';');
        appendNumber(static_cast<unsigned>(x + 1));
        out_.push_back('H');
    }
    cursorX_ = x;
    cursorY_ = y;
    cursorKnown_ = true;
}

void AnsiPresenter::emitGlyph(const Glyph& g)
{
    const bool fgChanged = !sgrKnown_ || g.fg != sgrFg_;
    const bool bgChanged = !sgrKnown_ || (options_.useBackground && g.bg != sgrBg_);

    if (fgChanged || bgChanged) {
        out_ += "\x1b[";
        if (fgChanged && options_.palette256) {
            out_ += "38;5;";
            appendNumber(static_cast<unsigned>(exactIndex(g.fg)));
        } else if (fgChanged) {
            out_ += "38;2;";
            appendNumber(g.fg.r);
            out_.push_back(';');
            appendNumber(g.fg.g);
            out_.push_back(';');
            appendNumber(g.fg.b);
        }
        if (bgChanged) {
            if (fgChanged) {
                out_.push_back(';');
            }
            if (options_.useBackground && options_.palette256) {
                out_ += "48;5;";
                appendNumber(static_cast<unsigned>(exactIndex(g.bg)));
            } else if (options_.useBackground) {
                out_ += "48;2;";
                appendNumber(g.bg.r);
                out_.push_back(';');
                appendNumber(g.bg.g);
                out_.push_back(';');
                appendNumber(g.bg.b);
            } else {
                out_ += "49"; // default background
            }
        }
        out_.push_back('m');
        sgrKnown_ = true;
        sgrFg_ = g.fg;
        sgrBg_ = g.bg;
    }

    appendUtf8(out_, g.ch);
}

const std::string& AnsiPresenter::encode(const Framebuffer& frame)
{
    out_.clear();
    stats_ = {};

    const int width = frame.width();
    const int height = frame.height();
    const bool full = forceFull_ || width != frontWidth_ || height != frontHeight_;

    if (full) {
        front_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), Glyph{});
        frontWidth_ = width;
        frontHeight_ = height;
        forceFull_ = false;
    }

    // The cursor may have been moved by anything between frames; never rely on it.
    cursorKnown_ = false;
    bool begun = false;
    const auto& cells = frame.cells();

    for (int y = 0; y < height; ++y) {
        const int rowStart = y * width;
        for (int x = 0; x < width; ++x) {
            const auto i = static_cast<std::size_t>(rowStart + x);
            const Glyph g = resolve(cells[i], x, y);
            if (!full && g == front_[i]) {
                continue;
            }

            if (!begun) {
                begun = true;
                if (options_.synchronizedOutput) {
                    out_ += "\x1b[?2026h";
                }
                if (full) {
                    // Reset attributes and wipe anything outside our area.
                    out_ += "\x1b[0m\x1b[2J";
                    sgrKnown_ = false;
                }
            }

            moveTo(x, y, rowStart);
            emitGlyph(g);
            front_[i] = g;
            ++stats_.cellsChanged;

            // After the last column the cursor position depends on the
            // terminal's wrap mode, so treat it as unknown.
            ++cursorX_;
            if (cursorX_ >= width) {
                cursorKnown_ = false;
            }
        }
    }

    if (begun && options_.synchronizedOutput) {
        out_ += "\x1b[?2026l";
    }

    stats_.fullRedraw = full;
    stats_.bytes = out_.size();
    return out_;
}

} // namespace av
