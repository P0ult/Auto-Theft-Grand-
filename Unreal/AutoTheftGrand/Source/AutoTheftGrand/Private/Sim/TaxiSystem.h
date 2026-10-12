// Los Soles Cabs (port of src/game/taxi.js).
//  - Whistle for a cab on foot (H): the nearest free cab pulls up at the kerb, or one is sent from a nearby
//    street. Get in the back (F), pick a destination (or use your map waypoint) and ride along with the
//    meter running. Space skips the trip for the estimated fare; F asks the driver to pull over.
//  - Climbing into the back of any cab that has a driver (G) starts a ride the same way.
//  - Taxi driver side job (J while driving a cab): fares wait at the kerb; pick them up and get them there
//    before the clock runs out, earning the fare plus a tip for speed. Every fifth fare in a row pays a bonus.
#pragma once

#include "Systems.h"
#include "Phone.h"
#include "Vehicle.h"
#include "Character.h"

namespace atg {

class Game;
class Ped;
class Marker;
class FreeRoam;

struct FreeDestination {
	std::string group, name;
	double x = 0, z = 0;
	double y = NaN(), yaw = NaN();
	bool foot = false;
	bool exactVeh = false;
	bool hasVeh = false; double vehX = 0, vehZ = 0, vehYaw = 0;
	bool platform = false;
};

constexpr double TAXI_FLAG = 4;    // $ when the meter starts
constexpr double TAXI_PER_KM = 9;  // $ per km driven

// a driver that just sits still (taxi.js HoldDriver: while the passenger chooses where to go)
class HoldDriver : public VehicleAI {
public:
	explicit HoldDriver(Vehicle* v) : veh(v) {}
	void update(double) override { veh->input.throttle = 0; veh->input.brake = 1; veh->input.steer = 0; veh->input.handbrake = veh->speedAbs() < 1; }
private:
	Vehicle* veh;
};

class TaxiSystem : public System {
public:
	explicit TaxiSystem(Game& g);
	Game& game;

	struct Hail { Ref<Vehicle> cab; double t = 0, wait = 0; double x = 0, z = 0, y = 0, yaw = 0; std::shared_ptr<Blip> blip; bool told = false; };
	struct Ride {
		Ref<Vehicle> cab; std::string state = "choose"; // choose, ride, leaving
		std::string destName;
		double tx = 0, tz = 0, ty = 0, tyaw = 0; // the target spot
		double dist = 0, fare = TAXI_FLAG, est = 0;
		V3 last;
		double t = 0;
		bool stopRequest = false, skipping = false, picked = false, picking = false;
	};
	struct Fare {
		Ref<Ped> ped; Marker* marker = nullptr;
		std::string stage = "waiting"; // waiting, boarding, riding, leaving
		double age = 0, px = 0, pz = 0;      // the waiting spot
		std::string destName; double tx = 0, tz = 0, ty = 0; // the destination spot
		double km = 0, limit = 0, timer = 0;
		bool late = false;
	};
	struct Job { Ref<Vehicle> cab; int fares = 0, streak = 0; double earned = 0, next = 1.5; std::unique_ptr<Fare> fare; };

	bool ctl() const;
	bool cabDriven(Vehicle* v) const;
	int rearSeat(Vehicle* v, const V3& pos) const;

	void update(double dt) override;
	void reset() override;

	// Game hooks (game.js tryEnterExit): ask the driver to pull over first; which seat for a hailed cab
	bool handleExit(Character* p);
	int seatFor(Vehicle* v, Character* p);

	// the phone's Downtown Cab Co.
	void hailCab();

private:
	std::unique_ptr<Hail> hail;
	std::unique_ptr<Ride> ride;
	std::pair<Ref<Vehicle>, double> held; // (a car waiting for you to climb in as a passenger)
	std::unique_ptr<Job> job;
	bool jobHinted = false;
	std::string tpGroup;
	std::string jobGroup;

	std::vector<FreeDestination> destinations() const;
	double estimate(double x, double z) const;

	void sendCab(Vehicle* cab);
	void updateHail(double dt);
	void clearHail(bool release, const char* msg = nullptr);
	void releaseCab(Vehicle* cab);
	void enterAsPassenger();
	void updateHeld(double dt);
	void beginRide(Vehicle* cab);
	void openPicker();
	void setDest(const std::string& name, double x, double z);
	void updateRide(double dt, bool ok);
	void skip();
	void arrive();
	void endRide();
	void charge(double amount, const char* label);
	void startJob(Vehicle* cab);
	void endJob(const char* msg = nullptr);
	void updateJob(double dt);
	Fare* spawnFare();
	void startFareTrip(Fare& f);
	void fareOut(Fare& f);
	void dropFare(Fare& f, bool despawn);
	std::vector<StoreItem> pickerRows();
	std::vector<StoreItem> jobRows();
};

} // namespace atg