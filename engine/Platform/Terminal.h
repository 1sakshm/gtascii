#pragma once

#include "Platform/Input.h"

#include <memory>
#include <string_view>
#include <vector>

namespace av {

struct TerminalSize {
    int cols = 0;
    int rows = 0;
    bool operator==(const TerminalSize& o) const { return cols == o.cols && rows == o.rows; }
    bool operator!=(const TerminalSize& o) const { return !(*this == o); }
};

struct TerminalOptions {
    bool alternateScreen = true;
    bool hideCursor = true;
    // Report mouse events. While enabled, text selection in the terminal
    // needs Shift+drag.
    bool enableMouse = false;
    // Used when stdout is not a terminal (e.g. redirected to a file).
    TerminalSize fallbackSize{120, 40};
};

// RAII owner of the terminal session.
//
// The constructor switches the terminal into a mode suitable for full-screen
// rendering (VT processing, UTF-8, raw unbuffered input, alternate screen,
// hidden cursor, no auto-wrap). The destructor restores everything it
// changed, including when unwinding from an exception.
//
// If stdout is not an interactive terminal, the session still works: output
// is written as-is and size() returns the fallback size.
class Terminal {
public:
    explicit Terminal(const TerminalOptions& options = {});
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    bool isInteractive() const noexcept;

    // Best-effort detection of 24-bit colour support.
    bool supportsTrueColor() const noexcept;
    // True where the 256-colour palette is the better choice (macOS Terminal.app).
    bool prefers256Colors() const noexcept;
    // True inside tmux or GNU screen, which re-draw all our output themselves.
    bool insideMultiplexer() const noexcept;

    // Current visible size in character cells.
    TerminalSize size() const;

    // Writes raw bytes (already containing escape sequences) to the terminal.
    void write(std::string_view bytes);

    // Appends pending input events without blocking.
    void pollInput(std::vector<InputEvent>& events);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

namespace vt {
inline constexpr std::string_view kEnterAltScreen = "\x1b[?1049h";
inline constexpr std::string_view kLeaveAltScreen = "\x1b[?1049l";
inline constexpr std::string_view kHideCursor = "\x1b[?25l";
inline constexpr std::string_view kShowCursor = "\x1b[?25h";
inline constexpr std::string_view kDisableAutoWrap = "\x1b[?7l";
inline constexpr std::string_view kEnableAutoWrap = "\x1b[?7h";
inline constexpr std::string_view kResetAttributes = "\x1b[0m";
inline constexpr std::string_view kClearScreen = "\x1b[2J";
inline constexpr std::string_view kCursorHome = "\x1b[H";
} // namespace vt

} // namespace av
