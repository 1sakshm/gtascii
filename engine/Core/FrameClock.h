#pragma once

#include <chrono>
#include <cstdint>

namespace av {

// Fixed-rate frame pacing and timing statistics.
//
//   clock.beginFrame();   // returns delta time since the previous frame
//   ... update + render ...
//   clock.endFrame();     // sleeps until the next frame deadline
class FrameClock {
public:
    using Clock = std::chrono::steady_clock;

    // targetFps <= 0 means uncapped.
    explicit FrameClock(double targetFps = 60.0);

    void setTargetFps(double targetFps);
    double targetFps() const noexcept { return targetFps_; }

    // Starts a frame; returns seconds elapsed since the previous beginFrame()
    // (clamped to 0.25 s so a stall does not explode simulations).
    double beginFrame();

    // Ends a frame: records work time, then waits for the next deadline.
    void endFrame();

    double elapsedSeconds() const;               // since construction
    double smoothedFps() const noexcept { return smoothedFps_; }
    double workTimeMs() const noexcept { return workTimeMs_; }   // last frame's work (excl. sleep)
    std::uint64_t frameCount() const noexcept { return frameCount_; }

private:
    double targetFps_ = 60.0;
    Clock::time_point start_;
    Clock::time_point frameStart_;
    Clock::time_point lastFrameStart_;
    Clock::time_point nextDeadline_;
    double smoothedFps_ = 0.0;
    double workTimeMs_ = 0.0;
    std::uint64_t frameCount_ = 0;
};

} // namespace av
