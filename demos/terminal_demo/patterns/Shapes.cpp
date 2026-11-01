// Synthwave scene built from 2D primitives: gradient sky, striped sun, a
// scrolling perspective grid (Bresenham lines), and bouncing shaded discs.
//
// The discs are drawn in arbitrary order through Framebuffer::setDepthTested
// with a per-cell depth, so overlapping discs intersect correctly. This
// exercises the framebuffer's depth channel; the discs are screen-space
// shading, not the real 3D pipeline (that is Phase 2).
#include "Pattern.h"
#include "PatternUtil.h"

#include "Graphics/Draw2D.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace demo {
namespace {

struct Ball {
    float x, y;       // normalised position [0, 1]
    float vx, vy;     // normalised units per second
    float radius;     // fraction of min(width, height * aspect)
    float depthPhase;
    av::Color color;
};

class Shapes final : public Pattern {
public:
    const char* name() const override { return "Shapes"; }

    void render(av::Framebuffer& fb, const PatternContext& ctx) override
    {
        const int width = fb.width();
        const int height = fb.height();
        if (width == 0 || height == 0) {
            return;
        }
        const float t = static_cast<float>(ctx.time);
        const int horizon = static_cast<int>(static_cast<float>(height) * 0.58f);

        drawSky(fb, horizon);
        drawSun(fb, horizon, ctx.cellAspect, t);
        drawGrid(fb, horizon, t);
        updateAndDrawBalls(fb, ctx, t);
    }

private:
    static void drawSky(av::Framebuffer& fb, int horizon)
    {
        for (int y = 0; y < horizon && y < fb.height(); ++y) {
            const float k = static_cast<float>(y) / static_cast<float>(std::max(1, horizon));
            av::Cell sky;
            sky.ch = U' ';
            sky.bg = av::lerp({8, 2, 30}, {90, 20, 90}, k);
            av::draw::fillRect(fb, 0, y, fb.width(), 1, sky);
        }
        // A few twinkling stars.
        for (int i = 0; i < fb.width() * horizon / 60; ++i) {
            const int sx = (i * 7919) % std::max(1, fb.width());
            const int sy = (i * 104729) % std::max(1, horizon / 2 + 1);
            if (fb.inBounds(sx, sy)) {
                fb.at(sx, sy).ch = (i % 3 == 0) ? U'*' : U'.';
                fb.at(sx, sy).fg = {200, 180, 255};
            }
        }
    }

    static void drawSun(av::Framebuffer& fb, int horizon, float aspect, float t)
    {
        const float cx = static_cast<float>(fb.width()) * 0.5f;
        const float cy = static_cast<float>(horizon);
        const float radius = std::min(static_cast<float>(fb.width()) * 0.18f,
                                      static_cast<float>(horizon) * aspect * 0.8f);
        // Recolour sky cells inside the disc with a vertical gradient; the
        // stripes let the sky show through.
        const float ry = radius / aspect;
        for (int y = std::max(0, static_cast<int>(cy - ry)); y < horizon; ++y) {
            const float k = (cy - static_cast<float>(y)) / std::max(ry, 1.0f); // 1 at top, 0 at horizon
            const bool stripe = k < 0.45f && static_cast<int>(static_cast<float>(y) - t * 2.0f) % 3 == 0;
            for (int x = 0; x < fb.width(); ++x) {
                av::Cell& c = fb.at(x, y);
                const float dx = (static_cast<float>(x) + 0.5f - cx) / radius;
                const float dy = (static_cast<float>(y) + 0.5f - cy) / ry;
                if (dx * dx + dy * dy > 1.0f || stripe) {
                    continue;
                }
                c.bg = av::lerp({255, 60, 120}, {255, 215, 90}, k);
                c.ch = U'=';
                c.fg = av::scale(c.bg, 1.15f);
            }
        }
    }

    static void drawGrid(av::Framebuffer& fb, int horizon, float t)
    {
        const int width = fb.width();
        const int height = fb.height();
        av::Cell floor;
        floor.ch = U' ';
        floor.bg = {12, 0, 24};
        av::draw::fillRect(fb, 0, horizon, width, height - horizon, floor);

        av::Cell gridLine;
        gridLine.ch = U'-';
        gridLine.fg = {255, 40, 200};
        gridLine.bg = {40, 0, 60};

        // Horizontal lines: spacing grows towards the viewer; they scroll forward.
        const float span = static_cast<float>(height - horizon);
        constexpr int kLines = 10;
        for (int i = 0; i < kLines; ++i) {
            const float z = std::fmod(static_cast<float>(i) + t * 1.5f, static_cast<float>(kLines)) + 1.0f;
            const int y = horizon + static_cast<int>(span / z);
            if (y < height) {
                av::draw::fillRect(fb, 0, y, width, 1, gridLine);
            }
        }

        // Vertical lines converging on the vanishing point.
        av::Cell ray = gridLine;
        ray.ch = U'|';
        const int vx = width / 2;
        for (int i = -8; i <= 8; ++i) {
            const int bottomX = vx + i * width / 6;
            av::draw::line(fb, vx + i * 2, horizon, bottomX, height - 1, ray);
        }
        av::Cell horizonLine = gridLine;
        horizonLine.ch = U'_';
        horizonLine.fg = {255, 120, 230};
        av::draw::fillRect(fb, 0, horizon, width, 1, horizonLine);
    }

