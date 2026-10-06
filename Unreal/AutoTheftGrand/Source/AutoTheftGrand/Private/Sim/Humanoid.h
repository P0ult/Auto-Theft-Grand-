// The humanoid's mesh (port of buildHumanoidGeometry in src/entities/humanoid.js): one smooth-skinned shell
// per person, built in the rest pose. The torso, arms and legs are lofted tubes whose rings blend between
// two bones; hands, shoes, the face, hair and hats are rigid parts on one bone. Every vertex carries its
// colour (linear), a material id for the shader (cloth, skin, hair, leather, eye, denim, metal) and up to
// four bone influences.
#pragma once

#include "Skeleton.h"

namespace atg {

namespace HumanMat { enum : int { cloth = 0, skin = 1, hair = 2, leather = 3, eye = 4, denim = 5, metal = 6 }; }

struct HumanoidMesh {
	std::vector<float> P, N, C;   // position, normal, colour (3 floats a vertex)
	std::vector<float> mat;       // material id
	std::vector<uint16_t> si;     // 4 bone indices a vertex
	std::vector<float> sw;        // 4 weights a vertex
	std::vector<uint32_t> I;
	V3 rest[Bone::COUNT];         // rest offsets (relative to the parent)
	V3 world[Bone::COUNT];        // rest positions in the mesh's frame
	size_t Count() const { return P.size() / 3; }
};

HumanoidMesh BuildHumanoidGeometry(const Appearance& a);

// three.js Color helpers (sRGB hex <-> linear), as the humanoid's colours are mixed
void HexToLinear(uint32_t hex, double out[3]);
uint32_t LinearToHex(const double c[3]);

} // namespace atg
