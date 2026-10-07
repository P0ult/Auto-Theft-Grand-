// The game (port of src/game/game.js minus the renderer): owns the collision world, the player, the
// vehicles, the camera rig, the clock and weather, the input and the systems, and runs a frame in the
// browser game's order:
//   1. systems' earlyInput   2. player control   3. getting in and out   4. drive-by aiming
//   5. systems' preUpdate    6. vehicles         7. the player           8. systems' update
//   9. environment, camera (real time)
// Sim time is the frame time scaled by game.timeScale and the slow motion in game.slowmo (the lowest wins).
#pragma once

#include "Camera.h"
#include "Collision.h"
#include "Env.h"
#include "Events.h"
#include "Input.h"
#include "Player.h"
#include "Systems.h"
#include "Vehicles.h"
#include "WorldMeshes.h"

namespace atg {

class PedManager;
class Traffic;
class RailSystem;
class Gameplay;
class HudModel;

struct Cheats {
	bool god = false, vehGod = false, neverWanted = false, superJump = false, superRun = false, infSprint = false, lowGravity = false;
	bool explosive = false, oneHit = false, freezeTime = false, slowmo = false, riot = false;
};

struct Settings {
	std::string quality = "high";
	double volume = 0.8, music = 0.6, sensitivity = 1;
	bool invertY = false;
	std::string npcCrime = "normal";
};

// a map blip (game.blips): the radar and the pause map draw these
struct Blip {
	double x = 0, z = 0;
	std::string icon, letter;
	uint32_t color = 0xffffff;
	bool small = false, noEdge = false, square = false;
	std::string label;   // (the map's legend)
	bool marker = false; // (a mission or service marker's blip)
};

struct Quality { int peds = 38, traffic = 28; double drawDist = 3000; };
const Quality& QualityPreset(const std::string& name);

// everything the generator makes that the game needs (the world, its collision primitives, the props)
struct WorldData {
	const CityMap* map = nullptr;
	std::vector<ColPrim> roadPrims, roadDecks;
	std::vector<PropInstance> props;
	std::map<std::string, PropTemplate>* propDefs = nullptr;
};

class Game {
public:
	explicit Game(const WorldData& world, const Settings& settings = Settings());
	~Game();

	const CityMap& map;
	std::unique_ptr<CollisionWorld> collision;
	Events events;
	Input input;
	Settings settings;
	Quality quality;
	double time = 0;
	bool paused = false;
	double timeScale = 1;
	std::map<std::string, double> slowmo;
	double fxScale() const { double k = 1; for (const auto& s : slowmo) k = Min(k, s.second); return k; }
	double gravity = 1;
	double specialGrip() const;     // (the special ability grips harder)
	bool cutscene = false;
	bool menuOpen = false; // (a full-screen menu: no weapon wheel)
	Cheats cheats, cheatsOn;
	bool freeRoam = false, missionActive = false;
	bool freeroamActive() const { return freeRoam && !missionActive; }
	bool phoneOpen = false, photoMode = false; // (the phone is out; Snapmatic: the HUD hides)
	class Phone* phone = nullptr;
	System* taxiSystem = nullptr; // (taxi.js: with the taxis)
	bool weaponWheelOpen() const; // (the weapon wheel is up: no aiming or firing)
	// the post-process uniforms the gameplay rules drive (WASTED / BUSTED): desaturation, the death tint, its
	// night boost and the white flash
	struct PostFx { double desat = 0, death = 0, deathBoost = 0, flash = 0; } post;
	struct Stats { double kills = 0, copKills = 0, headshots = 0, carsStolen = 0, carsDestroyed = 0, runOver = 0, wasted = 0, busted = 0, maxWanted = 0, bestDrift = 0, driven = 0, walked = 0, playTime = 0, missions = 0, sprays = 0, tricks = 0; } stats;

	Environment env;
	CameraRig rig;
	VehicleManager vehicles;
	std::shared_ptr<Player> player;

