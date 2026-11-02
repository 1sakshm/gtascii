// POSIX terminal backend (Linux/macOS). Phase 1 targets Windows Terminal;
// this backend is provided for portability but has not yet been verified on
// a POSIX system.
#include "Platform/Terminal.h"

#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace av {

namespace {

volatile std::sig_atomic_t g_interrupt = 0;
volatile std::sig_atomic_t g_resized = 0;

extern "C" void onInterruptSignal(int) { g_interrupt = 1; }
extern "C" void onResizeSignal(int) { g_resized = 1; }

void writeAll(int fd, std::string_view bytes)
{
    while (!bytes.empty()) {
        const ssize_t n = ::write(fd, bytes.data(), bytes.size());
        if (n <= 0) {
            return;
        }
        bytes.remove_prefix(static_cast<std::size_t>(n));
    }
}

} // namespace

struct Terminal::Impl {
    TerminalOptions options;
    bool outIsTty = false;
    bool inIsTty = false;
    termios originalTermios{};
    bool termiosChanged = false;
    int originalStdinFlags = 0;
    bool stdinFlagsChanged = false;
    struct sigaction oldInt{}, oldTerm{}, oldWinch{};
    bool signalsInstalled = false;
    bool screenEntered = false;
    std::string pendingInput;
    bool mouseLeft = false;

    ~Impl()
    {
        if (screenEntered) {
            std::string restore;
            restore += vt::kResetAttributes;
            if (options.enableMouse) {
                restore += "\x1b[?1006l\x1b[?1003l\x1b[?1002l\x1b[?1000l";
            }
            restore += vt::kEnableAutoWrap;
            restore += vt::kShowCursor;
            if (options.alternateScreen) {
                restore += vt::kLeaveAltScreen;
            }
            writeAll(STDOUT_FILENO, restore);
        }
        if (stdinFlagsChanged) {
            fcntl(STDIN_FILENO, F_SETFL, originalStdinFlags);
        }
        if (termiosChanged) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios);
        }
        if (signalsInstalled) {
            sigaction(SIGINT, &oldInt, nullptr);
            sigaction(SIGTERM, &oldTerm, nullptr);
            sigaction(SIGWINCH, &oldWinch, nullptr);
        }
    }
};

Terminal::Terminal(const TerminalOptions& options)
{
    auto impl = std::make_unique<Impl>();
    impl->options = options;
    impl->outIsTty = isatty(STDOUT_FILENO) != 0;
    impl->inIsTty = isatty(STDIN_FILENO) != 0;

    if (impl->inIsTty && tcgetattr(STDIN_FILENO, &impl->originalTermios) == 0) {
        termios raw = impl->originalTermios;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG | IEXTEN));
        raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
            impl->termiosChanged = true;
        }
        impl->originalStdinFlags = fcntl(STDIN_FILENO, F_GETFL);
        if (impl->originalStdinFlags != -1 &&
            fcntl(STDIN_FILENO, F_SETFL, impl->originalStdinFlags | O_NONBLOCK) != -1) {
            impl->stdinFlagsChanged = true;
        }
    }

    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = onInterruptSignal;
    sigaction(SIGINT, &sa, &impl->oldInt);
    sigaction(SIGTERM, &sa, &impl->oldTerm);
    sa.sa_handler = onResizeSignal;
    sigaction(SIGWINCH, &sa, &impl->oldWinch);
    impl->signalsInstalled = true;

    std::string enter;
    if (options.alternateScreen) {
        enter += vt::kEnterAltScreen;
    }
    if (options.hideCursor) {
        enter += vt::kHideCursor;
    }
    enter += vt::kDisableAutoWrap;
    if (options.enableMouse) {
        enter += "\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h"; // buttons + all movement (mouse look), SGR encoding
    }
    enter += vt::kResetAttributes;
    enter += vt::kClearScreen;
    enter += vt::kCursorHome;
    writeAll(STDOUT_FILENO, enter);
    impl->screenEntered = true;

    impl_ = std::move(impl);
}

Terminal::~Terminal() = default;

bool Terminal::isInteractive() const noexcept
{
    return impl_->outIsTty;
}

bool Terminal::supportsTrueColor() const noexcept
{
    const char* ct = std::getenv("COLORTERM");
    return ct != nullptr && (std::strcmp(ct, "truecolor") == 0 || std::strcmp(ct, "24bit") == 0);
}

TerminalSize Terminal::size() const
{
    winsize ws{};
    if (impl_->outIsTty && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return {ws.ws_col, ws.ws_row};
    }
    return impl_->options.fallbackSize;
}

void Terminal::write(std::string_view bytes)
{
    writeAll(STDOUT_FILENO, bytes);
}

