// The player character, Andre "Dre" Castillo (port of src/game/player.js): the input-driven controller on
// foot (camera-relative movement, sprint and stamina, crouch, jump, aiming, weapon switching, reloads,
// melee combos, firing and throwing) plus drive-by weapons, bailing out of aircraft and the parachute.
#pragma once

#include "Character.h"

namespace atg {

class CameraRig;
class Input;

Appearance PlayerLook();

class Player : public Character {
public:
	explicit Player(Game& game);
	double money = 250;
	bool sprinting = false;
	double drunk = 0;   // (drinks at the bar: the camera sways, shops.js)
	double stamina = 1, aimHold = 0, fireCooldown = 0, comboTimer = 0, reloading = 0, maxArmor = 100;
	int comboIndex = 0;
	bool enterRequest = false;
	double spin = 0, spinSnd = 0;
	// free fall and the parachute
	bool skydive = false; double skydiveT = 0; bool skydiveAuto = false;
	bool chute = false;
	double swimTime = 0;
	// set by the gameplay rules (gameplay.js: p.onDeath, p.onDamaged)
	std::function<void()> deathHook;
	std::function<void(Character*, double)> damagedHook;
	void onDeath(const DamageInfo&) override { if (deathHook) deathHook(); }
	void onDamaged(Character* src, double dmg, const DamageInfo&) override { if (damagedHook) damagedHook(src, dmg); }

	void control(double dt, Input& input, CameraRig& rig);
	void update(double dt) override;
	void onHardLanding(double v) override;

	void bailOut(double alt);
	void openChute();
	void closeChute();
	bool carWeaponOk(const std::string& id, int seat = -2) const;
	std::string bestCarWeapon(int seat = -2) const;
	void cycleCarWeapon(int dir);
	void cycleWeapon(int dir);
	void switchTo(const std::string& id);
	void startReload();
	void finishReload();
	void melee();
	void fire(CameraRig& rig);
	void throwGrenade(CameraRig& rig);
};

} // namespace atg
