#include "Graphics/Mesh.h"

#include <algorithm>
#include <cmath>

namespace av {

void Mesh::computeBounds()
{
    bounds = AABB{};
    for (const Vertex& v : vertices) {
        bounds.expand(v.position);
    }
}

void Mesh::clear()
{
    vertices.clear();
    indices.clear();
    triangleMaterials.clear();
    bounds = AABB{};
}

std::uint32_t MeshBuilder::addVertex(const Mat4& xf, const Mat3& nxf, const Vec3& p, const Vec3& n)
{
    mesh_.vertices.push_back({xf.transformPoint(p), normalize(nxf * n)});
    const Vec3& wp = mesh_.vertices.back().position;
    mesh_.bounds.expand(wp);
    return static_cast<std::uint32_t>(mesh_.vertices.size() - 1);
}

void MeshBuilder::addIndexedTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c, MaterialId mat)
{
    mesh_.indices.push_back(a);
    mesh_.indices.push_back(b);
    mesh_.indices.push_back(c);
    mesh_.triangleMaterials.push_back(mat);
}

void MeshBuilder::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, MaterialId mat)
{
    const Vec3 n = normalize(cross(b - a, c - a));
    const Mat4 id;
    const Mat3 nid;
    const auto ia = addVertex(id, nid, a, n);
    const auto ib = addVertex(id, nid, b, n);
    const auto ic = addVertex(id, nid, c, n);
    addIndexedTriangle(ia, ib, ic, mat);
}

void MeshBuilder::addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, MaterialId mat)
{
    const Vec3 n = normalize(cross(b - a, c - a));
    const Mat4 id;
    const Mat3 nid;
    const auto ia = addVertex(id, nid, a, n);
    const auto ib = addVertex(id, nid, b, n);
    const auto ic = addVertex(id, nid, c, n);
    const auto idd = addVertex(id, nid, d, n);
    addIndexedTriangle(ia, ib, ic, mat);
    addIndexedTriangle(ia, ic, idd, mat);
}

void MeshBuilder::addBox(const AABB& box, MaterialId mat, const Mat4& xf, unsigned faces)
{
    const Mat3 nxf = normalMatrix(xf);
    const Vec3& lo = box.min;
    const Vec3& hi = box.max;
    // Each face: normal and four corners, counter-clockwise from outside.
    struct Face {
        unsigned bit;
        Vec3 n;
        Vec3 c[4];
    };
    const Face faceList[6] = {
        {FacePosX, {1, 0, 0}, {{hi.x, lo.y, hi.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}}},
        {FaceNegX, {-1, 0, 0}, {{lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}}},
        {FacePosY, {0, 1, 0}, {{lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z}}},
        {FaceNegY, {0, -1, 0}, {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}}},
        {FacePosZ, {0, 0, 1}, {{lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}}},
        {FaceNegZ, {0, 0, -1}, {{hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}}},
    };
    for (const Face& f : faceList) {
        if ((faces & f.bit) == 0) {
            continue;
        }
        const auto a = addVertex(xf, nxf, f.c[0], f.n);
        const auto b = addVertex(xf, nxf, f.c[1], f.n);
        const auto c = addVertex(xf, nxf, f.c[2], f.n);
        const auto d = addVertex(xf, nxf, f.c[3], f.n);
        addIndexedTriangle(a, b, c, mat);
        addIndexedTriangle(a, c, d, mat);
    }
}

