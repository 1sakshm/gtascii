#include "Graphics/ObjLoader.h"

#include "Core/FileUtil.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace av {

namespace {

struct FaceRef {
    int v = 0;
    int n = 0; // 0 = none
};

int resolveIndex(int idx, std::size_t count, int lineNo)
{
    // OBJ is 1-based; negative indices count back from the end.
    const int resolved = idx > 0 ? idx - 1 : static_cast<int>(count) + idx;
    if (idx == 0 || resolved < 0 || resolved >= static_cast<int>(count)) {
        throw std::runtime_error("OBJ line " + std::to_string(lineNo) + ": index out of range");
    }
    return resolved;
}

FaceRef parseFaceRef(const std::string& token, int lineNo)
{
    FaceRef ref;
    const std::size_t s1 = token.find('/');
    ref.v = std::atoi(token.substr(0, s1).c_str());
    if (s1 != std::string::npos) {
        const std::size_t s2 = token.find('/', s1 + 1);
        if (s2 != std::string::npos && s2 + 1 < token.size()) {
            ref.n = std::atoi(token.substr(s2 + 1).c_str());
        }
    }
    if (ref.v == 0) {
        throw std::runtime_error("OBJ line " + std::to_string(lineNo) + ": bad face vertex '" + token + "'");
    }
    return ref;
}

} // namespace

Mesh parseObj(std::string_view text, const MaterialLibrary& materials, MaterialId defaultMaterial)
{
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    Mesh mesh;
    MeshBuilder builder(mesh);
    MaterialId current = defaultMaterial;

    std::istringstream in{std::string(text)};
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (const auto hash = line.find('#'); hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream ls(line);
        std::string kind;
        if (!(ls >> kind)) {
            continue;
        }
        if (kind == "v") {
            Vec3 p;
            if (!(ls >> p.x >> p.y >> p.z)) {
                throw std::runtime_error("OBJ line " + std::to_string(lineNo) + ": bad vertex");
            }
            positions.push_back(p);
        } else if (kind == "vn") {
            Vec3 n;
            if (!(ls >> n.x >> n.y >> n.z)) {
                throw std::runtime_error("OBJ line " + std::to_string(lineNo) + ": bad normal");
            }
            normals.push_back(normalize(n));
        } else if (kind == "usemtl") {
            std::string name;
            ls >> name;
            const MaterialId id = materials.find(name);
            current = id != kNoMaterial ? id : defaultMaterial;
        } else if (kind == "f") {
            std::vector<FaceRef> refs;
            std::string token;
            while (ls >> token) {
                refs.push_back(parseFaceRef(token, lineNo));
            }
            if (refs.size() < 3) {
                throw std::runtime_error("OBJ line " + std::to_string(lineNo) + ": face needs 3+ vertices");
            }
            for (FaceRef& r : refs) {
                r.v = resolveIndex(r.v, positions.size(), lineNo);
                r.n = r.n != 0 ? resolveIndex(r.n, normals.size(), lineNo) + 1 : 0;
            }
            for (std::size_t k = 1; k + 1 < refs.size(); ++k) {
                const FaceRef tri[3] = {refs[0], refs[k], refs[k + 1]};
                const Vec3 a = positions[static_cast<std::size_t>(tri[0].v)];
                const Vec3 b = positions[static_cast<std::size_t>(tri[1].v)];
                const Vec3 c = positions[static_cast<std::size_t>(tri[2].v)];
                const bool haveNormals = tri[0].n && tri[1].n && tri[2].n;
                if (!haveNormals) {
                    builder.addTriangle(a, b, c, current);
                    continue;
                }
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (const FaceRef& r : tri) {
                    mesh.vertices.push_back(
                        {positions[static_cast<std::size_t>(r.v)], normals[static_cast<std::size_t>(r.n - 1)]});
                    mesh.bounds.expand(mesh.vertices.back().position);
                }
                mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
                mesh.triangleMaterials.push_back(current);
            }
        }
        // vt, o, g, s, mtllib: ignored (no textures in an ASCII renderer).
    }
    return mesh;
}

Mesh loadObjFile(const std::filesystem::path& path, const MaterialLibrary& materials, MaterialId defaultMaterial)
{
    const auto text = readTextFile(path);
    if (!text) {
        throw std::runtime_error("cannot read model: " + path.string());
    }
    try {
        return parseObj(*text, materials, defaultMaterial);
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
}

} // namespace av
