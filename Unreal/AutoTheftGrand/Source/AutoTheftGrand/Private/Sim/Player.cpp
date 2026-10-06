#include "Player.h"
#include "Camera.h"
#include "Game.h"
#include "Input.h"
#include "Vehicle.h"

namespace atg {

Appearance PlayerLook() {
	Appearance a = RandomAppearance(0);
	a.female = false; a.skin = 0x8a5536; a.hair = 0x111111; a.hairStyle = "buzz"; a.shirt = 0xf2f2f2; a.shirtType = "tank"; a.pants = 0x2b3a55; a.shorts = false;
	a.shoes = 0xeeeeee; a.hat = -1; a.build = 1.08; a.height = 1.0; a.glasses = false; a.beard = false; a.jacketColor = 0x1b5e20; a.bandana = -1; a.hasUniform = false;
	return a;
}

Player::Player(Game& g) : Character(g, PlayerLook(), 100, 0, "player") {
	isPlayer = true;
}

void Player::onHardLanding(double v) {
	if (!game.cheatsOn.superJump) { DamageInfo di; di.type = "fall"; takeDamage((v - 13) * 6, di); }
}

void Player::control(double dt, Input& input, CameraRig& rig) {
	if (dead || ragdolling) { moveTargetX = moveTargetZ = 0; aiming = false; return; }
	if (vehicle) return;
	const double mx = input.moveX(), my = input.moveY();
	const double camYaw = rig.forwardYaw();
	double dx = std::sin(camYaw) * my - std::cos(camYaw) * mx;
	double dz = std::cos(camYaw) * my + std::sin(camYaw) * mx;
	const double len = Hypot(dx, dz);
	if (len > 1) { dx /= len; dz /= len; }
	const WeaponDef& def = weaponDef();
	const bool wantAim = input.aimDown() && def.type != "melee" && def.type != "thrown";
	aimHold = Max(0, aimHold - dt);
	aiming = (wantAim || aimHold > 0) && !swimming && !game.weaponWheelOpen();
	crouching = input.down("crouch") && !swimming;
	const bool wantSprint = input.down("sprint") && len > 0.1 && !aiming && !crouching;
	if (wantSprint && stamina > 0.05) { sprinting = true; stamina = Max(0, stamina - dt * 0.08); }
	else { sprinting = false; stamina = Min(1, stamina + dt * 0.15); }
	double speed = sprinting ? 7.2 : 4.3;
	if (chute) speed = 9;
	if (aiming) speed = 2.6;
	if (def.heavy) speed = Min(speed, aiming ? 1.9 : 3.2);
	if (crouching) speed = 1.8;
	if (swimming) speed = sprinting ? 3.8 : 2.4;
	if (game.cheatsOn.superRun && !aiming && !swimming) speed *= 2;
	if (anim->busy() && anim->action) {
		const std::string& n = anim->action->name;
		if (n == "jab" || n == "cross" || n == "kick" || n == "stab" || n == "swing" || n == "getup") speed *= 0.25;
	}
	moveTargetX = dx * speed; moveTargetZ = dz * speed;
	if (chute && len < 0.1) { moveTargetX = std::sin(yaw) * 6; moveTargetZ = std::cos(yaw) * 6; }
	if (skydive && !chute && input.hit("jump")) openChute();

	if (aiming || (anim->busy() && def.type == "melee")) {
		yaw = DampAngle(yaw, camYaw, aiming ? 25 : 12, dt);
		const V3 dir = rig.lookDir();
		aimPitch = std::asin(Clamp(dir.y, -1, 1));
		hasAimDir = true; aimDir = dir;
	} else if (len > 0.1) {
		yaw = DampAngle(yaw, std::atan2(dx, dz), sprinting ? 8 : 11, dt);
	}
	if (input.hit("jump") && !aiming) { if (game.cheatsOn.superJump) jump(17); else jump(); }
	if (game.weaponWheelOpen()) return;
	if (aiming && def.scope && input.mouse.wheel != 0) rig.scopeFov = Clamp(rig.scopeFov * (input.mouse.wheel > 0 ? 0.8 : 1.25), 5, 32);
	else {
		if (input.hit("nextWeapon") || input.mouse.wheel > 0) cycleWeapon(1);
		if (input.hit("prevWeapon") || input.mouse.wheel < 0) cycleWeapon(-1);
	}
	for (int k = 0; k <= 8; k++) if (input.keyHit("Digit" + std::to_string(k + 1))) { const std::string& id = WeaponOrder()[k]; if (weapons.count(id)) switchTo(id); }
	fireCooldown -= dt;
	comboTimer -= dt;
	if (reloading > 0) { reloading -= dt; if (reloading <= 0) finishReload(); }
	if (input.hit("reload")) startReload();
	if (def.type == "melee") {
		if (input.attackPressed() && !swimming) melee();
	} else if (def.type == "thrown") {
		if (input.attackPressed() && fireCooldown <= 0) throwGrenade(rig);
	} else if (!swimming) {
		const bool wantFire = def.automatic ? input.fireDown() : input.firePressed();
		if (def.spinUp > 0) {
			const bool spinning = input.fireDown() || wantAim;
			spin = Clamp(spin + (spinning ? dt : -dt * 0.6) / def.spinUp, 0, 1);
			if (spinning) { spinSnd -= dt; if (spinSnd <= 0) { spinSnd = 0.12; game.soundAt("clink", pos, 0.08 + spin * 0.1); } }
		} else spin = 0;
		if (wantFire) {
			aimHold = 0.6;
			if (!aiming) yaw = camYaw;
			aiming = true;
			if (fireCooldown <= 0 && reloading <= 0 && (def.spinUp <= 0 || spin >= 1)) fire(rig);
		}
	}
}

void Player::bailOut(double alt) {
	skydive = true; skydiveT = 0; skydiveAuto = alt > 28;
	if (alt > 28 && game.hud) game.hud->help("Press <b>Space</b> to open your parachute", 3);
}
void Player::openChute() {
	if (chute) return;
	chute = true;
	skydive = false;
	airAccel = 5;
	game.sound("swoosh", 1);
}
void Player::closeChute() {
	if (!chute) return;
	chute = false;
	airAccel = 0;
}

void Player::update(double dt) {
	if (skydive) {
		skydiveT += dt;
		if (grounded || dead || swimming || vehicle || ragdolling) skydive = false;
		else {
			vel.x *= std::exp(-dt * 0.9); vel.z *= std::exp(-dt * 0.9);
			vel.y = Max(vel.y, -55);
			if (skydiveAuto && skydiveT > 1.6) openChute();
		}
	}
	if (chute) {
		if (grounded || swimming || dead || vehicle || ragdolling) closeChute();
		else if (vel.y < -4.5) vel.y = Damp(vel.y, -4.5, 5, dt);
	}
	Character::update(dt);
}

bool Player::carWeaponOk(const std::string& id, int s) const {
	if (s == -2) s = seat;
	const WeaponDef* d = FindWeapon(id);
	if (!d || d->type != "gun" || !weapons.count(id)) return false;
	return s == 0 ? d->hold == "pistol" || d->hold == "smg" : true;
}
std::string Player::bestCarWeapon(int s) const {
	if (s == -2) s = seat;
	const std::vector<std::string> pref = s == 0 ? std::vector<std::string>{ "smg", "pistol" } : std::vector<std::string>{ "rifle", "smg", "shotgun", "pistol" };
	for (const std::string& id : pref) if (carWeaponOk(id, s)) { const WeaponSlot& w = weapons.at(id); if (w.clip + w.ammo > 0) return id; }
	return "";
}
void Player::cycleCarWeapon(int dir) {
	std::vector<std::string> owned;
	for (const std::string& id : WeaponOrder()) if (carWeaponOk(id)) { const WeaponSlot& w = weapons.at(id); if (w.clip + w.ammo > 0) owned.push_back(id); }
	if (owned.empty()) return;
	int i = -1;
	for (int k = 0; k < (int)owned.size(); k++) if (owned[k] == weapon) i = k;
	i = ((i + dir) % (int)owned.size() + (int)owned.size()) % (int)owned.size();
	switchTo(owned[i]);
}
void Player::cycleWeapon(int dir) {
	std::vector<std::string> owned;
	for (const std::string& id : WeaponOrder()) {
		auto it = weapons.find(id);
		if (it == weapons.end()) continue;
		if (FindWeapon(id)->type == "melee" || it->second.clip + it->second.ammo > 0) owned.push_back(id);
	}
	if (owned.empty()) return;
	int i = -1;
	for (int k = 0; k < (int)owned.size(); k++) if (owned[k] == weapon) i = k;
	i = ((i + dir) % (int)owned.size() + (int)owned.size()) % (int)owned.size();
	switchTo(owned[i]);
}
void Player::switchTo(const std::string& id) {
	if (id == weapon) return;
	reloading = 0;
	equip(id);
	game.sound("switch");
}

void Player::startReload() {
	const WeaponDef& def = weaponDef();
	WeaponSlot& w = weapons[weapon];
	if (!def.clip || def.type == "melee" || reloading > 0) return;
	if (w.clip >= def.clip || w.ammo <= 0) return;
	reloading = def.id == "shotgun" ? 1.4 : def.id == "rpg" ? 1.6 : 1.1;
	anim->play("reload", 1.1 / reloading);
	game.sound("reload");
}
void Player::finishReload() {
	const WeaponDef& def = weaponDef();
	WeaponSlot& w = weapons[weapon];
	const double need = def.clip - w.clip;
	const double take = Min(need, w.ammo);
	w.clip += take; w.ammo -= take;
}

void Player::melee() {
	if (anim->busy()) { if (anim->action->t / anim->action->dur < 0.6) return; }
	std::string act;
	if (weapon == "knife") act = "stab";
	else if (weapon == "bat") act = "swing";
	else {
		if (comboTimer <= 0) comboIndex = 0;
		static const char* combo[3] = { "jab", "cross", "kick" };
		act = combo[comboIndex % 3];
		comboIndex++;
		comboTimer = 0.9;
	}
	auto a = anim->play(act);
	Game* g = &game;
	Ref<Character> self(this);
	if (a) a->onHit = [g, self, act]() { if (Character* c = self.get()) if (g->combat) g->combat->meleeHit(c, act); };
	game.sound("swoosh");
}

void Player::fire(CameraRig& rig) {
	const WeaponDef& def = weaponDef();
	WeaponSlot& w = weapons[weapon];
	if (w.clip <= 0) {
		if (w.ammo > 0) startReload();
		else { game.sound("dryfire"); fireCooldown = 0.3; }
		return;
	}
	if (!game.freeroamActive()) w.clip--;
	fireCooldown = def.rate;
	anim->recoil = def.recoil;
	rig.addShake(def.shake ? def.shake : 0.2);
	const V3 origin = rig.camPos;
	const V3 dir = rig.lookDir();
	if (game.combat) game.combat->fireWeapon(this, def, origin, dir);
	if (w.clip <= 0 && w.ammo > 0) { Ref<Character> self(this); game.setTimeout(0.25, [self]() { if (auto* p = static_cast<Player*>(self.get())) p->startReload(); }); }
}

void Player::throwGrenade(CameraRig& rig) {
	const std::string id = weaponDef().type == "thrown" ? weapon : "grenade";
	auto it = weapons.find(id);
	if (it == weapons.end() || it->second.clip + it->second.ammo <= 0) return;
	WeaponSlot& w = it->second;
	if (!game.freeroamActive()) { if (w.clip > 0) w.clip--; else w.ammo--; }
	fireCooldown = 1.0;
	yaw = rig.forwardYaw();
	auto a = anim->play("throw");
	const V3 dir = rig.lookDir();
	Game* g = &game;
	Ref<Character> self(this);
	if (a) a->onHit = [g, self, dir, id]() { if (Character* c = self.get()) if (g->combat) g->combat->throwGrenade(c, dir, id); };
	if (w.clip + w.ammo <= 0) game.setTimeout(0.8, [self, id]() { if (auto* p = static_cast<Player*>(self.get())) { p->weapons.erase(id); p->switchTo("fist"); } });
}

} // namespace atg
