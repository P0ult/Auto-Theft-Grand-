// Procedural two-wheeler models (port of src/entities/bikes.js): motorbikes and bicycles with frames,
// forks, wheels, cranks and a windscreen. Coordinates are the vehicle's own: x to the left, y up,
// z forward (the JavaScript axes).
#pragma once

#include "MeshBuf.h"
#include "VehicleDefs.h"
#include "Loft.h"

namespace atg {

struct BikeModel {
	MeshBuf body;              // the paint builder
	MeshBuf trim;              // frame, engine, exhaust, seat (dark parts)
	MeshBuf fork;              // the front fork (hangs in the front wheel's pivot)
	MeshBuf wheel;             // one wheel at the origin, axle along x
	MeshBuf crank;             // the bicycle's crank group (pedals + chainring)
	double crankY = 0, crankZ = 0;
	bool hasGlass = false;
	Mat4 glassMatrix;          // the motorbike's windscreen box (0.3 x 0.2 x 0.012) at its position and rotation
	double headY = 0, headZ = 0, tailY = 0, tailZ = 0;
	double headX = 0, headZSize = 0, tailX = 0, tailZSize = 0; // head / tail box sizes (x half-width, z half-depth)
	struct Wheel { double z; bool front; double baseY; };
	std::vector<Wheel> wheels;
	std::vector<Pt3> seats;
	double seatHip = 0.04;
	Pt3 doorPos{ 0, 0, 0 };
	double grips[2] = { 0, 0 };
	bool hasFeet = false;
	double feet[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
};

const BikeModel& BuildBikeModel(const VehicleDef& def);

} // namespace atg