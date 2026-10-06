// The game's systems (src/main.js registers them with game.addSystem). A system may take input before the
// player (earlyInput), run after the player's controls (preUpdate) and in the main pass (update). Systems
// other parts of the game call into are reached through the interfaces below, so callers work whether or
// not the system is there (the browser game's `game.effects?.sparks?.(...)`).
#pragma once

#include "Core.h"

namespace atg {

class Character;
class Ped;
class Vehicle;
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
class IAudio {
public:
	virtual ~IAudio() = default;
	virtual void play(const std::string& name, double vol = 1) = 0;
	virtual void playAt(const std::string& name, const V3& pos, double vol = 1) = 0;
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
};

// on-screen help and messages (src/ui/hud.js)
class IHud {
public:
	virtual ~IHud() = default;
	virtual void help(const std::string& text, double seconds = 4) = 0;
	virtual void speech(Character* who, const std::string& text) = 0;
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
};

// street crime (src/game/npccrime.js)
class INpcCrime {
public:
	virtual ~INpcCrime() = default;
	virtual bool pedThink(Ped* p, double dt) = 0;
	virtual bool jaywalk(Ped* p) = 0;
};

// animals (src/game/wildlife.js)
class IWildlife {
public:
	virtual ~IWildlife() = default;
	virtual void addWalkedDog(Ped* owner) = 0;
};

} // namespace atg
