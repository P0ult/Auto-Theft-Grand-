// The segmented stand-in for a person (until the skinned humanoid of humanoid.js is ported). Plain C++
// geometry in the game's own axes (the character faces +z). (The vehicle catalogue and models are in
// VehicleDefs.h and VehicleModels.h.)
#pragma once

#include "MeshBuf.h"
#include "VehicleDefs.h"

namespace atg {

// A simple segmented person: each part is modelled from its joint (at the origin) downwards, so it swings
// when its joint turns. Heights in metres for a 1.8 m figure standing at y = 0.
struct HumanPart { const char* name; MeshBuf mesh; double joint[3]; const char* parent; };
struct HumanLook { uint32_t skin, shirt, pants, shoes, hair; };
std::vector<HumanPart> BuildHuman(const HumanLook& look);

} // namespace atg
