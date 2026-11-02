#include "Platform/Terminal.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <string>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace av {

namespace {

std::atomic<bool> g_interrupt{false};

BOOL WINAPI consoleCtrlHandler(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_interrupt.store(true);
        return TRUE;
    default:
        return FALSE;
    }
}

void writeAll(HANDLE handle, std::string_view bytes)
{
    while (!bytes.empty()) {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1u << 30));
        if (!WriteFile(handle, bytes.data(), chunk, &written, nullptr) || written == 0) {
            return; // nothing sensible to do; the terminal is gone
        }
        bytes.remove_prefix(written);
    }
}

// Converts a console key record. Letters are identified by virtual key so
// that press and release (and Shift-modified presses) map to the same key.
void translateKey(const KEY_EVENT_RECORD& k, std::vector<InputEvent>& events)
{
    KeyEvent key;
    key.down = k.bKeyDown != 0;
    key.ctrl = (k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    key.shift = (k.dwControlKeyState & SHIFT_PRESSED) != 0;
    const wchar_t wc = k.uChar.UnicodeChar;
    const WORD vk = k.wVirtualKeyCode;

    switch (vk) {
    case VK_ESCAPE: key.key = Key::Escape; break;
    case VK_RETURN: key.key = Key::Enter; break;
    case VK_BACK: key.key = Key::Backspace; break;
    case VK_TAB: key.key = Key::Tab; break;
    case VK_UP: key.key = Key::Up; break;
    case VK_DOWN: key.key = Key::Down; break;
    case VK_LEFT: key.key = Key::Left; break;
    case VK_RIGHT: key.key = Key::Right; break;
    case VK_SHIFT: key.key = Key::Shift; break;
    case VK_CONTROL: key.key = Key::Control; break;
    case VK_F1: case VK_F2: case VK_F3: case VK_F4: case VK_F5: case VK_F6:
    case VK_F7: case VK_F8: case VK_F9: case VK_F10: case VK_F11: case VK_F12:
        key.key = static_cast<Key>(static_cast<int>(Key::F1) + (vk - VK_F1));
        break;
    default:
        if (key.down && wc == 3) { // Ctrl+C arrives as ETX because processed input is off
            InputEvent ev;
            ev.type = InputEvent::Type::Interrupt;
            events.push_back(ev);
            return;
        }
        if (vk >= 'A' && vk <= 'Z') {
            key.ch = static_cast<char32_t>(U'a' + (vk - 'A'));
        } else if (vk == VK_SPACE) {
            key.ch = U' ';
        } else if (wc >= 0x20 && (wc < 0xD800 || wc > 0xDFFF)) {
            key.ch = static_cast<char32_t>(wc);
        } else {
            return; // unmapped key (function keys etc.)
        }
        key.key = Key::Character;
        break;
    }

    InputEvent ev;
    ev.type = InputEvent::Type::Key;
    ev.key = key;
    const int repeat = key.down ? std::max<int>(1, k.wRepeatCount) : 1;
    for (int n = 0; n < repeat; ++n) {
        events.push_back(ev);
    }
}

bool envSet(const char* name)
{
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0';
}

} // namespace

struct Terminal::Impl {
    TerminalOptions options;
    HANDLE out = INVALID_HANDLE_VALUE;
    HANDLE in = INVALID_HANDLE_VALUE;

    bool outIsConsole = false;
    bool inIsConsole = false;
    DWORD originalOutMode = 0;
    DWORD originalInMode = 0;
    UINT originalOutputCp = 0;
    UINT originalInputCp = 0;
    bool codepagesChanged = false;
    bool ctrlHandlerInstalled = false;
    bool timerPeriodRaised = false;
    bool screenEntered = false;

    ~Impl()
    {
        if (screenEntered) {
            std::string restore;
            restore += vt::kResetAttributes;
            restore += vt::kEnableAutoWrap;
            restore += vt::kShowCursor;
            if (options.alternateScreen) {
                restore += vt::kLeaveAltScreen;
            }
            writeAll(out, restore);
        }
        if (inIsConsole) {
            SetConsoleMode(in, originalInMode);
        }
        if (outIsConsole) {
            SetConsoleMode(out, originalOutMode);
        }
        if (codepagesChanged) {
            SetConsoleOutputCP(originalOutputCp);
            SetConsoleCP(originalInputCp);
        }
        if (ctrlHandlerInstalled) {
            SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
        }
        if (timerPeriodRaised) {
            timeEndPeriod(1);
        }
    }
};

