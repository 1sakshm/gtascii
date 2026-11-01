#include "Graphics/Renderer.h"

#include "Core/Random.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace av {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

// Signed distance of a clip-space vertex to frustum plane `p`
// (0..5: left, right, bottom, top, near, far). Inside when >= 0.
inline float planeDistance(const Vec4& c, int p)
{
    switch (p) {
    case 0: return c.w + c.x;
    case 1: return c.w - c.x;
    case 2: return c.w + c.y;
    case 3: return c.w - c.y;
    case 4: return c.w + c.z;
    default: return c.w - c.z;
    }
}

inline unsigned outcode(const Vec4& c)
{
    unsigned code = 0;
    for (int p = 0; p < 6; ++p) {
        if (planeDistance(c, p) < 0.0f) {
            code |= 1u << p;
        }
    }
    return code;
}

// Soft-knee tone curve: linear up to 0.8, then compresses towards 1.
inline float toneChannel(float c)
{
    if (c <= 0.8f) {
        return std::max(c, 0.0f);
    }
    return 0.8f + 0.2f * (1.0f - std::exp(-(c - 0.8f) * 5.0f));
}

inline Color toColor(const Vec3& c)
{
    return Color::fromFloat(toneChannel(c.x), toneChannel(c.y), toneChannel(c.z));
}

inline float luminance(const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

// Smooth 2D value noise in [0, 1].
float valueNoise(float x, float y)
{
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float tx = x - fx, ty = y - fy;
    auto h = [](int a, int b) { return static_cast<float>(hash2i(0x5eedu, a, b) & 0xFFFF) / 65535.0f; };
    const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const float a = lerpf(h(ix, iy), h(ix + 1, iy), sx);
    const float b = lerpf(h(ix, iy + 1), h(ix + 1, iy + 1), sx);
    return lerpf(a, b, sy);
}

float positionHash(const Vec3& p)
{
    const auto x = static_cast<int>(std::floor(p.x * 1.5f));
    const auto y = static_cast<int>(std::floor(p.y * 1.5f));
    const auto z = static_cast<int>(std::floor(p.z * 1.5f));
    return static_cast<float>(hashCombine(hash2i(0xc0ffeeu, x, y), static_cast<std::uint32_t>(z)) & 0xFFFF) /
               65535.0f - 0.5f;
}

} // namespace

Renderer::Renderer(const MaterialLibrary& materials, const CharRamp& defaultRamp)
    : materials_(materials)
    , defaultRamp_(&defaultRamp)
{
}

void Renderer::setSettings(const RendererSettings& settings)
{
    settings_ = settings;
    settings_.samplesX = std::clamp(settings_.samplesX, 1, 4);
    settings_.samplesY = std::clamp(settings_.samplesY, 1, 4);
}

void Renderer::beginFrame(int cols, int rows, const Camera& camera, const SceneLighting& lighting)
{
    cols_ = std::max(cols, 1);
    rows_ = std::max(rows, 1);
    sampleW_ = cols_ * settings_.samplesX;
    sampleH_ = rows_ * settings_.samplesY;
    camera_ = camera;
    lighting_ = lighting;
    stats_ = {};

    const float aspect = static_cast<float>(cols_) / (static_cast<float>(rows_) * settings_.cellAspect);
    view_ = camera.view();
    proj_ = camera.projection(aspect);
    viewProj_ = proj_ * view_;
    invViewProj_ = viewProj_.inverse();
    frustum_ = Frustum::fromMatrix(viewProj_);

    const std::size_t n = static_cast<std::size_t>(sampleW_) * static_cast<std::size_t>(sampleH_);
    depth_.assign(n, kInf);
    normal_.resize(n);
    position_.resize(n);
    material_.assign(n, kNoMaterial);

    // Keep the point lights nearest the camera that can affect visible space.
    lights_.clear();
    if (lighting.nightFactor > 0.01f) {
        for (const PointLight& l : lighting.pointLights) {
            const AABB reach = AABB::fromCenterExtents(l.position, {l.range, l.range, l.range});
            if (frustum_.intersects(reach) && distance(l.position, camera.position) < lighting.fogEnd) {
                lights_.push_back(l);
            }
        }
        const Vec3 eye = camera.position;
        const auto limit = static_cast<std::size_t>(std::max(settings_.maxPointLights, 0));
        if (lights_.size() > limit) {
            std::partial_sort(lights_.begin(), lights_.begin() + static_cast<std::ptrdiff_t>(limit), lights_.end(),
                              [&eye](const PointLight& a, const PointLight& b) {
                                  return lengthSq(a.position - eye) < lengthSq(b.position - eye);
                              });
            lights_.resize(limit);
        }
        for (PointLight& l : lights_) {
            l.intensity *= lighting.nightFactor;
        }
    }
}

