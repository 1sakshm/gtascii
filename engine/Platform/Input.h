#pragma once

namespace av {

enum class Key {
    Character, // printable character in KeyEvent::ch (letters are lowercase)
    Escape,
    Enter,
    Backspace,
    Tab,
    Up,
    Down,
    Left,
    Right,
    Shift,
    Control,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

struct KeyEvent {
    Key key = Key::Character;
    char32_t ch = 0;
    bool down = true;  // false = key released (only where the platform reports releases)
    bool ctrl = false;
    bool shift = false;
};

struct MouseEvent {
    int x = 0; // cell coordinates
    int y = 0;
    bool left = false;
    bool right = false;
    bool middle = false;
    int wheel = 0; // +1 away from the user, -1 towards
};

struct InputEvent {
    enum class Type {
        Key,
        Mouse,
        Resize,    // terminal size changed; query Terminal::size()
        Focus,     // window focus gained/lost (see `focused`)
        Interrupt, // Ctrl+C, Ctrl+Break, SIGINT/SIGTERM, console close
    };

    Type type = Type::Key;
    KeyEvent key;     // valid when type == Key
    MouseEvent mouse; // valid when type == Mouse
    bool focused = true;
};

} // namespace av
