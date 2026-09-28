// Vehicle catalogue (src/entities/vehicledefs.js, road cars) and the procedural car and character models.
// Plain C++ geometry in the game's own axes (x left, y up, z forward for vehicles; the character faces +z).
#pragma once

#include "MeshBuf.h"

namespace atg {

enum class EDrive : uint8_t { RWD, FWD, AWD };
struct CarDef {
	const char* id;
	const char* name;
	const char* body;
	double L, W, H, wheelbase, track, wheelR, clearance;
	double mass, force, top, grip;
	EDrive drive;
	double steer, brake;
	int rarity;
	double camDist, camHeight;
	std::vector<uint32_t> colors;
	bool police = false, taxi = false, military = false, hydraulics = false;
};
const std::vector<CarDef>& CarDefs();
const CarDef* FindCar(const std::string& id);

struct CarModel {
	MeshBuf paint;     // painted panels (tinted per car)
	MeshBuf trim;      // glass, lights, bumpers, grille, interior
	MeshBuf wheel;     // one wheel, axle along x, centred
	double seat[3];    // driver's seat (hips)
	double door[2];    // where you stand to get in (x, z)
	double cgH;        // height of the centre of gravity above the ground
};
CarModel BuildCarModel(const CarDef& d);

// A simple segmented person: each part is modelled from its joint (at the origin) downwards, so it swings
// when its joint turns. Heights in metres for a 1.8 m figure standing at y = 0.
struct HumanPart { const char* name; MeshBuf mesh; double joint[3]; const char* parent; };
struct HumanLook { uint32_t skin, shirt, pants, shoes, hair; };
std::vector<HumanPart> BuildHuman(const HumanLook& look);

} // namespace atg
