#pragma once

#include "Graphics/Cell.h"
#include "Math/Geometry.h"
#include "Math/Mat.h"

#include <cstdint>
#include <vector>

namespace av {

struct Vertex {
    Vec3 position;
    Vec3 normal;
};

// Indexed triangle mesh with one material per triangle, so a single mesh
// (e.g. a whole city chunk) can mix surfaces and still be drawn in one call.
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;       // 3 per triangle, counter-clockwise = front
    std::vector<MaterialId> triangleMaterials; // 1 per triangle
    AABB bounds;

    std::size_t triangleCount() const { return triangleMaterials.size(); }
    bool empty() const { return triangleMaterials.empty(); }
    void computeBounds();
    void clear();
};

// Appends procedural geometry to a mesh. Every shape takes an optional
// transform applied to positions (and, correctly, to normals).
class MeshBuilder {
public:
    explicit MeshBuilder(Mesh& target) : mesh_(target) {}

    // Flat-shaded primitives (normal from winding).
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, MaterialId mat);
    // Corners in counter-clockwise order as seen from the front.
    void addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, MaterialId mat);

    enum BoxFace : unsigned {
        FaceNegX = 1, FacePosX = 2, FaceNegY = 4, FacePosY = 8, FaceNegZ = 16, FacePosZ = 32, AllFaces = 63,
        NoBottom = AllFaces & ~FaceNegY,
    };
    void addBox(const AABB& box, MaterialId mat, const Mat4& xf = Mat4::identity(), unsigned faces = AllFaces);

    // Smooth-shaded primitives.
    void addEllipsoid(const Vec3& center, const Vec3& radii, int rings, int segments, MaterialId mat,
                      const Mat4& xf = Mat4::identity());
    // Frustum of a cone along +Y from base; capped ends optional.
    void addCylinder(const Vec3& base, float height, float radiusBottom, float radiusTop, int segments,
                     MaterialId mat, bool caps = true, const Mat4& xf = Mat4::identity());
    // Tapered capsule from the origin down to (0, -length, 0): radius r0 at
    // the top, r1 at the bottom, with hemispherical ends. Used for limbs.
    void addCapsule(float length, float r0, float r1, int segments, MaterialId mat,
                    const Mat4& xf = Mat4::identity());
    // Torus around +Y.
    void addTorus(const Vec3& center, float majorRadius, float minorRadius, int majorSegments, int minorSegments,
                  MaterialId mat, const Mat4& xf = Mat4::identity());

    // Copies another mesh; optionally replaces all of its materials.
    void append(const Mesh& other, const Mat4& xf = Mat4::identity(), MaterialId overrideMat = kNoMaterial);

private:
    std::uint32_t addVertex(const Mat4& xf, const Mat3& nxf, const Vec3& p, const Vec3& n);
    void addIndexedTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c, MaterialId mat);

    Mesh& mesh_;
};

} // namespace av
