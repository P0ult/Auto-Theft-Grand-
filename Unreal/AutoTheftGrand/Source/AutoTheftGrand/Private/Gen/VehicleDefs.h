// The vehicle catalogue (port of src/entities/vehicledefs.js): road cars, two-wheelers, the skateboard,
// military trucks, boats, the tank, the train and the aircraft. Dimensions in metres, forces in newtons,
// speeds in m/s. Fields a kind doesn't use are NaN (numbers) or empty / false.
#pragma once

#include "GenMath.h"

namespace atg {

enum class EDrive : uint8_t { RWD, FWD, AWD };

struct VehicleDef {
	std::string id, name, body;
	std::string kind;      // "" (road vehicle), boat, tank, train, plane, jet, heli
	std::string bike;      // "" or moto, bicycle, board
	std::string boat;      // rib, speed, jetski, cruiser, police
	double L = 0, W = 0, H = 0, wheelbase = 0, track = 0, wheelR = 0, clearance = 0;
	double mass = 0, force = 0, top = 0, grip = 1;
	EDrive drive = EDrive::RWD;
	double steer = 0.5, brake = 1;
	int rarity = 0;
	double camDist = NaN(), camHeight = NaN();
	double health = NaN();
	std::vector<uint32_t> colors;
	bool police = false, taxi = false, military = false, hydraulics = false, armored = false, offroad = false;
	bool pedal = false, board = false, astride = false, weapons = false, tank = false, train = false, freight = false;
	bool aircraft = false, cargo = false;
	double livery = NaN();           // painted colour of police vehicles (hex as a number)
	double bulletMul = NaN(), blastMul = NaN();
	// boats
	double draft = NaN(), freeboard = NaN(), turn = NaN();
	// tank
	double accel = NaN();
	// aircraft
	double colW = NaN(), thrust = NaN(), vStall = NaN(), vMax = NaN(), vRotate = NaN(), pitchRate = NaN(), rollRate = NaN(), yawRate = NaN();
	double gearH = NaN(), drag = NaN(), maxDial = NaN(), rotorR = NaN(), skidH = NaN();

	double Health() const { return IsSet(health) ? health : 1000; }
	double CamDist(double fallback = 7.5) const { return IsSet(camDist) && camDist > 0 ? camDist : fallback; }
	double CamHeight(double fallback = 1.6) const { return IsSet(camHeight) && camHeight > 0 ? camHeight : fallback; }
};

// in the order the JavaScript object lists them (traffic pools are built in this order)
const std::vector<VehicleDef>& VehicleDefs();
const VehicleDef* FindVehicle(const std::string& id);
// TRAFFIC_POOL: [id, rarity] for everything with a rarity
const std::vector<std::pair<std::string, double>>& TrafficPool();
// BIKES: the two-wheelers (not the skateboard); BOATS
const std::vector<std::string>& BikeIds();
const std::vector<std::string>& BoatIds();

// (phase 1 names)
using CarDef = VehicleDef;
inline const std::vector<VehicleDef>& CarDefs() { return VehicleDefs(); }
inline const VehicleDef* FindCar(const std::string& id) { return FindVehicle(id); }

} // namespace atg