void Renderer::drawMesh(const Mesh& mesh, const Mat4& model, MaterialId overrideMaterial)
{
    ++stats_.meshesSubmitted;
    if (mesh.empty() || !frustum_.intersects(mesh.bounds.transformed(model))) {
        ++stats_.meshesCulled;
        return;
    }

    const Mat4 mvp = viewProj_ * model;
    const Mat3 nrm = normalMatrix(model);
    transformed_.resize(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vertex& v = mesh.vertices[i];
        ClipVertex& cv = transformed_[i];
        cv.clip = mvp * Vec4(v.position, 1.0f);
        cv.world = model.transformPoint(v.position);
        cv.normal = nrm * v.normal;
    }

    const Vec3 eye = camera_.position;
    const std::size_t triCount = mesh.triangleCount();
    stats_.trianglesSubmitted += triCount;
    for (std::size_t t = 0; t < triCount; ++t) {
        const ClipVertex& a = transformed_[mesh.indices[t * 3]];
        const ClipVertex& b = transformed_[mesh.indices[t * 3 + 1]];
        const ClipVertex& c = transformed_[mesh.indices[t * 3 + 2]];
        const MaterialId mat = overrideMaterial != kNoMaterial ? overrideMaterial : mesh.triangleMaterials[t];

        const unsigned codeA = outcode(a.clip), codeB = outcode(b.clip), codeC = outcode(c.clip);
        if ((codeA & codeB & codeC) != 0) {
            continue; // entirely outside one plane
        }

        const Vec3 faceNormal = cross(b.world - a.world, c.world - a.world);
        const bool backfacing = dot(faceNormal, eye - a.world) <= 0.0f;
        if (backfacing) {
            if (!materials_.get(mat).doubleSided) {
                ++stats_.trianglesBackfaced;
                continue;
            }
            // Two-sided surface seen from behind: light the side we see.
            ClipVertex fa = a, fb = b, fc = c;
            fa.normal = -fa.normal;
            fb.normal = -fb.normal;
            fc.normal = -fc.normal;
            if ((codeA | codeB | codeC) == 0) {
                rasterize(fa, fb, fc, mat);
            } else {
                clipAndRasterize(fa, fb, fc, mat);
            }
            continue;
        }

        if ((codeA | codeB | codeC) == 0) {
            rasterize(a, b, c, mat);
        } else {
            clipAndRasterize(a, b, c, mat);
        }
    }
}

void Renderer::clipAndRasterize(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, MaterialId mat)
{
    ++stats_.trianglesClipped;
    // Sutherland-Hodgman in homogeneous clip space. A triangle clipped by six
    // planes has at most 9 vertices.
    ClipVertex bufA[12], bufB[12];
    ClipVertex* in = bufA;
    ClipVertex* out = bufB;
    int count = 3;
    in[0] = a;
    in[1] = b;
    in[2] = c;

    for (int p = 0; p < 6 && count > 0; ++p) {
        int outCount = 0;
        for (int i = 0; i < count; ++i) {
            const ClipVertex& cur = in[i];
            const ClipVertex& nxt = in[(i + 1) % count];
            const float dc = planeDistance(cur.clip, p);
            const float dn = planeDistance(nxt.clip, p);
            if (dc >= 0.0f) {
                out[outCount++] = cur;
            }
            if ((dc >= 0.0f) != (dn >= 0.0f)) {
                const float t = dc / (dc - dn);
                ClipVertex v;
                v.clip = cur.clip + (nxt.clip - cur.clip) * t;
                v.world = lerp(cur.world, nxt.world, t);
                v.normal = lerp(cur.normal, nxt.normal, t);
                out[outCount++] = v;
            }
        }
        std::swap(in, out);
        count = outCount;
    }

    for (int i = 1; i + 1 < count; ++i) {
        rasterize(in[0], in[i], in[i + 1], mat);
    }
}

