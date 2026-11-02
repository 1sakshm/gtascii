#pragma once

#include "Platform/Input.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace av {

// Turns the event stream into "is this key held?" state, which movement
// needs. Windows Terminal reports key releases; terminals that do not (most
// POSIX terminals) are handled by expiring a key shortly after its last
// press/auto-repeat. Losing window focus releases everything.
class InputState {
public:
    // Seconds after the last press/repeat before an un-released key counts
    // as up. Only used until the first key-release event is seen.
    static constexpr double kRepeatTimeout = 0.6;

    void beginFrame(double now);
    void handle(const InputEvent& event, double now);

    bool held(Key key) const { return held_.count(id(key, 0)) != 0; }
    bool held(char32_t ch) const { return held_.count(id(Key::Character, ch)) != 0; }
    // True only on the frame the key went down (auto-repeat excluded).
    bool pressed(Key key) const { return pressed_.count(id(key, 0)) != 0; }
    bool pressed(char32_t ch) const { return pressed_.count(id(Key::Character, ch)) != 0; }
    bool shiftHeld() const { return held(Key::Shift) || shiftFromKeys_; }

    // Mouse: drag deltas (cells) accumulated this frame while the left button is down.
    int dragDx() const { return dragDx_; }
    int dragDy() const { return dragDy_; }
    int wheel() const { return wheel_; }
    // Mouse look: movement (cells) this frame with or without a button, the
    // pointer position, and whether the mouse moved within `seconds`.
    int motionDx() const { return motionDx_; }
    int motionDy() const { return motionDy_; }
    int mouseX() const { return lastMouseX_; }
    int mouseY() const { return lastMouseY_; }
    bool mouseRecentlyMoved(double now, double seconds) const { return seenMouse_ && now - lastMouseTime_ < seconds; }
    bool mouseRight() const { return mouseRight_; }
    bool focused() const { return focused_; }
    bool mouseLeft() const { return mouseLeft_; }

    bool reportsReleases() const { return sawRelease_; }
    void releaseAll();

private:
    static std::uint64_t id(Key key, char32_t ch)
    {
        return (static_cast<std::uint64_t>(key) << 32) | static_cast<std::uint64_t>(ch);
    }

    std::unordered_map<std::uint64_t, double> held_; // key -> last press time
    std::unordered_set<std::uint64_t> pressed_;
    bool sawRelease_ = false;
    bool shiftFromKeys_ = false;

    int lastMouseX_ = 0, lastMouseY_ = 0;
    bool mouseLeft_ = false;
    int dragDx_ = 0, dragDy_ = 0, wheel_ = 0;
    int motionDx_ = 0, motionDy_ = 0;
    bool seenMouse_ = false, mouseRight_ = false, focused_ = true;
    double lastMouseTime_ = 0.0;
};

} // namespace av
