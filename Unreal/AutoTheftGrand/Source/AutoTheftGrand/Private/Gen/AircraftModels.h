// Procedural aircraft and tank models (port of src/entities/aircraftmodels.js): lofted fuselages and airfoils,
// canopies, props and rotors that spin, retractable gear, afterburner flames, nav lights; the tank's hull,
// tracks, road wheels, turret, gun and hatch. Coordinates are the vehicle's own: x left, y up, z forward,
// standing on y = 0. Identical to the JavaScript: Tools/dumpaircraft.mjs against Tools/aircrafttest.cpp.
#pragma once

#include "Loft.h"
#include "MeshBuf.h"
#include "VehicleDefs.h"

namespace atg {

struct AircraftModel {
	double cgY = 1;
	bool matte = true;              // the body part: matteMaterial (true) or bodyMaterial(white) (false)
	std::vector<MeshBuf> parts;     // the body group's meshes in order: [body (vertex coloured), trim]
	bool hasGear = false; MeshBuf gear;
	bool hasGlass = false; MeshBuf glass; Pt3 glassAt{ 0, 0, 0 }; bool glassInDoor = false;
	struct Prop { Pt3 pos; char axis; double rate, disc; MeshBuf blade; };
	std::vector<Prop> props;
	struct Rotor { Pt3 pos; double disc; MeshBuf blade; MeshBuf hub; bool hasHub = false; };
	bool hasRotor = false; Rotor rotor, tailRotor;
	struct Flame { Pt3 pos; MeshBuf mesh; bool core; };
	std::vector<Flame> flames;
	struct Light { Pt3 pos; double r; int kind; }; // kind: 0 red, 1 green, 2 strobe
	std::vector<Light> lights;
	bool hasGun = false; Pt3 gunAt{ 0, 0, 0 }; MeshBuf gun;           // the Warhawk's chin turret
	// the door: a canopy (axis x) or the tank's hatch (axis z)
	char doorAxis = 0; Pt3 doorPivot{ 0, 0, 0 }; double doorMax = 0;
	// the tank
	bool tank = false;
	Pt3 turretAt{ 0, 0, 0 }, gunPivot{ 0, 0, 0 };
	MeshBuf turret, tankGun, hatch, roadWheel;
	std::vector<std::vector<Pt3>> wheelSets;
	std::vector<Pt3> seats, muzzles, pylons, pods;
	int visibleSeats = 0;
	Pt3 doorPos{ 0, 0, 0 };
};

// the model for an aircraft or the tank (type: the vehicle id; color: its paint)
const AircraftModel& BuildAircraftModel(const VehicleDef& def, uint32_t color, const std::string& type);

} // namespace atg