void Renderer::rasterize(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, MaterialId mat)
{
    struct ScreenVertex {
        float x, y, invW;
        Vec3 worldOverW, normalOverW;
    };
    const ClipVertex* src[3] = {&a, &b, &c};
    ScreenVertex sv[3];
    const float sw = static_cast<float>(sampleW_);
    const float sh = static_cast<float>(sampleH_);
    for (int i = 0; i < 3; ++i) {
        const Vec4& cl = src[i]->clip;
        if (cl.w <= 1e-6f) {
            return; // degenerate after clipping
        }
        const float invW = 1.0f / cl.w;
        sv[i].x = (cl.x * invW * 0.5f + 0.5f) * sw;
        sv[i].y = (0.5f - cl.y * invW * 0.5f) * sh;
        sv[i].invW = invW;
        sv[i].worldOverW = src[i]->world * invW;
        sv[i].normalOverW = src[i]->normal * invW;
    }

    auto edge = [](const ScreenVertex& p, const ScreenVertex& q, float x, float y) {
        return (x - p.x) * (q.y - p.y) - (y - p.y) * (q.x - p.x);
    };
    float area = edge(sv[0], sv[1], sv[2].x, sv[2].y);
    if (std::fabs(area) < 1e-9f) {
        return;
    }
    const float sign = area > 0.0f ? 1.0f : -1.0f;
    area *= sign;
    const float invArea = 1.0f / area;

    const float minXf = std::min({sv[0].x, sv[1].x, sv[2].x});
    const float maxXf = std::max({sv[0].x, sv[1].x, sv[2].x});
    const float minYf = std::min({sv[0].y, sv[1].y, sv[2].y});
    const float maxYf = std::max({sv[0].y, sv[1].y, sv[2].y});
    const int minX = std::max(0, static_cast<int>(std::floor(minXf)));
    const int maxX = std::min(sampleW_ - 1, static_cast<int>(std::ceil(maxXf)));
    const int minY = std::max(0, static_cast<int>(std::floor(minYf)));
    const int maxY = std::min(sampleH_ - 1, static_cast<int>(std::ceil(maxYf)));
    if (minX > maxX || minY > maxY) {
        return;
    }

    ++stats_.trianglesRasterized;

    // Edge function increments per sample step.
    const float stepX0 = (sv[2].y - sv[1].y) * sign, stepY0 = -(sv[2].x - sv[1].x) * sign;
    const float stepX1 = (sv[0].y - sv[2].y) * sign, stepY1 = -(sv[0].x - sv[2].x) * sign;
    const float stepX2 = (sv[1].y - sv[0].y) * sign, stepY2 = -(sv[1].x - sv[0].x) * sign;

    const float startX = static_cast<float>(minX) + 0.5f;
    const float startY = static_cast<float>(minY) + 0.5f;
    float rowW0 = edge(sv[1], sv[2], startX, startY) * sign;
    float rowW1 = edge(sv[2], sv[0], startX, startY) * sign;
    float rowW2 = edge(sv[0], sv[1], startX, startY) * sign;

    for (int y = minY; y <= maxY; ++y) {
        float w0 = rowW0, w1 = rowW1, w2 = rowW2;
        std::size_t idx = index(minX, y);
        for (int x = minX; x <= maxX; ++x, ++idx) {
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                const float b0 = w0 * invArea, b1 = w1 * invArea, b2 = w2 * invArea;
                const float invW = b0 * sv[0].invW + b1 * sv[1].invW + b2 * sv[2].invW;
                const float depth = 1.0f / invW;
                if (depth < depth_[idx]) {
                    depth_[idx] = depth;
                    position_[idx] =
                        (sv[0].worldOverW * b0 + sv[1].worldOverW * b1 + sv[2].worldOverW * b2) * depth;
                    normal_[idx] = (sv[0].normalOverW * b0 + sv[1].normalOverW * b1 + sv[2].normalOverW * b2);
                    material_[idx] = mat;
                    ++stats_.samplesWritten;
                }
            }
            w0 += stepX0;
            w1 += stepX1;
            w2 += stepX2;
        }
        rowW0 += stepY0;
        rowW1 += stepY1;
        rowW2 += stepY2;
    }
}