void MeshBuilder::addEllipsoid(const Vec3& center, const Vec3& radii, int rings, int segments, MaterialId mat,
                               const Mat4& xf)
{
    rings = std::max(rings, 2);
    segments = std::max(segments, 3);
    const Mat3 nxf = normalMatrix(xf);
    const auto first = static_cast<std::uint32_t>(mesh_.vertices.size());

    for (int r = 0; r <= rings; ++r) {
        const float phi = kPi * static_cast<float>(r) / static_cast<float>(rings); // 0 = top
        const float sy = std::cos(phi), sr = std::sin(phi);
        for (int s = 0; s <= segments; ++s) {
            const float theta = kTwoPi * static_cast<float>(s) / static_cast<float>(segments);
            const Vec3 unit{sr * std::sin(theta), sy, sr * std::cos(theta)};
            const Vec3 p = center + unit * radii;
            // Ellipsoid normal: gradient of (x/a)^2 + (y/b)^2 + (z/c)^2.
            const Vec3 n{unit.x / radii.x, unit.y / radii.y, unit.z / radii.z};
            addVertex(xf, nxf, p, n);
        }
    }
    const int stride = segments + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const auto a = first + static_cast<std::uint32_t>(r * stride + s);
            const auto b = a + static_cast<std::uint32_t>(stride);
            if (r != 0) {
                addIndexedTriangle(a, b, a + 1, mat);
            }
            if (r != rings - 1) {
                addIndexedTriangle(a + 1, b, b + 1, mat);
            }
        }
    }
}

void MeshBuilder::addCylinder(const Vec3& base, float height, float radiusBottom, float radiusTop, int segments,
                              MaterialId mat, bool caps, const Mat4& xf)
{
    segments = std::max(segments, 3);
    const Mat3 nxf = normalMatrix(xf);
    const auto first = static_cast<std::uint32_t>(mesh_.vertices.size());
    // Slope of the side so normals are correct for cones.
    const float slope = (radiusBottom - radiusTop) / std::max(height, 1e-6f);

    for (int s = 0; s <= segments; ++s) {
        const float theta = kTwoPi * static_cast<float>(s) / static_cast<float>(segments);
        const Vec3 dir{std::sin(theta), 0, std::cos(theta)};
        const Vec3 n = normalize(Vec3{dir.x, slope, dir.z});
        addVertex(xf, nxf, base + dir * radiusBottom, n);
        addVertex(xf, nxf, base + Vec3{0, height, 0} + dir * radiusTop, n);
    }
    for (int s = 0; s < segments; ++s) {
        const auto b0 = first + static_cast<std::uint32_t>(s * 2);
        const auto t0 = b0 + 1, b1 = b0 + 2, t1 = b0 + 3;
        addIndexedTriangle(b0, b1, t1, mat);
        addIndexedTriangle(b0, t1, t0, mat);
    }

    if (!caps) {
        return;
    }
    auto cap = [&](float y, float r, const Vec3& n, bool top) {
        if (r <= 0.0f) {
            return;
        }
        const auto centre = addVertex(xf, nxf, base + Vec3{0, y, 0}, n);
        const auto ring = static_cast<std::uint32_t>(mesh_.vertices.size());
        for (int s = 0; s <= segments; ++s) {
            const float theta = kTwoPi * static_cast<float>(s) / static_cast<float>(segments);
            addVertex(xf, nxf, base + Vec3{std::sin(theta) * r, y, std::cos(theta) * r}, n);
        }
        for (int s = 0; s < segments; ++s) {
            const auto a = ring + static_cast<std::uint32_t>(s);
            if (top) {
                addIndexedTriangle(centre, a, a + 1, mat);
            } else {
                addIndexedTriangle(centre, a + 1, a, mat);
            }
        }
    };
    cap(height, radiusTop, {0, 1, 0}, true);
    cap(0.0f, radiusBottom, {0, -1, 0}, false);
}

