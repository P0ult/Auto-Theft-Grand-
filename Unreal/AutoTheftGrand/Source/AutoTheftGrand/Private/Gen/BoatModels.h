// Procedural boat models (port of buildBoatModel in src/entities/boat.js): lofted planing hulls (a deep V that
// narrows to the stem, a flat transom) with cockpits, consoles, windscreens, cabins, outboards, the police
// boat's light bar and bow gun. Coordinates are the boat's own: x left, y up, z forward, the waterline at
// y = 0. Identical to the JavaScript: Tools/dumpboats.mjs against Tools/boatstest.cpp.
#pragma once

#include "MeshBuf.h"
#include "VehicleDefs.h"
#include "Loft.h"

namespace atg {

struct BoatModel {
	MeshBuf hull;
	MeshBuf deck;
	MeshBuf trim;
	MeshBuf glass;
	MeshBuf chrome;
	MeshBuf head;
	MeshBuf tail;

	// police light bar (two halves: red and blue cylinders)
	bool hasLightbar = false;
	MeshBuf lightRed;
	MeshBuf lightBlue;
	Pt3 lightRedPos{ 0, 0, 0 };
	Pt3 lightBluePos{ 0, 0, 0 };

	// police bow gun
	bool hasGun = false;
	MeshBuf gun;
	Pt3 gunPos{ 0, 0, 0 };

	Pt3 propPos{ 0, 0, 0 };

	std::vector<Pt3> seats;
	double seatHip = 0.4;
	Pt3 doorPos{ 0, 0, 0 };

	// hull numbers for the physics
	struct Hull {
		double draft = 0, free = 0, L = 0, z0 = 0;
	} hullData;
};

const BoatModel& BuildBoatModel(const VehicleDef& def);

} // namespace atg
