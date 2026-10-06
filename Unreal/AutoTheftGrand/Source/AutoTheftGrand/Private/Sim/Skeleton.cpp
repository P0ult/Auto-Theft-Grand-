#include "Skeleton.h"

namespace atg {

const char* const BONE_NAMES[Bone::COUNT] = { "hips", "spine", "chest", "neck", "head", "lUpperArm", "lForearm", "lHand", "rUpperArm", "rForearm", "rHand", "lThigh", "lShin", "lFoot", "rThigh", "rShin", "rFoot" };
const int BONE_PARENT[Bone::COUNT] = { -1, 0, 1, 2, 3, 2, 5, 6, 2, 8, 9, 0, 11, 12, 0, 14, 15 };

int BoneIndex(const std::string& name) {
	for (int i = 0; i < Bone::COUNT; i++) if (name == BONE_NAMES[i]) return i;
	return -1;
}

namespace {
const std::vector<uint32_t> SKIN = { 0xf1c7a5, 0xe0ac87, 0xc68863, 0xa86b4a, 0x8a5536, 0x5f3a24, 0x3f2618 };
const std::vector<uint32_t> HAIR = { 0x111111, 0x2b1a10, 0x4a2c16, 0x7a5230, 0xb08a52, 0x999999, 0x1a1a1a };
const std::vector<uint32_t> SHIRT = { 0xffffff, 0x222222, 0x8b1e1e, 0x1e3f8b, 0x2e7d32, 0xd4a017, 0x7b1fa2, 0x607d8b, 0xe57373, 0x4db6ac, 0xff8f00, 0x455a64, 0xc2b280, 0x3949ab };
const std::vector<uint32_t> PANTS = { 0x1f2a44, 0x2b2b2b, 0x4e4a45, 0x6d5c43, 0x1d3b5c, 0x8d8d8d, 0x3b2f2a, 0xc9b99a };
}

Appearance RandomAppearance(RNG& rng, int female, int64_t skin) {
	Appearance a;
	a.female = female >= 0 ? female != 0 : rng.Chance(0.42);
	a.skin = skin >= 0 ? (uint32_t)skin : rng.Pick(SKIN);
	a.hair = rng.Pick(HAIR);
	a.hairStyle = a.female ? rng.Pick(std::vector<std::string>{ "long", "ponytail", "bun", "long", "short" }) : rng.Pick(std::vector<std::string>{ "short", "short", "bald", "afro", "cap", "buzz" });
	a.shirt = rng.Pick(SHIRT);
	a.shirtType = rng.Pick(std::vector<std::string>{ "tee", "tee", "long", "tank", "jacket" });
	a.pants = rng.Pick(PANTS);
	a.shorts = rng.Chance(0.2);
	a.shoes = rng.Pick(std::vector<uint32_t>{ 0x111111, 0xeeeeee, 0x5a3a22, 0x333333, 0x8b0000 });
	a.hat = -1;
	a.build = a.female ? rng.Range(0.9, 1.0) : rng.Range(0.95, 1.18);
	a.height = a.female ? rng.Range(0.92, 1.0) : rng.Range(0.96, 1.07);
	a.glasses = rng.Chance(0.15);
	a.beard = !a.female && rng.Chance(0.2);
	a.jacketColor = rng.Pick(SHIRT);
	if (a.hairStyle == "cap") a.hat = rng.Pick(SHIRT);
	return a;
}

Appearance RandomAppearance(int female, int64_t skin) {
	RNG rng((uint32_t)(int64_t)std::floor(Rand() * 1e9));
	return RandomAppearance(rng, female, skin);
}

void RestOffsets(const Appearance& a, V3 out[Bone::COUNT]) {
	const double sw = a.female ? 0.17 : 0.2 * a.build;
	const double hw = a.female ? 0.105 : 0.095;
	const V3 o[Bone::COUNT] = {
		{ 0, 0.98, 0 }, { 0, 0.12, 0 }, { 0, 0.2, 0 }, { 0, 0.24, 0 }, { 0, 0.09, 0 },
		{ sw, 0.19, 0 }, { 0, -0.29, 0 }, { 0, -0.26, 0 },
		{ -sw, 0.19, 0 }, { 0, -0.29, 0 }, { 0, -0.26, 0 },
		{ hw, -0.06, 0 }, { 0, -0.44, 0 }, { 0, -0.42, 0 },
		{ -hw, -0.06, 0 }, { 0, -0.44, 0 }, { 0, -0.42, 0 },
	};
	for (int i = 0; i < Bone::COUNT; i++) out[i] = o[i];
}

void Pose::Update(const M4& root) {
	for (int i = 0; i < Bone::COUNT; i++) {
		const M4 local = M4::Compose(pos[i], rot[i]);
		world[i] = BONE_PARENT[i] >= 0 ? world[BONE_PARENT[i]] * local : root * local;
	}
}

} // namespace atg
