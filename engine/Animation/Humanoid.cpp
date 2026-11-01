#include "Animation/Humanoid.h"

#include "Graphics/Renderer.h"

#include <iterator>

namespace av {

const Skeleton& humanoidSkeleton()
{
    static const Skeleton skeleton = [] {
        Skeleton s;
        // Offsets in metres; a ~1.83 m adult standing in the bind pose,
        // arms hanging straight down. Ankles end up ~4 cm above the ground.
        s.addJoint("Root", -1, {0, 0, 0});
        s.addJoint("Pelvis", JRoot, {0, 0.98f, 0});
        s.addJoint("Spine", JPelvis, {0, 0.10f, 0});
        s.addJoint("Chest", JSpine, {0, 0.20f, 0});
        s.addJoint("Neck", JChest, {0, 0.24f, 0});
        s.addJoint("Head", JNeck, {0, 0.08f, 0});
        s.addJoint("LeftShoulder", JChest, {0.07f, 0.20f, 0});
        s.addJoint("LeftUpperArm", JLShoulder, {0.12f, 0, 0});
        s.addJoint("LeftForearm", JLUpperArm, {0, -0.29f, 0});
        s.addJoint("LeftHand", JLForearm, {0, -0.26f, 0});
        s.addJoint("RightShoulder", JChest, {-0.07f, 0.20f, 0});
        s.addJoint("RightUpperArm", JRShoulder, {-0.12f, 0, 0});
        s.addJoint("RightForearm", JRUpperArm, {0, -0.29f, 0});
        s.addJoint("RightHand", JRForearm, {0, -0.26f, 0});
        s.addJoint("LeftHip", JPelvis, {0.095f, -0.06f, 0});
        s.addJoint("LeftThigh", JLHip, {0, 0, 0});
        s.addJoint("LeftShin", JLThigh, {0, -0.45f, 0});
        s.addJoint("LeftFoot", JLShin, {0, -0.43f, 0});
        s.addJoint("RightHip", JPelvis, {-0.095f, -0.06f, 0});
        s.addJoint("RightThigh", JRHip, {0, 0, 0});
        s.addJoint("RightShin", JRThigh, {0, -0.45f, 0});
        s.addJoint("RightFoot", JRShin, {0, -0.43f, 0});
        return s;
    }();
    return skeleton;
}

namespace {

// Shared body-part geometry in joint-local space, built once. Materials are
// assigned per character at draw time.
struct HumanoidMeshes {
    Mesh pelvis, abdomen, chest, shoulderL, shoulderR, neck, head, nose;
    Mesh upperArm, forearm, hand, thigh, shin, foot;
    Mesh hairShort, hairLong, hairAfro, hairPonytail;
    Mesh cap, capBrim, fedoraBrim, fedoraCrown, backpack, glasses;