void Terminal::pollInput(std::vector<InputEvent>& events)
{
    if (g_interrupt) {
        g_interrupt = 0;
        { InputEvent ev; ev.type = InputEvent::Type::Interrupt; events.push_back(ev); }
    }
    if (g_resized) {
        g_resized = 0;
        { InputEvent ev; ev.type = InputEvent::Type::Resize; events.push_back(ev); }
    }
    if (!impl_->inIsTty) {
        return;
    }

    char buf[256];
    ssize_t n;
    while ((n = ::read(STDIN_FILENO, buf, sizeof buf)) > 0) {
        impl_->pendingInput.append(buf, static_cast<std::size_t>(n));
    }

    std::string& in = impl_->pendingInput;
    auto pushKey = [&events](Key k, char32_t ch = 0, bool shift = false) {
        KeyEvent e;
        e.key = k;
        e.ch = ch;
        e.shift = shift;
        InputEvent ev;
        ev.type = InputEvent::Type::Key;
        ev.key = e;
        events.push_back(ev);
    };

    std::size_t i = 0;
    while (i < in.size()) {
        const auto c = static_cast<unsigned char>(in[i]);
        if (c == 0x1b) {
            if (i + 1 >= in.size()) {
                pushKey(Key::Escape); // lone ESC: nothing else arrived in this read
                ++i;
                continue;
            }
            const char kind = in[i + 1];
            if (kind == 'O' && i + 2 < in.size()) { // SS3: F1-F4, some arrow encodings
                const char f = in[i + 2];
                if (f >= 'P' && f <= 'S') pushKey(static_cast<Key>(static_cast<int>(Key::F1) + (f - 'P')));
                else if (f == 'A') pushKey(Key::Up);
                else if (f == 'B') pushKey(Key::Down);
                else if (f == 'C') pushKey(Key::Right);
                else if (f == 'D') pushKey(Key::Left);
                i += 3;
                continue;
            }
            if (kind != '[') {
                pushKey(Key::Escape);
                ++i;
                continue;
            }
            // CSI: ESC [ params final
            std::size_t j = i + 2;
            while (j < in.size() && !(in[j] >= 0x40 && in[j] <= 0x7E)) {
                ++j;
            }
            if (j >= in.size()) {
                break; // incomplete: wait for the rest of the sequence
            }
            const std::string params = in.substr(i + 2, j - (i + 2));
            const char final = in[j];
            i = j + 1;
            if (!params.empty() && params[0] == '<' && (final == 'M' || final == 'm')) {
                // SGR mouse: <button;x;y
                int b = 0, x = 0, y = 0;
                if (std::sscanf(params.c_str() + 1, "%d;%d;%d", &b, &x, &y) == 3) {
                    InputEvent ev;
                    ev.type = InputEvent::Type::Mouse;
                    ev.mouse.x = x - 1;
                    ev.mouse.y = y - 1;
                    if (b & 64) {
                        ev.mouse.wheel = (b & 1) ? -1 : 1;
                        ev.mouse.left = impl_->mouseLeft;
                    } else {
                        const int button = b & 3;
                        const bool motion = (b & 32) != 0;
                        const bool pressed = final == 'M';
                        if (button == 0 && !motion) impl_->mouseLeft = pressed;
                        ev.mouse.left = impl_->mouseLeft;
                        ev.mouse.middle = button == 1 && pressed && !motion;
                        ev.mouse.right = button == 2 && pressed;
                    }
                    events.push_back(ev);
                }
                continue;
            }
            const bool shift = params.find(";2") != std::string::npos;
            switch (final) {
            case 'A': pushKey(Key::Up, 0, shift); break;
            case 'B': pushKey(Key::Down, 0, shift); break;
            case 'C': pushKey(Key::Right, 0, shift); break;
            case 'D': pushKey(Key::Left, 0, shift); break;
            case 'Z': pushKey(Key::Tab, 0, true); break;
            case '~': {
                const int code = std::atoi(params.c_str());
                static const int fcodes[] = {11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24};
                for (int f = 0; f < 12; ++f) {
                    if (code == fcodes[f]) pushKey(static_cast<Key>(static_cast<int>(Key::F1) + f));
                }
                break;
            }
            default: break; // focus reports, unknown sequences
            }
        } else if (c == 3) {
            InputEvent ev;
            ev.type = InputEvent::Type::Interrupt;
            events.push_back(ev);
            ++i;
        } else if (c == '\r' || c == '\n') {
            pushKey(Key::Enter);
            ++i;
        } else if (c == 0x7f || c == 0x08) {
            pushKey(Key::Backspace);
            ++i;
        } else if (c == '\t') {
            pushKey(Key::Tab);
            ++i;
        } else if (c >= 0x20 && c < 0x7f) {
            const bool upper = c >= 'A' && c <= 'Z';
            pushKey(Key::Character, upper ? static_cast<char32_t>(c - 'A' + 'a') : c, upper);
            ++i;
        } else {
            ++i; // ignore other bytes (non-ASCII text input is not needed)
        }
    }
    in.erase(0, i);
}

} // namespace av
