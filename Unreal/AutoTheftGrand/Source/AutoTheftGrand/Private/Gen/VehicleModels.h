// Procedural car and truck models (port of src/entities/vehiclemodels.js): lofted bodywork with wheel arches,
// a glass greenhouse and painted pillars, lamps laid onto the curved nose and tail, bumpers, grilles, interior,
// wheels, an opening driver door, police light bars and taxi signs. Panels that can come off in a crash are
// separate parts. Coordinates are the vehicle's own: x to the left (the driver's side), y up, z forward.
#pragma once

#include "Loft.h"
#include "VehicleDefs.h"

namespace atg {

// which material a part is drawn with (vehiclemodels.js vehicleMaterials)
enum class EVMat : uint8_t { Paint, Glass, Trim, Chrome, Wheel, Head, Tail, Lightbar, TaxiSign, Beacon };

struct VPart {
	std::string name;   // body, hood, trunk, door, door2, bumperF, bumperR, glass, doorGlass, trim, chrome, head, tail, ...
	MeshBuf mesh;
	EVMat mat = EVMat::Trim;
	// where it hangs: "" = the body group; "door" = the driver's door pivot; "hood" / "trunk" = their hinges;
	// "rear0" / "rear1" = the armoured vans' rear doors
	std::string pivot;
	uint32_t color = 0;   // for the light bar halves, the taxi sign and the beacon
};

struct VehicleModel {
	std::vector<VPart> parts;
	Pt3 doorHinge{ 0, 0, 0 };        // the driver's door pivot (body frame); the door opens to -1.1 rad
	double doorMax = -1.1;
	Pt3 hoodHinge{ 0, 0, 0 }, trunkHinge{ 0, 0, 0 };
	bool hasHood = false, hasTrunk = false;
	MeshBuf wheel;                   // one wheel at the origin, axle along x (the right-hand ones are turned by PI)
	struct Wheel { double x, y, z; bool front; };
	std::vector<Wheel> wheels;
	struct RearDoor { Pt3 pivot; int side; };
	std::vector<RearDoor> rearDoors; // armoured vans
	bool lightbar = false;
	double beamZ = 0;                // where the headlight glow lies on the road (z, at y = 0.03)
	double seatHip = 0.09;           // a character's hips sit this far above a seat point
	std::vector<Pt3> seats;          // driver, passenger, rear left, rear right
	Pt3 doorPos{ 0, 0, 0 };          // where the driver stands to get in
	struct Hull { double roofX, roofY, roofZ0, roofZ1, beltY, cgH; } hull{};
	bool bumperBody = true;          // bumpers in body colour (otherwise chrome or dark trim)
};

// the model for a road vehicle (cars, vans, trucks; not bikes, boats or aircraft)
const VehicleModel& BuildVehicleModel(const VehicleDef& def);

} // namespace atg