Vec3 Renderer::skyGradient(const Vec3& dir) const
{
    const SceneLighting& L = lighting_;
    Vec3 c;
    if (dir.y >= 0.0f) {
        c = lerp(L.skyHorizon, L.skyZenith, std::sqrt(dir.y));
    } else {
        c = lerp(L.skyHorizon, L.skyHorizon * 0.55f, saturate(-dir.y * 4.0f));
    }
    if (L.sunVisible) {
        // s^48 and s^6 by repeated squaring (pow() per sky cell is costly).
        const float s = std::max(0.0f, dot(dir, L.sunDiscDirection));
        const float s2 = s * s, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8, s32 = s16 * s16;
        c += L.sunDiscColor * (s32 * s16 * 0.45f + s4 * s2 * 0.12f);
    }
    return c;
}

Vec3 Renderer::cellRayDirection(int cx, int cy) const
{
    const float ndcX = (static_cast<float>(cx) + 0.5f) / static_cast<float>(cols_) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (static_cast<float>(cy) + 0.5f) / static_cast<float>(rows_) * 2.0f;
    const Vec4 nearH = invViewProj_ * Vec4{ndcX, ndcY, -1.0f, 1.0f};
    const Vec4 farH = invViewProj_ * Vec4{ndcX, ndcY, 1.0f, 1.0f};
    const Vec3 nearP = nearH.xyz() * (1.0f / nearH.w);
    const Vec3 farP = farH.xyz() * (1.0f / farH.w);
    return normalize(farP - nearP);
}

Vec3 Renderer::skyCell(const Vec3& dir, char32_t& glyph) const
{
    const SceneLighting& L = lighting_;
    Vec3 c = skyGradient(dir);
    glyph = U' ';

    if (L.sunVisible && dot(dir, L.sunDiscDirection) > 0.9990f) {
        glyph = U'@';
        return L.sunDiscColor * 1.3f;
    }
    if (L.moonVisible) {
        const Vec3 moonDir = normalize(Vec3{-L.sunDiscDirection.x, -L.sunDiscDirection.y, -L.sunDiscDirection.z + 0.3f});
        if (dot(dir, moonDir) > 0.9993f) {
            glyph = U'O';
            return {0.85f, 0.88f, 1.0f};
        }
    }

    if (settings_.clouds && dir.y > 0.02f && L.cloudCover > 0.0f) {
        const float drift = L.time * 0.01f;
        const float u = dir.x / dir.y * 0.6f + drift;
        const float v = dir.z / dir.y * 0.6f;
        const float n = valueNoise(u, v) * 0.55f + valueNoise(u * 2.3f, v * 2.3f) * 0.30f +
                        valueNoise(u * 5.1f, v * 5.1f) * 0.15f;
        const float threshold = 1.0f - L.cloudCover;
        const float density = smoothstep(threshold, threshold + 0.22f, n) * smoothstep(0.02f, 0.20f, dir.y);
        if (density > 0.02f) {
            c = lerp(c, L.cloudColor * (0.75f + 0.35f * n), density * 0.85f);
            glyph = defaultRamp_->pick(density * 0.55f);
            return c;
        }
    }

    if (L.nightFactor > 0.3f && dir.y > 0.05f) {
        const auto qx = static_cast<int>(std::floor(dir.x * 140.0f));
        const auto qy = static_cast<int>(std::floor(dir.y * 140.0f));
        const auto qz = static_cast<int>(std::floor(dir.z * 140.0f));
        const std::uint32_t h = hashCombine(hash2i(0x57a25u, qx, qy), static_cast<std::uint32_t>(qz));
        if ((h & 0x3FF) < 6) {
            glyph = (h & 0x400) ? U'*' : U'.';
            return lerp(c, Vec3{1.0f, 1.0f, 0.95f}, L.nightFactor);
        }
    }
    return c;
}

