// The game's systems (src/main.js registers them with game.addSystem). A system may take input before the
// player (earlyInput), run after the player's controls (preUpdate) and in the main pass (update). Systems
// other parts of the game call into are reached through the interfaces below, so callers work whether or
// not the system is there (the browser game's `game.effects?.sparks?.(...)`).
#pragma once

#include "Core.h"
#include <optional>

namespace atg {

class Character;
class Game;
class Ped;
class Player;
class Vehicle;
class Animal;
struct CollObj;

class System {
public:
	virtual ~System() = default;
	virtual void earlyInput(double) {}
	virtual void preUpdate(double) {}
	virtual void update(double) {}
	virtual void reset() {}
};

// sounds (synthesised by the Unreal side, src/game/audio.js)
// how a positional sound carries (audio.js playAt opts): a gunshot (heard further off, with more reverb), an
// explosion's size (1: a car)
struct SoundOpts { bool gun = false; double size = NaN(); };

class IAudio {
public:
	virtual ~IAudio() = default;
	virtual void play(const std::string& name, double vol = 1) = 0;
	virtual void playAt(const std::string& name, const V3& pos, double vol = 1, const SoundOpts& opts = SoundOpts()) = 0;
	// recorded clips (the WASTED stinger); false when there is no such sample
	virtual bool playSample(const std::string&, double = 1) { return false; }
	virtual void stopSample(const std::string&, double = 0) {}
	virtual void muffle(bool) {}
	virtual double clock() const { return -1; } // (the audio context's time; -1 without audio)
	// a positional loop ("heli", "siren", "fire"): an id to move, fade or stop it (0: no sound)
	virtual int loop(const std::string&, const V3&) { return 0; }
	virtual void loopPos(int, const V3&) {}
	virtual void loopVol(int, double) {}
	virtual void loopStop(int) {}
	// the radio: the next station, and the current one's name (and genre on a second line)
	virtual void radioNext() {}
	virtual std::string radioLabel() const { return ""; }
};

// particles, debris, decals (src/game/effects.js)
class IEffects {
public:
	virtual ~IEffects() = default;
	virtual void sparks(const V3& pos, double k) = 0;
	virtual void dust(const V3& pos, double k) = 0;
	virtual void splash(const V3& pos, double k) = 0;
	virtual void blood(const V3& pos, const V3& dir, double n) = 0;
	virtual void glassBurst(const V3& pos, double w) = 0;
	virtual void propDebris(CollObj* prop, const V3& vel) = 0;
	virtual void hydrantSpray(double x, double y, double z) = 0;
	// a panel torn off a vehicle flies off as debris
	virtual void panelDebris(Vehicle* v, const std::string& part, const V3& vel) = 0;
	// smoke and fire on damaged and burning cars, tyre smoke and skid marks (gameplay.js)
	virtual void engineSmoke(const V3&, double) {}
	virtual void fire(const V3&, double) {}
	virtual void tireSmoke(const V3&, double) {}
	virtual void skidAdd(const std::string& /*key*/, double, double, double, double /*width*/, double /*alpha*/) {}
	virtual void skidBreak(const std::string& /*key*/) {}
	// weapons, explosions (combat.js and others)
	virtual void muzzleFlash(const V3&, const V3& /*dir*/, bool /*big*/) {}
	virtual void impact(const V3&, const V3& /*normal*/, const std::string& /*kind: concrete, metal*/) {}
	virtual void bloodPool(const V3&) {}
	virtual void tracer(const V3& /*from*/, const V3& /*to*/) {}
	// radius: visual size (6.75 = a car); foot: halfWidth, halfLength, sin(yaw), cos(yaw) of a vehicle, or null
	virtual void explosion(const V3&, double /*radius*/ = 6, const double* /*foot*/ = nullptr, bool /*secondary*/ = false) {}
	virtual void flash(const V3&, uint32_t /*color*/, double /*intensity*/, double /*life*/, double /*range*/ = 30) {}
	// boats: foam behind the stern, spray off the bow (s, c: the heading's sine and cosine)
	virtual void foam(const V3&, double, double, double, double) {}
	virtual void bowSpray(const V3&, double, double, double, double) {}
	// a boat's wake (key: the boat): a white trail on the water that spreads and fades; break ends the trail
	virtual void wakeAdd(int /*key*/, double, double, double, double /*width*/, double /*strength*/) {}
	virtual void wakeBreak(int /*key*/) {}
};

// an item in a shop menu (hud.js openStore: { name, desc, price, use(player, game) }): what buying it does, and
// a message for the menu (empty: "<name> bought.")
struct StoreItem {
	std::string name, desc;
	int price = 0;
	std::function<std::string(Player&, Game&)> use;
	bool available = true; // (the pet shop's animals wait for the pets)
};

// on-screen help and messages (src/ui/hud.js)
class IHud {
public:
	virtual ~IHud() = default;
	virtual void help(const std::string& text, double seconds = 4) = 0;
	virtual void speech(Character* who, const std::string& text) = 0;
	virtual void bigMessage(const std::string&, const std::string& = "title", double = 4, const std::string& = "") {}
	virtual void subtitle(const std::string&, const std::string& = "", double = 4) {}
	virtual void objective(const std::string&, double = 7) {}
	virtual void clearObjective() {}
	virtual void setBar(const std::string* /*label (null hides it)*/, double = 0, const std::string& = "#e63946") {}
	virtual void moneyFlash(double) {}
	virtual void promptSave() {} // (the safehouse's save point)
	// the shop menus: the Gun Barn's weapons and armour, and a store's items (both pause the game)
	virtual void openShop() {}
	virtual void openStore(const std::string& /*title*/, const std::string& /*sub*/, const std::vector<StoreItem>& /*items*/) {}
	virtual void openPause(const std::string&) {} // (the pause menu on a tab: map, teleport)
	virtual void dispatch(const std::string&, const std::string& = "") {}
	// the radio station's name (and genre) at the top of the screen
	virtual void showRadio(const std::string&, const std::string& = "") {}
	virtual void interact(const std::string&) {}
	// fade to black over dur seconds, call mid, fade back in
	virtual void fade(double = 0.5, std::function<void()> mid = nullptr) { if (mid) mid(); }
	virtual void fadeTo(double, double = 0.5) {}
	virtual void damage(double) {}
	virtual void showWasted(const std::string&) {}
	virtual void deathMode(bool) {}
	virtual void setTimer(double /*NaN hides it*/) {}
	virtual void setCounter(const std::string&, const std::string&) {}
	virtual void letterbox(bool) {}
	virtual void routeTo(std::optional<V2>) {}
	virtual void showCredits() {}
};

// weapons and damage (src/game/combat.js)
class ICombat {
public:
	virtual ~ICombat() = default;
	virtual void vehicleExplosion(Vehicle* v, const V3& pos) = 0;
	virtual void meleeHit(Character* attacker, const std::string& action) = 0;
	struct FireOpts { bool fromMuzzle = false; double spreadMul = 1; };
	virtual void fireWeapon(Character* shooter, const struct WeaponDef& def, const V3& origin, const V3& dir, const FireOpts& opts = FireOpts()) = 0;
	virtual void throwGrenade(Character* thrower, const V3& dir, const std::string& id) = 0;
};

// money and weapons dropped by the dead (src/game/pickups.js)
class IPickups {
public:
	virtual ~IPickups() = default;
	virtual void dropMoney(const V3& pos, int amount) = 0;
	virtual void dropWeapon(const V3& pos, const std::string& weapon, int ammo) = 0;
};

// the police's officers think here (src/game/police.js copThink)
class IPolice {
public:
	virtual ~IPolice() = default;
	virtual void copThink(Ped* cop, double dt) = 0;
	virtual int wantedLevel() const { return 0; }
	virtual void clearWanted() {} // (police.reset() after WASTED / BUSTED)
	// for the minimap: are they searching (lost sight of you: police.flash), where are they looking, and where
	// are the cops
	virtual bool searching() const { return false; }
	struct RadarCone { double x, z, yaw, len, wide; };
	virtual void radarCones(std::vector<RadarCone>&) const {}
	virtual void radarCops(std::vector<V3>&) const {}
	// the helicopter: shots and missiles can hit it (a 2.6 m sphere)
	virtual bool heliRay(double, double, double, double, double, double, double, double&) const { return false; }
	virtual void heliHit(double) {}
	virtual bool heliAlive(V3&) const { return false; }
};

// street crime (src/game/npccrime.js)
struct NpcTag { Character* c; int n; bool hot; }; // (a suspect with stars showing)
class INpcCrime {
public:
	virtual ~INpcCrime() = default;
	virtual bool pedThink(Ped* p, double dt) = 0;
	virtual bool jaywalk(Ped* p) = 0;
	// a cop on a call to an NPC suspect (police copThink): true when that took care of the cop this frame
	virtual bool copThink(Ped*, double) { return false; }
	// everyone with stars over their head, for the HUD and the radar
	virtual void tagged(std::vector<NpcTag>&) const {}
};

// animals (src/game/wildlife.js)
class IWildlife {
public:
	virtual ~IWildlife() = default;
	virtual void addWalkedDog(Ped* owner) = 0;
	virtual std::vector<Animal*> all() const = 0;
};

} // namespace atg