    void updateAndDrawBalls(av::Framebuffer& fb, const PatternContext& ctx, float t)
    {
        const float w = static_cast<float>(fb.width());
        const float h = static_cast<float>(fb.height());
        const float unit = std::min(w, h * ctx.cellAspect);
        // Light from the upper left, towards the viewer.
        const float lx = -0.45f, ly = -0.55f, lz = 0.70f;

        for (Ball& b : balls_) {
            // Radius in normalised screen units on each axis.
            const float rxN = b.radius * unit / w;
            const float ryN = b.radius * unit / (h * ctx.cellAspect);

            b.x += b.vx * ctx.dt;
            b.y += b.vy * ctx.dt;
            if (b.x < rxN) { b.x = rxN; b.vx = std::fabs(b.vx); }
            if (b.x > 1.0f - rxN) { b.x = 1.0f - rxN; b.vx = -std::fabs(b.vx); }
            if (b.y < ryN) { b.y = ryN; b.vy = std::fabs(b.vy); }
            if (b.y > 1.0f - ryN) { b.y = 1.0f - ryN; b.vy = -std::fabs(b.vy); }

            const float cx = b.x * w;
            const float cy = b.y * h;
            const float rCols = b.radius * unit;
            const float rRows = rCols / ctx.cellAspect;
            const float centreDepth = 2.0f + std::sin(t * 0.8f + b.depthPhase);

            const int x0 = std::max(0, static_cast<int>(cx - rCols));
            const int x1 = std::min(fb.width() - 1, static_cast<int>(cx + rCols));
            const int y0 = std::max(0, static_cast<int>(cy - rRows));
            const int y1 = std::min(fb.height() - 1, static_cast<int>(cy + rRows));

            for (int y = y0; y <= y1; ++y) {
                const float ny = (static_cast<float>(y) + 0.5f - cy) / rRows;
                for (int x = x0; x <= x1; ++x) {
                    const float nx = (static_cast<float>(x) + 0.5f - cx) / rCols;
                    const float d2 = nx * nx + ny * ny;
                    if (d2 > 1.0f) {
                        continue;
                    }
                    const float nz = std::sqrt(1.0f - d2);
                    const float diffuse = std::max(0.0f, nx * lx + ny * ly + nz * lz);
                    const float spec = std::pow(diffuse, 24.0f) * 0.7f;
                    const float intensity = std::min(1.0f, 0.10f + 0.75f * diffuse + spec);

                    av::Cell cell;
                    cell.ch = ctx.ramp->pick(intensity);
                    cell.fg = av::addSaturate(av::scale(b.color, 0.25f + 0.9f * diffuse),
                                              av::scale({255, 255, 255}, spec));
                    cell.bg = av::scale(b.color, 0.08f + 0.25f * diffuse);
                    cell.depth = centreDepth - nz * 0.6f; // sphere surface bulges towards the viewer
                    fb.setDepthTested(x, y, cell);
                }
            }
        }
    }

    Ball balls_[6] = {
        {0.20f, 0.30f, 0.11f, 0.08f, 0.09f, 0.0f, {255, 70, 160}},
        {0.70f, 0.40f, -0.09f, 0.10f, 0.12f, 1.3f, {60, 220, 255}},
        {0.45f, 0.70f, 0.07f, -0.12f, 0.08f, 2.1f, {255, 200, 60}},
        {0.85f, 0.20f, -0.12f, 0.06f, 0.07f, 3.4f, {120, 255, 140}},
        {0.30f, 0.55f, 0.10f, 0.11f, 0.10f, 4.2f, {180, 110, 255}},
        {0.60f, 0.80f, -0.06f, -0.09f, 0.06f, 5.0f, {255, 120, 70}},
    };
};

} // namespace

std::unique_ptr<Pattern> makeShapes() { return std::make_unique<Shapes>(); }

} // namespace demo