    HumanoidMeshes()
    {
        constexpr MaterialId m = kNoMaterial;
        auto ell = [](Mesh& mesh, Vec3 c, Vec3 r, int rings = 6, int segs = 10) {
            MeshBuilder(mesh).addEllipsoid(c, r, rings, segs, m);
        };
        auto box = [](Mesh& mesh, Vec3 lo, Vec3 hi) { MeshBuilder(mesh).addBox(AABB(lo, hi), m); };

        ell(pelvis, {0, 0.0f, 0}, {0.165f, 0.12f, 0.11f});
        ell(abdomen, {0, 0.10f, 0}, {0.145f, 0.14f, 0.10f});
        ell(chest, {0, 0.11f, 0.005f}, {0.18f, 0.16f, 0.115f}, 7, 12);
        ell(shoulderL, {0.09f, -0.01f, 0}, {0.075f, 0.075f, 0.075f}, 5, 8);
        ell(shoulderR, {-0.09f, -0.01f, 0}, {0.075f, 0.075f, 0.075f}, 5, 8);
        MeshBuilder(neck).addCylinder({0, -0.04f, 0}, 0.13f, 0.052f, 0.048f, 8, m, false);
        ell(head, {0, 0.11f, 0.01f}, {0.095f, 0.115f, 0.105f}, 7, 12);
        box(nose, {-0.015f, 0.085f, 0.095f}, {0.015f, 0.125f, 0.125f});

        MeshBuilder(upperArm).addCapsule(0.29f, 0.058f, 0.047f, 8, m);
        MeshBuilder(forearm).addCapsule(0.26f, 0.046f, 0.036f, 8, m);
        ell(hand, {0, -0.065f, 0.005f}, {0.034f, 0.08f, 0.05f}, 4, 8);
        MeshBuilder(thigh).addCapsule(0.45f, 0.088f, 0.062f, 9, m);
        MeshBuilder(shin).addCapsule(0.43f, 0.060f, 0.045f, 8, m);
        ell(foot, {0, -0.01f, 0.055f}, {0.05f, 0.045f, 0.125f}, 4, 8);

        ell(hairShort, {0, 0.15f, -0.005f}, {0.103f, 0.085f, 0.112f});
        ell(hairLong, {0, 0.15f, -0.005f}, {0.106f, 0.088f, 0.115f});
        box(hairLong, {-0.095f, -0.07f, -0.115f}, {0.095f, 0.15f, -0.035f});
        ell(hairAfro, {0, 0.17f, -0.01f}, {0.145f, 0.13f, 0.145f}, 7, 12);
        ell(hairPonytail, {0, 0.15f, -0.005f}, {0.103f, 0.085f, 0.112f});
        MeshBuilder(hairPonytail).addCapsule(0.18f, 0.035f, 0.02f, 6, m,
                                             Mat4::translation({0, 0.15f, -0.12f}) * Mat4::rotationX(-0.5f));

        ell(cap, {0, 0.165f, 0}, {0.107f, 0.075f, 0.114f});
        box(capBrim, {-0.08f, 0.165f, 0.07f}, {0.08f, 0.18f, 0.19f});
        MeshBuilder(fedoraBrim).addCylinder({0, 0.17f, 0}, 0.015f, 0.17f, 0.17f, 12, m);
        MeshBuilder(fedoraCrown).addCylinder({0, 0.18f, 0}, 0.09f, 0.105f, 0.095f, 12, m);

        box(backpack, {-0.13f, -0.06f, -0.235f}, {0.13f, 0.24f, -0.10f});
        box(glasses, {-0.085f, 0.105f, 0.095f}, {0.085f, 0.14f, 0.118f});
    }
};

const HumanoidMeshes& meshes()
{
    static const HumanoidMeshes m;
    return m;
}

Vec3 toVec(Color c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f}; }


} // namespace

namespace {
const Color kSkinPalette[] = {{255, 224, 196}, {241, 194, 160}, {224, 172, 135}, {198, 134, 96},
                              {160, 104, 70},  {120, 78, 52},   {90, 58, 40}};
const Color kHairPalette[] = {{30, 25, 25}, {80, 50, 30}, {220, 190, 120}, {160, 70, 40}, {170, 170, 170}};
const Color kShirtPalette[] = {{240, 80, 150}, {40, 190, 190}, {240, 240, 240}, {250, 230, 90}, {110, 170, 240},
                               {255, 120, 100}, {180, 140, 230}, {30, 30, 35},  {210, 50, 50},  {90, 200, 120}};
const Color kTrouserPalette[] = {{235, 235, 230}, {210, 190, 150}, {40, 50, 90}, {70, 100, 150}, {30, 30, 35}, {130, 130, 135}};
const Color kShoePalette[] = {{245, 245, 245}, {30, 30, 30}, {100, 60, 30}, {200, 40, 40}};
const Color kHatPalette[] = {{245, 245, 240}, {30, 30, 30}, {220, 200, 140}, {210, 50, 60}};
const Color kPackPalette[] = {{40, 120, 200}, {220, 80, 40}, {60, 60, 60}, {240, 200, 40}};

// Bit fields: (offset, width).
struct Field {
    int offset, bits;
};
constexpr Field kSkinF{0, 3}, kHairF{3, 3}, kStyleF{6, 3}, kShirtF{9, 4}, kTrousersF{13, 3}, kShoesF{16, 2}, kHatF{18, 2},
    kHatColorF{20, 2}, kBackpackF{22, 1}, kPackF{23, 2}, kSleevesF{25, 1}, kShortsF{26, 1}, kGlassesF{27, 1};

int get(std::uint32_t code, Field f, int count)
{
    return static_cast<int>((code >> f.offset) & ((1u << f.bits) - 1u)) % count;
}
void put(std::uint32_t& code, Field f, int value)
{
    code |= (static_cast<std::uint32_t>(value) & ((1u << f.bits) - 1u)) << f.offset;
}
} // namespace