Vec3 Renderer::shadeSample(std::size_t i, float& intensity, const CharRamp*& ramp) const
{
    const Material& m = materials_.get(material_[i]);
    const SceneLighting& L = lighting_;
    const Vec3 p = position_[i];
    Vec3 n = normalize(normal_[i]);
    const Vec3 toEye = camera_.position - p;
    const float dist = length(toEye);
    const Vec3 V = dist > 1e-6f ? toEye / dist : Vec3{0, 1, 0};
    ramp = m.ramp ? m.ramp : defaultRamp_;

    if (m.water) {
        const float t = L.time;
        n = normalize(n + Vec3{std::sin(p.x * 0.9f + t * 1.7f) * 0.14f + std::sin(p.z * 1.9f - t * 1.1f) * 0.07f, 0.0f,
                               std::cos(p.z * 0.8f + t * 1.3f) * 0.14f + std::cos(p.x * 2.1f + t * 0.7f) * 0.07f});
    }

    Vec3 light;
    Vec3 color;
    float sunTerm = 0.0f;
    if (m.unlit) {
        light = {1.0f, 1.0f, 1.0f};
        color = m.albedo;
    } else {
        light = lerp(L.groundAmbient, L.skyAmbient, n.y * 0.5f + 0.5f);
        sunTerm = std::max(0.0f, dot(n, L.toSun));
        light += L.sunColor * sunTerm;
        light += Vec3{1.0f, 1.0f, 1.0f} * (settings_.cameraFill * std::max(0.0f, dot(n, V)));
        for (const PointLight& pl : lights_) {
            const Vec3 d = pl.position - p;
            const float dsq = lengthSq(d);
            if (dsq >= pl.range * pl.range) {
                continue;
            }
            const float dl = std::sqrt(dsq);
            const float falloff = 1.0f - dl / pl.range;
            const float wrap = dl > 1e-4f ? std::max(0.0f, dot(n, d / dl)) * 0.75f + 0.25f : 1.0f;
            light += pl.color * (falloff * falloff * pl.intensity * wrap);
        }
        color = m.albedo * light;
        if (m.specular > 0.0f && sunTerm > 0.0f) {
            const Vec3 h = normalize(L.toSun + V);
            color += L.sunColor * (std::pow(std::max(dot(n, h), 0.0f), m.shininess) * m.specular);
        }
        if (m.reflectivity > 0.0f) {
            const float facing = std::max(dot(n, V), 0.0f);
            const float f = m.reflectivity * (0.35f + 0.65f * std::pow(1.0f - facing, 3.0f));
            const Vec3 r = reflect(-V, n);
            color = color * (1.0f - f) + skyGradient(r) * f;
        }
    }

    const Vec3 emissive = m.nightEmissive ? m.emissive * L.nightFactor : m.emissive;
    color += emissive;

    float jitter = 0.0f;
    if (m.noise > 0.0f) {
        jitter = positionHash(p) * m.noise;
        color *= 1.0f + jitter;
    }

    const float lum = luminance(light);
    intensity = (1.0f - std::exp(-lum * 1.1f)) * m.glyphScale + luminance(emissive) * 0.8f + jitter * 0.8f;

    const float fog = smoothstep(L.fogStart, L.fogEnd, dist);
    if (fog > 0.0f) {
        color = lerp(color, L.skyHorizon, fog);
        intensity *= 1.0f - fog * 0.8f;
    }
    intensity = saturate(intensity);
    return color;
}

