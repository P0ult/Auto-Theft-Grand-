// Multiplayer (port of src/net/net.js and avatar.js): share the world with other players.
//
// Every player publishes one small state object about a dozen times a second: who they are (nickname, colour,
// look), where they are and what their body is doing, the vehicle they're driving (if any), and a short rolling
// log of recent moments (shots, hits on other players, explosions, chat, kills). Each client keeps simulating its
// own world (traffic, pedestrians, police) and draws the other players into it: avatars posed from their state,
// and the cars / aircraft they drive as kinematic stand-ins. Health is each player's own business: a hit you land
// on someone is sent to them as a moment, and their client applies it (unless they switched player damage off).
//
// The state is the browser game's JSON, so this port and the browser can share a room. The transport (how the
// state travels: server.mjs's WebSocket relay) is the Unreal side's; the native tests use a loopback.
#pragma once

#include "Character.h"
#include "Json.h"
#include "Systems.h"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace atg {

class Game;
class Vehicle;
class NetSystem;
class NpcSync;
struct NpcPeer;

extern const uint32_t PLAYER_COLORS[8];

// How the state travels. join() may answer later (the network) or at once (the tests); the callbacks deliver
// other players' states (onPeer), their leaving (onLeft) and the link going up or down (onStatus).
class NetTransport {
public:
	virtual ~NetTransport() = default;
	std::string kind = "ws", label = "local server";
	virtual void join(const std::string& code, std::function<void(bool ok, const std::string& error)> done) = 0;
	virtual void send(const Json& state) = 0;      // (the player's state: the relay wraps it as { g: state })
	virtual void sendWorld(const Json& /*n*/) {}   // (the shared pedestrians and traffic)
	virtual void leave() = 0;
	virtual bool connected() const = 0;
	virtual std::string selfId() const = 0;
	std::function<void(const std::string& id, const Json& state, bool guest)> onPeer;
	std::function<void(const std::string& id)> onLeft;
	std::function<void(bool connected)> onStatus;
	std::function<void(const std::string& id, const Json& n)> onWorld;
};

// only well-formed appearance fields from the network (it's someone else's data)
Appearance SanitizeAppearance(const Json* ap);
Json AppearanceJson(const Appearance& a);

// Another player, as seen here: a character posed from the network state instead of simulated. Damage the local
// player deals to it is sent to its owner; everything else that would hurt it here is ignored.
class RemoteAvatar : public Character {
public:
	RemoteAvatar(Game& game, NetSystem& net, const std::string& peerId, const Appearance& look);
	NetSystem& net;
	std::string peerId;
	void setWeapon(const std::string& id);
	void update(double dt) override;
	bool takeDamage(double amount, const DamageInfo& info) override;
	void setDown(bool down, bool dead);
};

class NetSystem : public System {
public:
	explicit NetSystem(Game& game);
	~NetSystem() override;
	Game& game;

	std::string status = "offline"; // offline | connecting | online | error
	std::string error, code;
	bool linkUp = false;
	// the player's own settings (the Unreal side keeps them between runs)
	std::string name;
	int color = 0;
	bool pvp = true;

	struct Sample { double t; Json st; };
	struct Peer {
		std::string id, name;
		Json st;
		bool guest = false;
		std::string apKey;
		std::shared_ptr<RemoteAvatar> avatar;
		Ref<Vehicle> veh, oldVeh; // (the stand-ins; the vehicle list owns them)
		std::deque<Sample> buf;
		double lastEv = 0, last = 0;
		double actN = 0;
		uint32_t col = 0;
		std::shared_ptr<struct Blip> blip;
		std::shared_ptr<NpcPeer> npc; // (their shared pedestrians and traffic: NpcSync)
	};
	std::map<std::string, std::unique_ptr<Peer>> peers;

	// the chat and the system lines (the HUD draws the feed; html like the browser's, <b> for names)
	struct ChatLine { bool sys = false, me = false; std::string name, text; uint32_t col = 0xffffff; double t = 0; };
	std::deque<ChatLine> chat;
	bool chatOpen = false;

	bool online() const { return status == "online" && transport; }
	std::string selfId() const { return transport ? transport->selfId() : std::string(); }
	// connect with a transport the Unreal side made (or a test's loopback); false at once if it can't
	void connect(std::unique_ptr<NetTransport> t, const std::string& code = "");
	void disconnect(bool quiet = false);

	// hooks (combat.js, vehicle.js)
	void onShot(Character* shooter, const std::string& weaponId, const V3& from, const V3& to);
	void onExplosion(const V3& pos, double radius, const void* source, double vis);
	void sendHit(const std::string& peerId, double dmg, const DamageInfo& info);
	void sendVehicleHit(Vehicle* v, double dmg, Character* source);
	void say(const std::string& text);

	void tick(double dt); // (real time, after the frame's update)
	void reset() override;

	// the Online tab's list: everyone else, nearest first
	struct RosterRow { std::string id, name, where, vehicle; uint32_t col; double dist, hp, x, z; bool guest, dead, pvp, paused, story; };
	std::vector<RosterRow> roster() const;
	// the other players' avatars (the game's character list includes them)
	std::vector<Character*> avatars() const;
	double nowMs = 0; // (performance.now(): advanced by tick)
	std::shared_ptr<NpcSync> npc;  // (shared pedestrians, traffic and trains)
	double joinedAt = 0;

private:
	std::unique_ptr<NetTransport> transport;
	std::unique_ptr<NetTransport> retired; // (a transport that failed inside its own callback: freed next tick)
	struct Out { double t; Json e; };
	std::deque<Out> out;
	friend class NpcSync;
	double seq = 0, sendT = 0, shotT = -1e9, actN = 0, clockT = 0;
	const void* lastAct = nullptr;
	struct PendingHit { double dmg = 0; bool head = false, kd = false; Json imp; std::string w; };
	std::map<std::string, PendingHit> pendingHits;

	void emit(Json e);
	Json buildState();
	Json vehState(Vehicle* v, int seat);
	void send();
	void onPeer(const std::string& id, const Json& st, bool guest);
	Peer* addPeer(const std::string& id, const Json& st, bool guest, const std::string& apKey);
	void removePeer(const std::string& id, bool announce);
	void moments(Peer& P, const Json* ev);
	void takeHit(Peer& P, double dmg, bool head, bool kd, const Json* imp, const std::string& weapon);
	struct Pick { const Json* a; const Json* b; double k, ext; };
	Pick sample(const Peer& P, double now) const;
	void pose(Peer& P, double now, double dt);
	void poseVehicle(Peer& P, const Json& vs, const Json* va, const Json* vb, double k, double ext, double dt);
	void netStep(Vehicle* v, double dt);
	void parkProxy(Peer& P, Vehicle* v);
	void releaseProxy(Vehicle* v);
	void proxyExplode(Vehicle* v);
	void syncClock(double dt);
	void sys(const std::string& html);
	void pushChat(ChatLine m);
};

} // namespace atg