	// systems in registration order, and the ones other code calls into
	std::vector<std::pair<std::string, System*>> systems;
	std::vector<std::unique_ptr<System>> ownedSystems;
	template <typename T> T* addSystem(const std::string& name, std::unique_ptr<T> s) { T* p = s.get(); systems.push_back({ name, p }); ownedSystems.push_back(std::move(s)); return p; }
	System* system(const std::string& name) const { for (const auto& s : systems) if (s.first == name) return s.second; return nullptr; }
	IAudio* audio = nullptr;
	IEffects* effects = nullptr;
	IHud* hud = nullptr;
	ICombat* combat = nullptr;
	IPickups* pickups = nullptr;
	IPolice* police = nullptr;
	INpcCrime* npcCrime = nullptr;
	IWildlife* wildlife = nullptr;
	IShopSystem* shops = nullptr;
	PedManager* peds = nullptr;
	Traffic* traffic = nullptr;
	RailSystem* rail = nullptr;
	Gameplay* gameplay = nullptr;
	// the HUD's state (hud.js); hud points at it. Updated after each frame with the real frame time.
	std::unique_ptr<IHud> ownedHud;
	HudModel* hudModel = nullptr;
	std::unordered_map<int, Ref<Vehicle>> nodeBusy; // road node -> the car holding that junction (traffic.js n.busy)
	std::map<std::string, double> gangDensity; // (missions thin gangs out)
	bool disableAmbient = false;               // (no ambient traffic or pedestrians: some missions)
	// what the missions and other systems tell the police (missions.js maxWanted / noBust, vigilante.active)
	double missionMaxWanted = NaN(); bool missionNoBust = false, missionNoSpray = false, missionNoRoadblocks = false, missionNoArmy = false, vigilanteActive = false;
	// (systems the police reset after WASTED / BUSTED)
	System* army = nullptr; System* roadblocks = nullptr;
	class WeaponWheel* wheel = nullptr;
	class Special* special = nullptr;
	class Pickups* pickupsSys = nullptr; // (the pickups and markers, for the systems that place them)
	class Police* policeSys = nullptr; // (the police itself, for the systems that spawn units through it)
	class Military* military = nullptr; // (Fort Carver: whether the base is on alert)
	class ShopSystem* shopsSys = nullptr; // (the shop system)
	double viewAspect = 16.0 / 9.0;            // (the screen's, for what the camera can see)

	// characters: a registry of everyone alive (the renderer draws these), and who counts for collisions
	template <typename T, typename... A> std::shared_ptr<T> makeCharacter(A&&... args) {
		auto c = std::make_shared<T>(*this, std::forward<A>(args)...);
		characterRegistry.push_back(c);
		return c;
	}
	std::vector<std::weak_ptr<Character>> characterRegistry;
	std::vector<std::function<void(std::vector<Character*>&)>> characterSources; // (pedestrians, other players)
	std::vector<Character*> allCharacters();
	std::function<void(Character*)> characterRemover; // (the pedestrian system removes its own)
	void removeCharacter(Character* c);
	std::vector<Character*> liveCharacters();

	// things removed this frame stay alive until the frame ends
	void graveyard(std::shared_ptr<Vehicle> v) { if (v) graveV.push_back(std::move(v)); }
	void graveyard(std::shared_ptr<Character> c) { if (c) graveC.push_back(std::move(c)); }

	// setTimeout (real seconds)
	void setTimeout(double seconds, std::function<void()> fn) { timers.push_back({ seconds, std::move(fn) }); }

	// map blips
	std::vector<std::shared_ptr<Blip>> blips;
	// moving blips the systems add to the radar each frame (the roadblocks, the army, NPC suspects), in order
	std::vector<std::function<void(std::vector<Blip>&)>> radarSources;
	std::shared_ptr<Blip> addBlip(const Blip& b) { auto p = std::make_shared<Blip>(b); blips.push_back(p); return p; }
	void removeBlip(const std::shared_ptr<Blip>& b) { for (size_t i = 0; i < blips.size(); i++) if (blips[i] == b) { blips.erase(blips.begin() + i); return; } }

	// sounds (null-safe)
	void sound(const std::string& name, double vol = 1) { if (audio) audio->play(name, vol); }
	void soundAt(const std::string& name, const V3& p, double vol = 1) { if (audio) audio->playAt(name, p, vol); }

	// props: smashing street furniture (the renderer hides broken ones)
	std::vector<PropInstance> props;
	std::vector<CollObj*> propColliders;
	std::set<int> brokenProps;
	int propVersion = 0;
	void breakProp(CollObj* o);
	void restoreProp(int index);
	std::string propType(int i) const { return i >= 0 && i < (int)props.size() ? props[i].type : std::string(); }
	double propY(int i) const { return i >= 0 && i < (int)props.size() ? props[i].y : 0; }

	// the player's headlight (one spot light, reused)
	struct Headlight { V3 pos, target; double intensity = 0; } headlight;

	// one frame: input already fed; dt is the real frame time (capped at 1/20 s)
	void frame(double dt);
	void update(double dt, double realDt);
	void tryEnterExit();
	// hooks for the missions (no stealing the mission car), taxis (riding in the back) and multiplayer
	std::function<bool(Vehicle*)> canEnterVehicle;
	std::function<int(Vehicle*, Character*)> cabSeatFor;
	std::function<bool(Character*)> taxiHandleExit;
	std::function<void(Vehicle*)> takeRemoteCar;
	void respawnPlayer(double x, double z, double yaw);

private:
	struct Timer { double t; std::function<void()> fn; };
	std::vector<Timer> timers;
	std::vector<std::shared_ptr<Vehicle>> graveV;
	std::vector<std::shared_ptr<Character>> graveC;
};

// the collision world's extra primitives in the browser game's order: the props' circles, the road
// network's walls and barriers, its decks, then the trees and rocks
std::vector<ColPrim> PropCircles(const std::vector<PropInstance>& props, const std::map<std::string, PropTemplate>& defs);
std::vector<ColPrim> VegetationCircles(const CityMap& map);

} // namespace atg
