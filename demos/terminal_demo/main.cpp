// GRAND THEFT ASCII - Phase 1 terminal graphics demo.
//
// Renders animated patterns into an av::Framebuffer, converts them to ANSI
// true-colour output with av::AnsiPresenter, and writes the result through
// the RAII av::Terminal session.
#include "DemoOptions.h"
#include "Hud.h"
#include "Pattern.h"

#include "Core/FrameClock.h"
#include "Graphics/AnsiPresenter.h"
#include "Graphics/CharRamp.h"
#include "Graphics/Framebuffer.h"
#include "Platform/Terminal.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <iterator>
#include <vector>

namespace {

struct RunStats {
    std::uint64_t frames = 0;
    double seconds = 0.0;
    std::size_t totalBytes = 0;
    std::size_t peakBytes = 0;
    std::size_t totalCells = 0;
    double totalWorkMs = 0.0;
    int width = 0;
    int height = 0;
    bool interactive = false;
    bool trueColor = false;
};

constexpr double kFpsSteps[] = {15, 24, 30, 60, 120, 0};
constexpr int kColorBitSteps[] = {8, 6, 5, 4, 3};

std::size_t indexOfRamp(const std::string& name)
{
    const auto& ramps = av::builtinRamps();
    for (std::size_t i = 0; i < ramps.size(); ++i) {
        if (ramps[i].name() == name) {
            return i;
        }
    }
    return 0;
}

RunStats run(const demo::DemoOptions& opts)
{
    av::Terminal terminal;
    RunStats stats;
    stats.interactive = terminal.isInteractive();
    stats.trueColor = terminal.supportsTrueColor();

    auto patterns = demo::createPatterns();
    const int patternCount = static_cast<int>(patterns.size());
    int current = std::clamp(opts.pattern, 0, patternCount - 1);

    std::size_t rampIndex = indexOfRamp(opts.ramp);
    const auto& ramps = av::builtinRamps();

    av::PresenterOptions presenterOptions;
    presenterOptions.useBackground = opts.background;
    presenterOptions.colorBits = opts.colorBits;
    presenterOptions.synchronizedOutput = opts.synchronizedOutput;
    av::AnsiPresenter presenter(presenterOptions);

    av::FrameClock clock(opts.fps);
    av::Framebuffer fb;
    std::vector<av::InputEvent> events;

    bool hud = opts.hud;
    bool autoCycle = opts.autoCycle;
    double patternTime = 0.0;
    double sinceSwitch = 0.0;
    bool running = true;

    auto switchTo = [&](int index) {
        current = ((index % patternCount) + patternCount) % patternCount;
        patternTime = 0.0;
        sinceSwitch = 0.0;
    };

    while (running) {
        const double dt = clock.beginFrame();
        patternTime += dt;
        sinceSwitch += dt;

        events.clear();
        terminal.pollInput(events);
        for (const av::InputEvent& ev : events) {
            if (ev.type == av::InputEvent::Type::Interrupt) {
                running = false;
                continue;
            }
            if (ev.type == av::InputEvent::Type::Resize) {
                presenter.invalidate();
                continue;
            }
            if (ev.type != av::InputEvent::Type::Key || !ev.key.down) {
                continue;
            }
            const av::KeyEvent& k = ev.key;
            if (k.key == av::Key::Escape) {
                running = false;
            } else if (k.key == av::Key::Right) {
                switchTo(current + 1);
            } else if (k.key == av::Key::Left) {
                switchTo(current - 1);
            } else if (k.key == av::Key::Character) {
                const char32_t c = (k.ch >= U'A' && k.ch <= U'Z') ? k.ch - U'A' + U'a' : k.ch;
                if (c == U'q') {
                    running = false;
                } else if (c >= U'1' && c < U'1' + static_cast<char32_t>(patternCount)) {
                    switchTo(static_cast<int>(c - U'1'));
                } else if (c == U'n' || c == U' ') {
                    switchTo(current + 1);
                } else if (c == U'p') {
                    switchTo(current - 1);
                } else if (c == U'a') {
                    autoCycle = !autoCycle;
                    sinceSwitch = 0.0;
                } else if (c == U'r') {
                    rampIndex = (rampIndex + 1) % ramps.size();
                } else if (c == U'h') {
                    hud = !hud;
                } else if (c == U'b') {
                    presenterOptions.useBackground = !presenterOptions.useBackground;
                    presenter.setOptions(presenterOptions);
                } else if (c == U'c') {
                    const auto* it = std::find(std::begin(kColorBitSteps), std::end(kColorBitSteps),
                                               presenterOptions.colorBits);
                    const auto pos = it == std::end(kColorBitSteps) ? 0 : (it - std::begin(kColorBitSteps) + 1);
                    presenterOptions.colorBits = kColorBitSteps[pos % std::size(kColorBitSteps)];
                    presenter.setOptions(presenterOptions);
                } else if (c == U'+' || c == U'=' || c == U'-') {
                    const double fps = clock.targetFps();
                    const auto* it = std::find(std::begin(kFpsSteps), std::end(kFpsSteps), fps);
                    long pos = it == std::end(kFpsSteps) ? 3 : static_cast<long>(it - std::begin(kFpsSteps));
                    pos += (c == U'-') ? -1 : 1;
                    pos = std::clamp<long>(pos, 0, static_cast<long>(std::size(kFpsSteps)) - 1);
                    clock.setTargetFps(kFpsSteps[pos]);
                }
            }
        }
        if (!running) {
            break;
        }

        if (autoCycle && sinceSwitch >= opts.cycleSeconds) {
            switchTo(current + 1);
        }

        // Fit the framebuffer to the terminal (or the fixed size, clamped to it).
        const av::TerminalSize ts = terminal.size();
        int w = ts.cols;
        int h = ts.rows;
        if (opts.fixedWidth > 0) {
            w = std::min(w, opts.fixedWidth);
            h = std::min(h, opts.fixedHeight);
        }
        if (w != fb.width() || h != fb.height()) {
            fb.resize(w, h);
        }

        fb.clear();
        demo::PatternContext ctx;
        ctx.time = patternTime;
        ctx.dt = static_cast<float>(dt);
        ctx.cellAspect = opts.cellAspect;
        ctx.ramp = &ramps[rampIndex];
        patterns[static_cast<std::size_t>(current)]->render(fb, ctx);

        if (hud) {
            // Shows the previous frame's presenter stats (this frame's are not known yet).
            const av::PresentStats& last = presenter.lastStats();
            demo::HudInfo info;
            info.patternName = patterns[static_cast<std::size_t>(current)]->name();
            info.patternIndex = current;
            info.patternCount = patternCount;
            info.fps = clock.smoothedFps();
            info.targetFps = clock.targetFps();
            info.workMs = clock.workTimeMs();
            info.cellsChanged = last.cellsChanged;
            info.bytes = last.bytes;
            info.rampName = ramps[rampIndex].name();
            info.colorBits = presenterOptions.colorBits;
            info.background = presenterOptions.useBackground;
            info.autoCycle = autoCycle;
            info.trueColor = stats.trueColor;
            demo::drawHud(fb, info);
        }

        const std::string& out = presenter.encode(fb);
        if (!out.empty()) {
            terminal.write(out);
        }

        const av::PresentStats& ps = presenter.lastStats();
        stats.totalBytes += ps.bytes;
        stats.peakBytes = std::max(stats.peakBytes, ps.bytes);
        stats.totalCells += ps.cellsChanged;

        clock.endFrame();
        stats.totalWorkMs += clock.workTimeMs();

        if (opts.maxFrames > 0 && clock.frameCount() >= opts.maxFrames) {
            running = false;
        }
    }

    stats.frames = clock.frameCount();
    stats.seconds = clock.elapsedSeconds();
    stats.width = fb.width();
    stats.height = fb.height();
    return stats; // ~Terminal restores the console here
}

void printStats(const RunStats& s)
{
    const double frames = static_cast<double>(std::max<std::uint64_t>(s.frames, 1));
    std::printf("GRAND THEFT ASCII Phase 1 demo finished.\n");
    std::printf("  terminal       : %s, true colour %s\n", s.interactive ? "interactive" : "redirected (fallback size)",
                s.trueColor ? "detected" : "not detected");
    std::printf("  framebuffer    : %d x %d cells\n", s.width, s.height);
    std::printf("  frames         : %llu in %.2f s (%.1f fps average)\n",
                static_cast<unsigned long long>(s.frames), s.seconds, s.seconds > 0 ? s.frames / s.seconds : 0.0);
    std::printf("  work time      : %.2f ms/frame average (render + encode + write)\n", s.totalWorkMs / frames);
    std::printf("  cells changed  : %.0f per frame average\n", static_cast<double>(s.totalCells) / frames);
    std::printf("  output         : %.1f KB/frame average, %.1f KB peak, %.2f MB total\n",
                static_cast<double>(s.totalBytes) / frames / 1024.0, static_cast<double>(s.peakBytes) / 1024.0,
                static_cast<double>(s.totalBytes) / (1024.0 * 1024.0));
}

} // namespace

int main(int argc, char** argv)
{
    const demo::ParseResult parsed = demo::parseOptions(argc, argv);
    if (!parsed.error.empty()) {
        std::fprintf(stderr, "error: %s\n\n%s", parsed.error.c_str(), demo::usageText());
        return 2;
    }
    if (parsed.showHelp) {
        std::printf("%s", demo::usageText());
        return 0;
    }

    try {
        const RunStats stats = run(parsed.options);
        printStats(stats);
    } catch (const std::exception& e) {
        // The terminal has already been restored by stack unwinding.
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
