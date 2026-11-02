#pragma once

#include "Graphics/Framebuffer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace av {

struct PresenterOptions {
    bool useBackground = true;        // false: leave the terminal's default background
    int colorBits = 8;                // per-channel precision sent to the terminal (1..8)
    bool synchronizedOutput = true;   // wrap frames in DEC mode 2026 to avoid tearing
    bool palette256 = false;          // xterm 256-colour palette instead of 24-bit colour (shorter, works everywhere)
};

struct PresentStats {
    std::size_t cellsChanged = 0;
    std::size_t bytes = 0;
    bool fullRedraw = false;
};

// Converts framebuffers into ANSI true-colour escape output.
//
// The presenter keeps a copy of what the terminal currently shows (the
// "front" buffer). encode() compares the new frame against it and emits only
// the cells that changed, with minimal cursor movement and SGR (colour)
// changes. It produces bytes only; writing them is the caller's job, so the
// presenter is testable without a terminal.
class AnsiPresenter {
public:
    explicit AnsiPresenter(PresenterOptions options = {});

    // Changing options forces a full redraw on the next encode().
    void setOptions(const PresenterOptions& options);
    const PresenterOptions& options() const noexcept { return options_; }

    // Forces the next encode() to redraw every cell (e.g. after something
    // else wrote to the terminal).
    void invalidate() noexcept { forceFull_ = true; }

    // Returns the bytes needed to bring the terminal from the previous frame
    // to `frame`. Empty if nothing changed. The reference is valid until the
    // next call.
    const std::string& encode(const Framebuffer& frame);

    const PresentStats& lastStats() const noexcept { return stats_; }

private:
    // What a cell looks like on the terminal after quantisation/sanitising.
    struct Glyph {
        char32_t ch = U' ';
        Color fg;
        Color bg;
        bool operator==(const Glyph& o) const { return ch == o.ch && fg == o.fg && bg == o.bg; }
        bool operator!=(const Glyph& o) const { return !(*this == o); }
    };

    Glyph resolve(const Cell& cell, int x, int y) const;
    bool matchesCurrentSgr(const Glyph& g) const;
    void moveTo(int x, int y, int rowStart);
    void emitGlyph(const Glyph& g);
    void appendNumber(unsigned value);

    PresenterOptions options_;
    std::vector<Glyph> front_;
    int frontWidth_ = 0;
    int frontHeight_ = 0;
    bool forceFull_ = true;

    // Terminal state tracking during encoding.
    int cursorX_ = 0;
    int cursorY_ = 0;
    bool cursorKnown_ = false;
    bool sgrKnown_ = false;
    Color sgrFg_;
    Color sgrBg_;

    std::string out_;
    PresentStats stats_;
};

} // namespace av
