#include "Platform/InputState.h"

#include <algorithm>

namespace av {

void InputState::beginFrame(double now)
{
    pressed_.clear();
    dragDx_ = dragDy_ = wheel_ = 0;
    motionDx_ = motionDy_ = 0;
    if (!sawRelease_) {
        for (auto it = held_.begin(); it != held_.end();) {
            it = (now - it->second > kRepeatTimeout) ? held_.erase(it) : std::next(it);
        }
        if (held_.empty()) {
            shiftFromKeys_ = false;
        }
    }
}

void InputState::releaseAll()
{
    held_.clear();
    shiftFromKeys_ = false;
    mouseLeft_ = false;
}

void InputState::handle(const InputEvent& event, double now)
{
    switch (event.type) {
    case InputEvent::Type::Key: {
        const KeyEvent& k = event.key;
        const std::uint64_t key = id(k.key, k.key == Key::Character ? k.ch : 0);
        if (k.down) {
            if (held_.count(key) == 0) {
                pressed_.insert(key);
            }
            held_[key] = now;
            shiftFromKeys_ = k.shift;
        } else {
            sawRelease_ = true;
            held_.erase(key);
            if (k.key == Key::Shift) {
                shiftFromKeys_ = false;
            }
        }
        break;
    }
    case InputEvent::Type::Mouse: {
        const MouseEvent& m = event.mouse;
        if (m.left && mouseLeft_) {
            dragDx_ += m.x - lastMouseX_;
            dragDy_ += m.y - lastMouseY_;
        }
        if (seenMouse_ && m.wheel == 0) {
            // Clamp: the first event after the pointer re-enters the window can jump.
            motionDx_ += std::clamp(m.x - lastMouseX_, -12, 12);
            motionDy_ += std::clamp(m.y - lastMouseY_, -8, 8);
        }
        if (m.wheel == 0) {
            seenMouse_ = true;
            lastMouseTime_ = now;
        }
        mouseRight_ = m.right;
        mouseLeft_ = m.left;
        lastMouseX_ = m.x;
        lastMouseY_ = m.y;
        wheel_ += m.wheel;
        break;
    }
    case InputEvent::Type::Focus:
        focused_ = event.focused;
        if (!event.focused) {
            releaseAll();
        }
        break;
    default:
        break;
    }
}

} // namespace av