void MeshBuilder::addCapsule(float length, float r0, float r1, int segments, MaterialId mat, const Mat4& xf)
{
    segments = std::max(segments, 4);
    const Mat3 nxf = normalMatrix(xf);
    const int capRings = std::max(2, segments / 3);
    const auto first = static_cast<std::uint32_t>(mesh_.vertices.size());

    // Profile from top pole to bottom pole: (radius, y, normal y-component).
    struct ProfilePoint {
        float radius, y, ny, nr;
    };
    std::vector<ProfilePoint> profile;
    for (int i = 0; i <= capRings; ++i) { // top hemisphere
        const float phi = kHalfPi * static_cast<float>(i) / static_cast<float>(capRings);
        profile.push_back({r0 * std::sin(phi), r0 * std::cos(phi), std::cos(phi), std::sin(phi)});
    }
    for (int i = 0; i <= capRings; ++i) { // bottom hemisphere
        const float phi = kHalfPi + kHalfPi * static_cast<float>(i) / static_cast<float>(capRings);
        profile.push_back({r1 * std::sin(phi), -length + r1 * std::cos(phi), std::cos(phi), std::sin(phi)});
    }

    for (const ProfilePoint& pp : profile) {
        for (int s = 0; s <= segments; ++s) {
            const float theta = kTwoPi * static_cast<float>(s) / static_cast<float>(segments);
            const Vec3 dir{std::sin(theta), 0, std::cos(theta)};
            addVertex(xf, nxf, Vec3{dir.x * pp.radius, pp.y, dir.z * pp.radius},
                      Vec3{dir.x * pp.nr, pp.ny, dir.z * pp.nr});
        }
    }
    const int stride = segments + 1;
    const int rows = static_cast<int>(profile.size());
    for (int r = 0; r < rows - 1; ++r) {
        for (int s = 0; s < segments; ++s) {
            const auto a = first + static_cast<std::uint32_t>(r * stride + s);
            const auto b = a + static_cast<std::uint32_t>(stride);
            if (profile[static_cast<std::size_t>(r)].radius > 0.0f) {
                addIndexedTriangle(a, b, a + 1, mat);
            }
            if (profile[static_cast<std::size_t>(r + 1)].radius > 0.0f) {
                addIndexedTriangle(a + 1, b, b + 1, mat);
            }
        }
    }
}

void MeshBuilder::addTorus(const Vec3& center, float majorRadius, float minorRadius, int majorSegments,
                           int minorSegments, MaterialId mat, const Mat4& xf)
{
    majorSegments = std::max(majorSegments, 3);
    minorSegments = std::max(minorSegments, 3);
    const Mat3 nxf = normalMatrix(xf);
    const auto first = static_cast<std::uint32_t>(mesh_.vertices.size());
    for (int i = 0; i <= majorSegments; ++i) {
        const float u = kTwoPi * static_cast<float>(i) / static_cast<float>(majorSegments);
        const Vec3 ringDir{std::sin(u), 0, std::cos(u)};
        for (int j = 0; j <= minorSegments; ++j) {
            const float v = kTwoPi * static_cast<float>(j) / static_cast<float>(minorSegments);
            const Vec3 n = ringDir * std::cos(v) + Vec3{0, std::sin(v), 0};
            addVertex(xf, nxf, center + ringDir * majorRadius + n * minorRadius, n);
        }
    }
    const int stride = minorSegments + 1;
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            const auto a = first + static_cast<std::uint32_t>(i * stride + j);
            const auto b = a + static_cast<std::uint32_t>(stride);
            addIndexedTriangle(a, b + 1, a + 1, mat);
            addIndexedTriangle(a, b, b + 1, mat);
        }
    }
}

void MeshBuilder::append(const Mesh& other, const Mat4& xf, MaterialId overrideMat)
{
    const Mat3 nxf = normalMatrix(xf);
    const auto base = static_cast<std::uint32_t>(mesh_.vertices.size());
    mesh_.vertices.reserve(mesh_.vertices.size() + other.vertices.size());
    for (const Vertex& v : other.vertices) {
        addVertex(xf, nxf, v.position, v.normal);
    }
    for (std::size_t t = 0; t < other.triangleCount(); ++t) {
        addIndexedTriangle(base + other.indices[t * 3], base + other.indices[t * 3 + 1],
                           base + other.indices[t * 3 + 2],
                           overrideMat != kNoMaterial ? overrideMat : other.triangleMaterials[t]);
    }
}

} // namespace av
