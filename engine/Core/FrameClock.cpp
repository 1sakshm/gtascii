#include "Core/FrameClock.h"

#include <algorithm>
#include <thread>

namespace av {

namespace {
using Seconds = std::chrono::duration<double>;
using Millis = std::chrono::duration<double, std::milli>;
} // namespace

FrameClock::FrameClock(double targetFps)
    : targetFps_(targetFps)
    , start_(Clock::now())
    , frameStart_(start_)
    , lastFrameStart_(start_)
    , nextDeadline_(start_)
{
}

void FrameClock::setTargetFps(double targetFps)
{
    targetFps_ = targetFps;
    nextDeadline_ = Clock::now();
}

double FrameClock::beginFrame()
{
    lastFrameStart_ = frameStart_;
    frameStart_ = Clock::now();

    if (frameCount_ == 0) {
        nextDeadline_ = frameStart_;
        return 0.0;
    }

    const double dt = Seconds(frameStart_ - lastFrameStart_).count();
    if (dt > 0.0) {
        const double instantFps = 1.0 / dt;
        smoothedFps_ = smoothedFps_ <= 0.0 ? instantFps : smoothedFps_ * 0.9 + instantFps * 0.1;
    }
    return std::min(dt, 0.25);
}

void FrameClock::endFrame()
{
    const auto workEnd = Clock::now();
    workTimeMs_ = Millis(workEnd - frameStart_).count();
    ++frameCount_;

    if (targetFps_ <= 0.0) {
        return;
    }

    const auto period = std::chrono::duration_cast<Clock::duration>(Seconds(1.0 / targetFps_));
    nextDeadline_ += period;

    // If we fell more than a frame behind, drop the backlog instead of
    // running a burst of unpaced frames to catch up.
    if (workEnd > nextDeadline_ + period) {
        nextDeadline_ = workEnd;
        return;
    }

    // Coarse sleep, then a short yield-spin for precision.
    const auto spinMargin = std::chrono::milliseconds(2);
    if (nextDeadline_ - workEnd > spinMargin) {
        std::this_thread::sleep_until(nextDeadline_ - spinMargin);
    }
    while (Clock::now() < nextDeadline_) {
        std::this_thread::yield();
    }
}

double FrameClock::elapsedSeconds() const
{
    return Seconds(Clock::now() - start_).count();
}

} // namespace av
