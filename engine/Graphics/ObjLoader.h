#pragma once

#include "Graphics/Material.h"
#include "Graphics/Mesh.h"

#include <filesystem>
#include <string_view>

namespace av {

// Wavefront OBJ subset: v, vn, f (v, v/vt, v//vn, v/vt/vn; polygons are
// fan-triangulated; negative indices allowed), usemtl (looked up by name in
// the library; unknown names use `defaultMaterial`). Faces without normals
// get flat normals. Throws std::runtime_error with a line number on errors.
Mesh parseObj(std::string_view text, const MaterialLibrary& materials, MaterialId defaultMaterial);

Mesh loadObjFile(const std::filesystem::path& path, const MaterialLibrary& materials, MaterialId defaultMaterial);

} // namespace av