void Renderer::resolve(Framebuffer& fb)
{
    if (fb.width() != cols_ || fb.height() != rows_) {
        fb.resize(cols_, rows_);
    }
    const int sx = settings_.samplesX;
    const int sy = settings_.samplesY;
    const float samplesPerCell = static_cast<float>(sx * sy);
    cellDepth_.assign(static_cast<std::size_t>(cols_) * static_cast<std::size_t>(rows_), kInf);

    for (int cy = 0; cy < rows_; ++cy) {
        for (int cx = 0; cx < cols_; ++cx) {
            Vec3 colorSum;
            float intensitySum = 0.0f;
            int covered = 0;
            float frontDepth = kInf;
            const CharRamp* frontRamp = defaultRamp_;
            MaterialId frontMat = kNoMaterial;

            for (int j = 0; j < sy; ++j) {
                for (int i = 0; i < sx; ++i) {
                    const std::size_t idx = index(cx * sx + i, cy * sy + j);
                    if (material_[idx] == kNoMaterial) {
                        continue;
                    }
                    float inten = 0.0f;
                    const CharRamp* ramp = nullptr;
                    colorSum += shadeSample(idx, inten, ramp);
                    intensitySum += inten;
                    ++covered;
                    if (depth_[idx] < frontDepth) {
                        frontDepth = depth_[idx];
                        frontRamp = ramp;
                        frontMat = material_[idx];
                    }
                }
            }

            Cell& cell = fb.at(cx, cy);
            if (covered < sx * sy) {
                char32_t skyGlyph = U' ';
                const Vec3 dir = cellRayDirection(cx, cy);
                const Vec3 sky = skyCell(dir, skyGlyph);
                if (covered == 0) {
                    cell.ch = skyGlyph;
                    cell.bg = skyGlyph == U' ' ? toColor(sky) : toColor(skyGradient(dir));
                    cell.fg = toColor(sky * 1.15f);
                    cell.depth = kInf;
                    cell.material = kNoMaterial;
                    continue;
                }
                colorSum += sky * static_cast<float>(sx * sy - covered);
            }

            const Vec3 color = colorSum * (1.0f / samplesPerCell);
            const float coverage = static_cast<float>(covered) / samplesPerCell;
            const float intensity = intensitySum / static_cast<float>(covered) * (0.55f + 0.45f * coverage);
            cell.ch = frontRamp->pick(intensity);
            cell.fg = toColor(color * settings_.foregroundBoost);
            cell.bg = toColor(color * settings_.backgroundLevel);
            cell.depth = frontDepth;
            cell.material = frontMat;
            cellDepth_[static_cast<std::size_t>(cy * cols_ + cx)] = frontDepth;
        }
    }

    if (!settings_.outlines) {
        return;
    }
    // Silhouettes: a cell is an edge if a neighbour is much further away.
    auto farther = [this](int x, int y, float d) {
        if (x < 0 || y < 0 || x >= cols_ || y >= rows_) {
            return false;
        }
        const float nd = cellDepth_[static_cast<std::size_t>(y * cols_ + x)];
        return nd > d * 1.15f + 0.8f;
    };
    for (int cy = 0; cy < rows_; ++cy) {
        for (int cx = 0; cx < cols_; ++cx) {
            const float d = cellDepth_[static_cast<std::size_t>(cy * cols_ + cx)];
            if (d == kInf) {
                continue;
            }
            if (farther(cx - 1, cy, d) || farther(cx + 1, cy, d) || farther(cx, cy - 1, d) || farther(cx, cy + 1, d)) {
                Cell& cell = fb.at(cx, cy);
                cell.bg = scale(cell.bg, 0.45f);
                cell.fg = scale(cell.fg, 0.85f);
            }
        }
    }
}

} // namespace av
