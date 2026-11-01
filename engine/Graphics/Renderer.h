#pragma once

#include "Graphics/Camera.h"
#include "Graphics/CharRamp.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Lighting.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Math/Geometry.h"

#include <cstdint>
#include <vector>

namespace av {

struct RendererSettings {
    int samplesX = 2;              // sub-samples per cell, horizontally
    int samplesY = 2;              // and vertically (anti-aliasing, thin-object stability)
    float cellAspect = 2.0f;       // terminal cell height / width
    bool outlines = true;          // darken silhouette edges for readability
    float foregroundBoost = 1.25f; // glyph colour relative to the shaded colour
    float backgroundLevel = 0.45f; // cell background relative to the shaded colour
    bool clouds = true;
    int maxPointLights = 16;       // nearest lights considered per frame
    float cameraFill = 0.22f;      // soft light from the viewer: keeps surfaces facing the camera readable
};

struct RenderStats {
    std::size_t meshesSubmitted = 0;
    std::size_t meshesCulled = 0;
    std::size_t trianglesSubmitted = 0;
    std::size_t trianglesBackfaced = 0;
    std::size_t trianglesClipped = 0;   // needed polygon clipping
    std::size_t trianglesRasterized = 0;
    std::size_t samplesWritten = 0;
};

// Software 3D renderer targeting a character-cell framebuffer.
//
// Pipeline: model -> world -> clip transform; backface culling in world
// space; homogeneous clipping against all six frustum planes; perspective-
// correct rasterisation into a sub-sampled G-buffer (depth, normal, world
// position, material) with z-buffering; then a deferred shading + resolve
// pass that lights each sample once, folds samples into cells, picks a glyph
// from the material's ramp by light intensity and writes RGB fg/bg colours.
class Renderer {
public:
    Renderer(const MaterialLibrary& materials, const CharRamp& defaultRamp);

    void setSettings(const RendererSettings& settings);
    const RendererSettings& settings() const noexcept { return settings_; }
    void setDefaultRamp(const CharRamp& ramp) { defaultRamp_ = &ramp; }

    // Starts a frame for a cols x rows cell viewport.
    void beginFrame(int cols, int rows, const Camera& camera, const SceneLighting& lighting);

    bool isVisible(const AABB& worldBounds) const { return frustum_.intersects(worldBounds); }

    // Rasterises a mesh. overrideMaterial replaces every triangle's material.
    void drawMesh(const Mesh& mesh, const Mat4& model, MaterialId overrideMaterial = kNoMaterial);

    // Shades the G-buffer and writes cells (char, colours, depth, material).
    void resolve(Framebuffer& fb);

    const RenderStats& stats() const noexcept { return stats_; }
    const Mat4& viewProjection() const noexcept { return viewProj_; }
    const Frustum& frustum() const noexcept { return frustum_; }

    // G-buffer inspection (tests, picking). Depth is view-space distance.
    int sampleWidth() const noexcept { return sampleW_; }
    int sampleHeight() const noexcept { return sampleH_; }
    float sampleDepth(int sx, int sy) const { return depth_[index(sx, sy)]; }
    MaterialId sampleMaterial(int sx, int sy) const { return material_[index(sx, sy)]; }

private:
    struct ClipVertex {
        Vec4 clip;
        Vec3 world;
        Vec3 normal;
    };

    std::size_t index(int sx, int sy) const
    {
        return static_cast<std::size_t>(sy) * static_cast<std::size_t>(sampleW_) + static_cast<std::size_t>(sx);
    }

    void clipAndRasterize(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, MaterialId mat);
    void rasterize(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, MaterialId mat);

    Vec3 shadeSample(std::size_t i, float& intensity, const CharRamp*& ramp) const;
    Vec3 skyGradient(const Vec3& dir) const;
    Vec3 skyCell(const Vec3& dir, char32_t& glyph) const;
    Vec3 cellRayDirection(int cx, int cy) const;

    const MaterialLibrary& materials_;
    const CharRamp* defaultRamp_;
    RendererSettings settings_;

    int cols_ = 0, rows_ = 0;
    int sampleW_ = 0, sampleH_ = 0;
    Camera camera_;
    SceneLighting lighting_;
    std::vector<PointLight> lights_; // nearest point lights this frame
    Mat4 view_, proj_, viewProj_, invViewProj_;
    Frustum frustum_;

    // G-buffer (structure of arrays).
    std::vector<float> depth_;
    std::vector<Vec3> normal_;
    std::vector<Vec3> position_;
    std::vector<MaterialId> material_;

    std::vector<ClipVertex> transformed_; // per-mesh scratch
    std::vector<float> cellDepth_;        // per-cell front depth (outline pass)

    RenderStats stats_;
};

} // namespace av