Terminal::Terminal(const TerminalOptions& options)
{
    // Built in a local unique_ptr so that, if anything below throws, ~Impl
    // undoes whatever was already changed.
    auto impl = std::make_unique<Impl>();
    impl->options = options;
    impl->out = GetStdHandle(STD_OUTPUT_HANDLE);
    impl->in = GetStdHandle(STD_INPUT_HANDLE);

    DWORD mode = 0;
    if (impl->out != INVALID_HANDLE_VALUE && GetConsoleMode(impl->out, &mode)) {
        impl->outIsConsole = true;
        impl->originalOutMode = mode;
        if (!SetConsoleMode(impl->out, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            throw std::runtime_error(
                "This console does not support virtual terminal sequences. "
                "Use Windows Terminal or Windows 10 1809+.");
        }

        impl->originalOutputCp = GetConsoleOutputCP();
        impl->originalInputCp = GetConsoleCP();
        impl->codepagesChanged = true;
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }

    if (impl->in != INVALID_HANDLE_VALUE && GetConsoleMode(impl->in, &mode)) {
        impl->inIsConsole = true;
        impl->originalInMode = mode;
        // Raw input: no line buffering, no echo, Ctrl+C delivered as a key,
        // quick-edit off (clicks would otherwise freeze output), resize events on.
        DWORD inMode = ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
        if (options.enableMouse) {
            inMode |= ENABLE_MOUSE_INPUT;
        }
        SetConsoleMode(impl->in, inMode);
        FlushConsoleInputBuffer(impl->in);
    }

    impl->ctrlHandlerInstalled = SetConsoleCtrlHandler(consoleCtrlHandler, TRUE) != 0;
    impl->timerPeriodRaised = timeBeginPeriod(1) == TIMERR_NOERROR;

    std::string enter;
    if (options.alternateScreen) {
        enter += vt::kEnterAltScreen;
    }
    if (options.hideCursor) {
        enter += vt::kHideCursor;
    }
    enter += vt::kDisableAutoWrap;
    enter += vt::kResetAttributes;
    enter += vt::kClearScreen;
    enter += vt::kCursorHome;
    writeAll(impl->out, enter);
    impl->screenEntered = true;

    impl_ = std::move(impl);
}

Terminal::~Terminal() = default;

bool Terminal::isInteractive() const noexcept
{
    return impl_->outIsConsole;
}

bool Terminal::prefers256Colors() const noexcept { return false; }

bool Terminal::supportsTrueColor() const noexcept
{
    if (const char* ct = std::getenv("COLORTERM")) {
        const std::string v = ct;
        if (v == "truecolor" || v == "24bit") {
            return true;
        }
    }
    // Windows Terminal, and conhost with VT processing (Win10 1703+), both render 24-bit colour.
    return envSet("WT_SESSION") || impl_->outIsConsole;
}

TerminalSize Terminal::size() const
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (impl_->outIsConsole && GetConsoleScreenBufferInfo(impl_->out, &info)) {
        return {info.srWindow.Right - info.srWindow.Left + 1, info.srWindow.Bottom - info.srWindow.Top + 1};
    }
    return impl_->options.fallbackSize;
}

void Terminal::write(std::string_view bytes)
{
    writeAll(impl_->out, bytes);
}

void Terminal::pollInput(std::vector<InputEvent>& events)
{
    if (g_interrupt.exchange(false)) {
        InputEvent ev;
        ev.type = InputEvent::Type::Interrupt;
        events.push_back(ev);
    }
    if (!impl_->inIsConsole) {
        return;
    }

    DWORD pending = 0;
    while (GetNumberOfConsoleInputEvents(impl_->in, &pending) && pending > 0) {
        INPUT_RECORD records[64];
        DWORD read = 0;
        if (!ReadConsoleInputW(impl_->in, records, std::min<DWORD>(pending, 64), &read) || read == 0) {
            return;
        }

        for (DWORD r = 0; r < read; ++r) {
            const INPUT_RECORD& rec = records[r];
            switch (rec.EventType) {
            case WINDOW_BUFFER_SIZE_EVENT: {
                InputEvent ev;
                ev.type = InputEvent::Type::Resize;
                events.push_back(ev);
                break;
            }
            case FOCUS_EVENT: {
                InputEvent ev;
                ev.type = InputEvent::Type::Focus;
                ev.focused = rec.Event.FocusEvent.bSetFocus != 0;
                events.push_back(ev);
                break;
            }
            case MOUSE_EVENT: {
                const MOUSE_EVENT_RECORD& m = rec.Event.MouseEvent;
                InputEvent ev;
                ev.type = InputEvent::Type::Mouse;
                ev.mouse.x = m.dwMousePosition.X;
                ev.mouse.y = m.dwMousePosition.Y;
                ev.mouse.left = (m.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
                ev.mouse.right = (m.dwButtonState & RIGHTMOST_BUTTON_PRESSED) != 0;
                ev.mouse.middle = (m.dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) != 0;
                if (m.dwEventFlags & MOUSE_WHEELED) {
                    // High word of dwButtonState is the signed wheel delta.
                    const auto delta = static_cast<short>(HIWORD(m.dwButtonState));
                    ev.mouse.wheel = delta > 0 ? 1 : -1;
                }
                events.push_back(ev);
                break;
            }
            case KEY_EVENT:
                translateKey(rec.Event.KeyEvent, events);
                break;
            default:
                break;
            }
        }
    }
}

} // namespace av
