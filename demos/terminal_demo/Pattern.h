#pragma once

#include "Graphics/CharRamp.h"
#include "Graphics/Framebuffer.h"

#include <memory>
#include <vector>

namespace demo {

struct PatternContext {
    double time = 0.0;         // seconds since the pattern became active
    float dt = 0.0f;           // seconds since previous frame
    float cellAspect = 2.0f;   // terminal cell height / width
    const av::CharRamp* ramp = nullptr;
};

// An animated full-screen pattern. render() must write every cell it wants
// visible; the framebuffer is cleared to black before each call.
class Pattern {
public:
    virtual ~Pattern() = default;
    virtual const char* name() const = 0;
    virtual void render(av::Framebuffer& fb, const PatternContext& ctx) = 0;
};

std::unique_ptr<Pattern> makePlasma();
std::unique_ptr<Pattern> makeTunnel();
std::unique_ptr<Pattern> makeInterference();
std::unique_ptr<Pattern> makeShapes();
std::unique_ptr<Pattern> makeTestCard();

std::vector<std::unique_ptr<Pattern>> createPatterns();

} // namespace demo