void Appearance::resolveColors()
{
    skin = kSkinPalette[skinIndex % kSkins];
    hair = kHairPalette[hairIndex % kHairColors];
    shirt = kShirtPalette[shirtIndex % kShirts];
    trousers = kTrouserPalette[trousersIndex % kTrousers];
    shoes = kShoePalette[shoesIndex % kShoes];
    hatColor = kHatPalette[hatIndex % kHatColors];
    backpackColor = kPackPalette[packIndex % kPacks];
}

Appearance Appearance::fromCode(std::uint32_t code)
{
    Appearance a;
    a.skinIndex = get(code, kSkinF, kSkins);
    a.hairIndex = get(code, kHairF, kHairColors);
    a.hair_style = static_cast<HairStyle>(get(code, kStyleF, kHairStyles));
    a.shirtIndex = get(code, kShirtF, kShirts);
    a.trousersIndex = get(code, kTrousersF, kTrousers);
    a.shoesIndex = get(code, kShoesF, kShoes);
    a.hat = static_cast<HatStyle>(get(code, kHatF, kHats));
    a.hatIndex = get(code, kHatColorF, kHatColors);
    a.backpack = get(code, kBackpackF, 2) != 0;
    a.packIndex = get(code, kPackF, kPacks);
    a.longSleeves = get(code, kSleevesF, 2) != 0;
    a.shorts = get(code, kShortsF, 2) != 0;
    a.sunglasses = get(code, kGlassesF, 2) != 0;
    a.resolveColors();
    return a;
}

std::uint32_t Appearance::code() const
{
    std::uint32_t c = 0;
    put(c, kSkinF, skinIndex);
    put(c, kHairF, hairIndex);
    put(c, kStyleF, static_cast<int>(hair_style));
    put(c, kShirtF, shirtIndex);
    put(c, kTrousersF, trousersIndex);
    put(c, kShoesF, shoesIndex);
    put(c, kHatF, static_cast<int>(hat));
    put(c, kHatColorF, hatIndex);
    put(c, kBackpackF, backpack ? 1 : 0);
    put(c, kPackF, packIndex);
    put(c, kSleevesF, longSleeves ? 1 : 0);
    put(c, kShortsF, shorts ? 1 : 0);
    put(c, kGlassesF, sunglasses ? 1 : 0);
    return c;
}

Appearance Appearance::random(Rng& rng)
{
    Appearance a;
    a.skinIndex = rng.rangeInt(0, kSkins - 1);
    a.hairIndex = rng.rangeInt(0, kHairColors - 1);
    a.shirtIndex = rng.rangeInt(0, kShirts - 1);
    a.trousersIndex = rng.rangeInt(0, kTrousers - 1);
    a.shoesIndex = rng.rangeInt(0, kShoes - 1);
    a.hatIndex = rng.rangeInt(0, kHatColors - 1);
    a.packIndex = rng.rangeInt(0, kPacks - 1);
    a.hair_style = static_cast<HairStyle>(rng.rangeInt(0, kHairStyles - 1));
    a.hat = rng.chance(0.25f) ? (rng.chance(0.5f) ? HatStyle::Cap : HatStyle::Fedora) : HatStyle::None;
    a.shorts = rng.chance(0.35f);
    a.longSleeves = rng.chance(0.3f);
    a.backpack = rng.chance(0.2f);
    a.sunglasses = rng.chance(0.4f);
    a.resolveColors();
    return a;
}

