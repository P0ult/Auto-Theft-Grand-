// The game's event bus (src/core/events.js): one typed signal per event name the JavaScript emits. on()
// returns an id for off(); listeners run in the order they were added, over a copy (so a listener may add or
// remove others while an event is being sent).
#pragma once

#include "Core.h"
#include <functional>

namespace atg {

class Character;
class Vehicle;
class Animal;
struct DamageInfo;

template <typename... A>
class Signal {
public:
	int on(std::function<void(A...)> f) { const int id = ++next; fns.push_back({ id, std::move(f) }); return id; }
	void off(int id) { for (size_t i = 0; i < fns.size(); i++) if (fns[i].first == id) { fns.erase(fns.begin() + i); return; } }
	void emit(A... a) { const auto copy = fns; for (auto& f : copy) f.second(a...); }
	void clear() { fns.clear(); }
private:
	std::vector<std::pair<int, std::function<void(A...)>>> fns;
	int next = 0;
};

struct Events {
	// combat
	Signal<Character*, Character*, const std::string&, const std::string&> kill;     // killer, victim, weapon, part
	Signal<Character*, V3, const std::string&> gunshot;                               // shooter, muzzle, weapon id
	Signal<V3, double, Character*> explosion;                                          // position, radius, source
	Signal<Vehicle*, Character*, V3> vehicleShot;                                      // vehicle, shooter, point
	Signal<Character*, Character*, const std::string&> melee;                         // attacker, victim, weapon id
	Signal<Character*, const DamageInfo&> death;
	Signal<Character*, double, Character*> charDamaged;                               // who, damage, source
	// vehicles
	Signal<Vehicle*, Vehicle*, double> carCrash;                                       // A, B, impact
	Signal<Character*, Character*, Vehicle*> carjack;                                  // by, victim, vehicle
	Signal<Character*, Vehicle*> enteredVehicle, exitedVehicle;
	Signal<Character*, Vehicle*, double> pedHitByCar;                                  // person, car, closing speed
	Signal<Character*, Vehicle*> pedRunOver;
	Signal<Vehicle*, double, Character*> vehicleDamaged;                               // vehicle, amount, source
	Signal<Vehicle*> vehicleExploded, vehicleSunk;
	Signal<Vehicle*, int> trainArrived;                                                // train, station index
	Signal<> heliDown;
	// police, army, special
	Signal<int> wantedUp;
	Signal<> wantedCleared, playerDied, busted, armyDeployed, specialOn;
	// missions and features
	Signal<const std::string&> missionStart, missionPassed, missionFailed;
	Signal<const std::string&, double, Vehicle*> skateTrick;                          // trick, cash, board
	Signal<const std::string&, double> shopRobbed;                                     // shop key, amount
	Signal<double> shipSafe;
	Signal<> shipRaided;
	Signal<const std::string&> raceWon, purchase, propertyBought;
	Signal<Animal*> petAdopted;
	Signal<Animal*, Character*> animalKilled;
};

} // namespace atg
