// Shared NPCs (port of src/net/npcsync.js): every client keeps simulating the pedestrians and traffic it spawned,
// and publishes the ones near other players on a second channel (the "world" presence) about six times a second.
// Other clients draw them as stand-ins: pedestrians posed from that state, cars as kinematic proxies, both passing
// any damage done to them back to the owner, who applies it. Stealing a stand-in car hands it over. Spawning counts
// everyone's NPCs so crowds don't double where players meet, and an owner keeps NPCs alive while any player is near
// them. The trains follow whoever has been online longest (or the player driving one).
#pragma once

#include "Character.h"
#include "Json.h"
#include "Net.h"

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace atg {

class Game;
class NetSystem;
class NpcSync;
class Vehicle;

// appearance <-> the compact array the world channel carries
Json PackLook(const Appearance& a);
bool UnpackLook(const Json* q, Appearance& out);

// a stand-in pedestrian: posed from its owner's state; damage done to it goes to the owner
class NpcProxy : public Character {
public:
	NpcProxy(Game& game, NpcSync& sync, const std::string& owner, int id, const Appearance& look);
	NpcSync& sync;
	std::string owner;
	int npcWanted = 0;   // (an NPC another player's police are after: the stars over its head)
	double gone = 0;     // (seconds missing from its owner's state)
	void update(double dt) override;
	bool takeDamage(double amount, const DamageInfo& info) override;
	void setDown(bool down, bool isDead);
};

// what one other player shares
struct NpcPeer {
	struct Snap { double t = 0; std::map<int, std::vector<double>> cars, peds; Json tr; };
	std::deque<Snap> buf;
	std::map<int, Ref<Vehicle>> cars;                   // their cars, as our stand-ins (the vehicle list owns them)
	std::map<int, double> carGone;
	std::map<int, std::shared_ptr<NpcProxy>> peds;
	std::map<int, Json> statics;                        // type / colour / look per id
	std::map<int, double> taken;                        // cars we drove off in (ms)
	Json train;
};

class NpcSync {
public:
	explicit NpcSync(NetSystem& net);
	NetSystem& net;
	Game& game;
	std::vector<std::shared_ptr<NpcProxy>> pedList; // everyone else's stand-in pedestrians (the character list includes them)

	// for the spawners: other players near a point, and the stand-ins near a point
	int peersNear(double x, double z, double R) const;
	int proxyCarsNear(double x, double z, double R) const;
	int proxyPedsNear(double x, double z, double R) const;

	void send(double dt);
	void onWorld(NetSystem::Peer* P, const Json* n);
	void clearPeer(NetSystem::Peer& P);
	void update(double dt);
	void dropPed(Character* q);
	void dropCar(Vehicle* v);

	void sendPedHit(NpcProxy& q, double dmg, const DamageInfo& info);
	void sendCarHit(Vehicle* v, double dmg);
	void takeCar(Vehicle* v);                 // (the player got into another client's car: it's ours now)
	void onMoment(NetSystem::Peer& P, const Json& e);

private:
	double sendT = 0, seq = 0;
	int nextId = 1;
	bool sentAny = false;
	struct Shared { Ref<Character> c; Ref<Vehicle> v; };
	std::map<int, Shared> byId;            // our shared entities: netId -> ped / vehicle
	std::map<int, double> sentStatic;      // netId -> when we last sent its static info

	int idOf(Character* c);
	int idOf(Vehicle* v);
	Json build();
	Vehicle* adoptCar(NetSystem::Peer& P, int id, const std::string& type, double x, double z, bool& isDriven);
	void dedupeStatic(const NetSystem::Peer& P, Vehicle* v);
	bool isHost() const;
	Json trainState() const;
	void applyTrains(double dt);
	bool peerIsHost(const NetSystem::Peer& P) const;
};

} // namespace atg