AppearanceMaterials registerAppearance(MaterialLibrary& library, const Appearance& a, const std::string& name)
{
    auto make = [&](const char* slot, Color c, float specular = 0.0f, float noise = 0.0f) {
        Material m;
        m.name = name + "/" + slot;
        m.albedo = toVec(c);
        m.specular = specular;
        m.shininess = 20.0f;
        m.noise = noise;
        return library.add(m);
    };
    AppearanceMaterials out;
    out.skin = make("skin", a.skin, 0.15f);
    out.hair = make("hair", a.hair, 0.1f, 0.15f);
    out.shirt = make("shirt", a.shirt);
    out.trousers = make("trousers", a.trousers);
    out.shoes = make("shoes", a.shoes, 0.4f);
    out.hat = make("hat", a.hatColor);
    out.backpack = make("backpack", a.backpackColor, 0.1f);
    out.glasses = make("glasses", {15, 15, 20}, 1.5f);
    return out;
}

HumanoidVisual::HumanoidVisual(const Appearance& a, const AppearanceMaterials& mat)
{
    const HumanoidMeshes& m = meshes();
    auto add = [this](int joint, const Mesh& mesh, MaterialId material) { parts_.push_back({joint, &mesh, material}); };

    add(JPelvis, m.pelvis, mat.trousers);
    add(JSpine, m.abdomen, mat.shirt);
    add(JChest, m.chest, mat.shirt);
    add(JLShoulder, m.shoulderL, mat.shirt);
    add(JRShoulder, m.shoulderR, mat.shirt);
    add(JNeck, m.neck, mat.skin);
    add(JHead, m.head, mat.skin);
    add(JHead, m.nose, mat.skin);

    const MaterialId forearm = a.longSleeves ? mat.shirt : mat.skin;
    const MaterialId shin = a.shorts ? mat.skin : mat.trousers;
    for (const int side : {0, 1}) {
        add(side ? JRUpperArm : JLUpperArm, m.upperArm, mat.shirt);
        add(side ? JRForearm : JLForearm, m.forearm, forearm);
        add(side ? JRHand : JLHand, m.hand, mat.skin);
        add(side ? JRThigh : JLThigh, m.thigh, mat.trousers);
        add(side ? JRShin : JLShin, m.shin, shin);
        add(side ? JRFoot : JLFoot, m.foot, mat.shoes);
    }

    switch (a.hair_style) {
    case HairStyle::Short: add(JHead, m.hairShort, mat.hair); break;
    case HairStyle::Long: add(JHead, m.hairLong, mat.hair); break;
    case HairStyle::Afro: add(JHead, m.hairAfro, mat.hair); break;
    case HairStyle::Ponytail: add(JHead, m.hairPonytail, mat.hair); break;
    case HairStyle::Bald: break;
    }
    if (a.hat == HatStyle::Cap) {
        add(JHead, m.cap, mat.hat);
        add(JHead, m.capBrim, mat.hat);
    } else if (a.hat == HatStyle::Fedora) {
        add(JHead, m.fedoraBrim, mat.hat);
        add(JHead, m.fedoraCrown, mat.hat);
    }
    if (a.backpack) {
        add(JChest, m.backpack, mat.backpack);
    }
    if (a.sunglasses) {
        add(JHead, m.glasses, mat.glasses);
    }
}

void HumanoidVisual::draw(Renderer& renderer, const Mat4& world, const std::vector<Mat4>& jointGlobals) const
{
    if (!renderer.isVisible(localBounds().transformed(world))) {
        return;
    }
    for (const Part& p : parts_) {
        renderer.drawMesh(*p.mesh, world * jointGlobals[static_cast<std::size_t>(p.joint)], p.material);
    }
}

} // namespace av
