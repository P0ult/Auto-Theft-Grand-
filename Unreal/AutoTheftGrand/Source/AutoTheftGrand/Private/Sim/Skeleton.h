// The humanoid's bones and looks (the data half of src/entities/humanoid.js): 17 bones in a fixed order, their
// rest offsets for a given build, random appearances, and a small pose: local bone rotations and positions,
// and their world matrices under a root transform (the browser game's root group scaled by the height).
#pragma once

#include "Core.h"

namespace atg {

namespace Bone { enum : int { hips, spine, chest, neck, head, lUpperArm, lForearm, lHand, rUpperArm, rForearm, rHand, lThigh, lShin, lFoot, rThigh, rShin, rFoot, COUNT }; }
extern const char* const BONE_NAMES[Bone::COUNT];
extern const int BONE_PARENT[Bone::COUNT];
int BoneIndex(const std::string& name); // -1 if unknown

struct Appearance {
	bool female = false;
	uint32_t skin = 0x8a5536, hair = 0x111111;
	std::string hairStyle = "short";
	uint32_t shirt = 0xffffff;
	std::string shirtType = "tee";
	uint32_t pants = 0x1f2a44;
	bool shorts = false;
	uint32_t shoes = 0x111111;
	int64_t hat = -1;           // cap colour, or -1
	double build = 1, height = 1;
	bool glasses = false, beard = false;
	uint32_t jacketColor = 0x222222;
	int64_t bandana = -1;
	// uniforms (police, soldiers, guards): shirt / trousers, a peaked cap and its band
	bool hasUniform = false;
	uint32_t uniformShirt = 0, uniformPants = 0;
	int64_t uniformHat = -1, uniformBand = -1;
	bool noBadge = false;
	// vests: "hivis" or "tactical"
	std::string vest; int64_t vestColor = -1;
	int64_t mask = -1, hardhat = -1;
	std::string cacheKey;
};

// randomAppearance(rng, opts): the female / skin choices are made first, everything else after
Appearance RandomAppearance(RNG& rng, int female = -1, int64_t skin = -1);
Appearance RandomAppearance(int female = -1, int64_t skin = -1); // with a Math.random seed

// rest offsets (relative to the parent) for a 1.8 m person of this build
void RestOffsets(const Appearance& a, V3 out[Bone::COUNT]);

struct Pose {
	V3 pos[Bone::COUNT];   // local positions
	Quat rot[Bone::COUNT]; // local rotations
	M4 world[Bone::COUNT]; // after Update
	void Reset(const V3 rest[Bone::COUNT]) { for (int i = 0; i < Bone::COUNT; i++) { pos[i] = rest[i]; rot[i] = Quat(); } }
	// world matrices under the root (the character's own transform, including its height scale)
	void Update(const M4& root);
	V3 WorldPos(int bone) const { return world[bone].position(); }
	V3 WorldPoint(int bone, const V3& local) const { return world[bone].apply(local); }
};

} // namespace atg
