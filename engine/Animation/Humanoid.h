#pragma once

#include "Animation/Skeleton.h"
#include "Core/Random.h"
#include "Graphics/Color.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"

#include <string>
#include <vector>

namespace av {

class Renderer;

// Joint indices of the humanoid skeleton (matches the spec hierarchy:
// Root > Pelvis > Spine > Chest > Neck > Head / Shoulders > arms, Pelvis > Hips > legs).
// Character-local space: +Y up, +Z forward, +X is the character's left.
enum HumanoidJoint : int {
    JRoot, JPelvis, JSpine, JChest, JNeck, JHead,
    JLShoulder, JLUpperArm, JLForearm, JLHand,
    JRShoulder, JRUpperArm, JRForearm, JRHand,
    JLHip, JLThigh, JLShin, JLFoot,
    JRHip, JRThigh, JRShin, JRFoot,
    JCount
};

const Skeleton& humanoidSkeleton();

enum class HairStyle { Bald, Short, Long, Afro, Ponytail };
enum class HatStyle { None, Cap, Fedora };

// Modular look of a character. Every component gets its own material.
struct Appearance {
    Color skin{224, 172, 135};
    Color hair{60, 40, 30};
    Color shirt{240, 90, 140};
    Color trousers{230, 230, 220};
    Color shoes{245, 245, 245};
    Color hatColor{240, 240, 235};
    Color backpackColor{40, 120, 200};
    HairStyle hair_style = HairStyle::Short;
    HatStyle hat = HatStyle::None;
    bool shorts = false;
    bool longSleeves = false;
    bool backpack = false;
    bool sunglasses = true;

    // Palette indices behind the colours above (what code() encodes).
    int skinIndex = 2, hairIndex = 0, shirtIndex = 0, trousersIndex = 0, shoesIndex = 0, hatIndex = 0, packIndex = 0;

    static Appearance random(Rng& rng);
    // Every look packs into 28 bits, so a character travels over the network
    // as a single number. Any 32-bit value decodes to a valid appearance.
    static Appearance fromCode(std::uint32_t code);
    std::uint32_t code() const;
    void resolveColors(); // refresh colours from the palette indices

    // Palette sizes, for the character creator.
    enum : int { kSkins = 7, kHairColors = 5, kHairStyles = 5, kShirts = 10, kTrousers = 6, kShoes = 4, kHats = 3, kHatColors = 4, kPacks = 4 };
};

// Appearance materials registered in a MaterialLibrary.
struct AppearanceMaterials {
    MaterialId skin, hair, shirt, trousers, shoes, hat, backpack, glasses;
};

AppearanceMaterials registerAppearance(MaterialLibrary& library, const Appearance& appearance,
                                       const std::string& uniqueName);

// Renders a humanoid from joint matrices. Body-part meshes are procedural
// (ellipsoids, tapered capsules, boxes) and shared by all characters; each
// part is drawn rigidly attached to its joint with the appearance material.
class HumanoidVisual {
public:
    HumanoidVisual(const Appearance& appearance, const AppearanceMaterials& materials);

    void draw(Renderer& renderer, const Mat4& world, const std::vector<Mat4>& jointGlobals) const;

    // Model-space bounds for culling (generous, covers all poses).
    static AABB localBounds() { return AABB({-1.0f, -0.1f, -1.0f}, {1.0f, 2.3f, 1.0f}); }

private:
    struct Part {
        int joint;
        const Mesh* mesh;
        MaterialId material;
    };
    std::vector<Part> parts_;
};

} // namespace av
