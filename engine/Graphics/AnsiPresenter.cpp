#include "Graphics/AnsiPresenter.h"

#include "Core/Utf8.h"

#include <algorithm>

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

AnsiPresenter::Glyph AnsiPresenter::resolve(const Cell& cell) const
{
    Glyph g;
    g.ch = sanitize(cell.ch);
    g.fg = quantize(cell.fg, options_.colorBits);
    // Without backgrounds, bg is irrelevant; normalise it so it never
    // registers as a change.
    g.bg = options_.useBackground ? quantize(cell.bg, options_.colorBits) : Color{};
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
        if (fgChanged) {
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
            if (options_.useBackground) {
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
            const Glyph g = resolve(cells[i]);
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
