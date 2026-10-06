#include "Gameplay.h"
#include "Game.h"
#include "Peds.h"

namespace atg {

const std::vector<std::pair<std::string, double>>& FreeRoamKit() {
	static const std::vector<std::pair<std::string, double>> K = { { "bat", 0 }, { "pistol", 120 }, { "smg", 200 }, { "shotgun", 40 }, { "rifle", 180 }, { "rpg", 6 }, { "grenade", 8 }, { "sniper", 30 }, { "minigun", 1200 }, { "molotov", 8 } };
	return K;
}

namespace {
std::string Fixed(double v, int digits) { char b[64]; std::snprintf(b, sizeof b, "%.*f", digits, v); return b; }
}

Gameplay::Gameplay(Game& g) : game(g) {
	Player& p = *g.player;
	p.deathHook = [this]() { onPlayerDeath(); };
	p.damagedHook = [this](Character*, double dmg) { if (game.hud) game.hud->damage(dmg); game.rig.addShake(Min(0.5, dmg / 60)); };
	auto& ev = g.events;
	ev.kill.on([this](Character* killer, Character* victim, const std::string&, const std::string& part) {
		if (killer != game.player.get()) return;
		game.stats.kills++;
		if (Ped* v = dynamic_cast<Ped*>(victim)) if (v->brain == "cop") game.stats.copKills++;
		if (part == "head") game.stats.headshots++;
	});
	ev.carjack.on([this](Character* by, Character*, Vehicle*) { if (by == game.player.get()) game.stats.carsStolen++; });
	ev.enteredVehicle.on([this](Character* c, Vehicle* v) {
		Game& g = game;
		if (c != g.player.get()) return;
		if (!v->ownedByPlayer) { v->ownedByPlayer = true; if (v->parked || v->traffic) g.stats.carsStolen++; }
		v->traffic = false; v->ai.reset(); v->parked = false;
		if (!g.hud) return;
		const std::string& kind = v->def.kind;
		if (!kind.empty() && !hinted[kind]) {
			hinted[kind] = true;
			const std::string guns = v->def.weapons ? (kind == "jet" ? " · <b>LMB</b> cannon · <b>RMB</b> homing missile" : kind == "heli" ? " · <b>LMB</b> minigun · <b>RMB</b> rockets" : "") : "";
			if (kind == "plane" || kind == "jet") g.hud->help("<b>W/S</b> throttle · <b>Mouse</b> or <b>↑↓</b> pitch (↓ pulls up) · <b>A/D</b> roll · <b>Q/E</b> rudder · <b>Space</b> brakes" + guns + " · <b>F</b> bail out. Build speed on the runway, then pull up.", 12);
			else if (kind == "heli") g.hud->help("<b>Space</b> climb · <b>Shift</b> descend · <b>W/S</b> nose down/up · <b>A/D</b> turn · <b>Q/E</b> strafe · <b>Mouse</b> camera" + guns + " · <b>F</b> bail out. Wait for the rotor to spin up.", 12);
			else if (kind == "train") g.hud->help(v->driver() == c ? "<b>W</b> throttle · <b>S</b> brake / reverse · <b>H</b> horn · <b>F</b> climb out. The line runs from Dry Wells to Union Station." : "Riding the Sol Line. Sit back, or press <b>F</b> to get off (best at a station).", 9);
			else if (kind == "tank") g.hud->help("<b>W/S</b> drive · <b>A/D</b> turn on the spot · <b>Mouse</b> aim the turret · <b>LMB</b> fire the cannon · drive straight over cars.", 10);
		} else if (!v->def.bike.empty() && !hinted[v->def.board ? "board" : "bike"]) {
			hinted[v->def.board ? "board" : "bike"] = true;
			if (v->def.board) g.hud->help("<b>W</b> push · <b>S</b> foot-brake · <b>A/D</b> carve · <b>Space</b> ollie · in the air <b>A/D</b> kickflip / heelflip, <b>S</b> shove-it · <b>F</b> step off. Land it clean for cash.", 10);
			else g.hud->help(v->def.pedal ? "<b>W</b> pedal · <b>S</b> brake · <b>A/D</b> steer · <b>F</b> get off. Hit something hard and you'll go over the bars." : "<b>W</b> throttle · <b>S</b> brake · <b>A/D</b> steer · <b>Space</b> handbrake · <b>F</b> get off. Crash hard and you're thrown off.", 8);
		} else if (kind.empty() && v->def.bike.empty() && !hinted["drive"]) { hinted["drive"] = true; g.hud->help("<b>W</b> accelerate · <b>S</b> brake/reverse · <b>Space</b> handbrake · <b>N</b> radio · <b>V</b> camera · <b>F</b> exit", 7); }
	});
	ev.vehicleExploded.on([this](Vehicle* v) { if (v->lastDamager.get() == game.player.get() || game.player->vehicle == v) game.stats.carsDestroyed++; });
	ev.pedRunOver.on([this](Character*, Vehicle* v) { if (v->driver() == game.player.get()) game.stats.runOver++; });
	ev.wantedUp.on([this](int l) { game.stats.maxWanted = Max(game.stats.maxWanted, l); game.sound("wanted"); });
	ev.busted.on([this]() { onBusted(); });
}

// ------------------------------------------------------------------ death / arrest
void Gameplay::onPlayerDeath() {
	if (state == "dead") return;
	state = "dead";
	game.stats.wasted++;
	beginDeathScreen("wasted", 0.28);
	game.events.playerDied.emit();
}

void Gameplay::onBusted() {
	if (state != "playing") return;
	state = "busted";
	game.stats.busted++;
	Player& p = *game.player;
	if (Vehicle* v = p.vehicle) { v->input.throttle = 0; v->input.brake = 1; game.vehicles.exit(&p); }
	p.animState.handsUp = true;
	beginDeathScreen("busted", 0.5);
}

// GTA V style sequence: slow motion and a white flash, black and white, a slow camera drift away from the
// body, the stinger plays and the "wasted" shard lands on its big hit (~2.45 s), then fade and respawn.
void Gameplay::beginDeathScreen(const std::string& kind, double slowmo) {
	deathTimer = 0;
	deathKind = kind;
	shardShown = false;
	game.timeScale = slowmo;
	if (game.hud) game.hud->deathMode(true);
	IAudio* au = game.audio;
	if (au) au->muffle(true);
	const bool sampled = au && au->playSample("wasted", 1);
	if (!sampled) game.sound("wasted");
	shardAt = sampled ? 2.42 : 1.1;
	audioStart = sampled ? au->clock() : -1;
	hasDeathCam = false;
}

void Gameplay::deathCamera(double) {
	Player& p = *game.player;
	const V3 body = p.ragdolling && p.ragdoll ? p.ragdoll->center() : p.vehicle ? p.vehicle->pos : p.pos;
	if (!hasDeathCam) {
		const V3 cp = game.rig.camPos;
		double dx = cp.x - body.x, dz = cp.z - body.z;
		double d = Hypot(dx, dz); if (d == 0) d = 1;
		dx /= d; dz /= d;
		deathCam = { dx, dz, Clamp(d, 2.5, p.vehicle ? 9 : 5), Clamp(cp.y - body.y, 2.2, 4.5), Rand() < 0.5 ? -1.0 : 1.0 };
		hasDeathCam = true;
	}
	const DeathCam& D = deathCam;
	const double k = Clamp(deathTimer / 6.5, 0, 1);
	const double e = 1 - std::pow(1 - k, 2);
	// swing slowly around the body while rising into a high-angle shot
	const double a = D.side * e * 0.55;
	const double ca = std::cos(a), sa = std::sin(a);
	const double rx = D.dx * ca - D.dz * sa, rz = D.dx * sa + D.dz * ca;
	const bool busted = deathKind == "busted";
	const double dist = D.d0 + e * (busted ? 1.5 : 2.6);
	const V3 pos(body.x + rx * dist, body.y + D.h0 + e * (busted ? 1.5 : 4.2), body.z + rz * dist);
	const V3 look(body.x, body.y + (p.ragdolling ? 0.2 : 0.9), body.z);
	game.rig.setCinematic(pos, look, 58 - e * 8);
}

void Gameplay::respawn(const std::string& kind) {
	Game& g = game;
	const Landmark& L = g.map.landmarks.at(kind == "busted" ? "police" : "hospital");
	const P3 spotP = L.pts.at("respawn");
	const double spotRot = L.vals.at("respawnRot");
	if (g.audio) g.audio->stopSample("wasted", 0.9);
	auto after = [this, kind, spotP, spotRot]() {
		Game& g = game;
		Player& p = *g.player;
		g.timeScale = 1;
		g.post = Game::PostFx();
		if (g.hud) g.hud->deathMode(false);
		if (g.audio) g.audio->muffle(false);
		g.rig.clearCinematic();
		if (Vehicle* v = p.vehicle) v->takeOut(&p);
		p.dead = false; p.ragdolling = false;
		p.health = p.maxHealth; p.armor = 0;
		const bool free = g.freeroamActive();
		const double lost = free ? 0 : kind == "busted" ? 100 * Max(1, g.police ? g.police->wantedLevel() : 0) : 100;
		p.money = Max(0, p.money - lost);
		if (free) {
			// free roam: keep everything and top the ammo back up
			const std::string keep = p.weapon;
			for (const auto& wa : FreeRoamKit()) {
				auto it = p.weapons.find(wa.first);
				if (it == p.weapons.end()) p.giveWeapon(wa.first, wa.second);
				else if (it->second.ammo + it->second.clip < wa.second) p.giveWeapon(wa.first, wa.second);
			}
			p.equip(p.weapons.count(keep) ? keep : "pistol");
		} else {
			p.weapons.clear();
			p.weapons["fist"] = { kInf, kInf };
			p.equip("fist");
		}
		p.anim->action.reset();
		p.anim->beginBlend(0.01);
		p.animState.handsUp = false;
		// a clean slate: no leftover fall speed, parachute or swim, and a few seconds' grace
		p.vel.set(0, 0, 0); p.skydive = false; p.closeChute(); p.swimming = false; p.crouching = false; p.aiming = false; p.downTime = 0;
		p.protectUntil = g.time + 4;
		// nobody is still gunning for you: attackers lose interest and angered gangs cool down
		if (g.peds) {
			for (auto& kv : g.peds->gangAggro) kv.second = false;
			g.peds->gangAggro["vipers"] = true;
			for (auto& q : g.peds->list) {
				if (q->dead || q->removed || q->brain == "script" || q->brain == "cop") continue;
				if (q->threat.get() == &p || q->state == "attack") { q->threat = nullptr; q->setState(!q->gang.empty() ? "guard" : "wander"); }
			}
		}
		// (shops.calmDown comes with the shops)
		p.setPosition(spotP.x, spotP.z);
		p.yaw = spotRot;
		p.visible = true;
		if (g.police) g.police->clearWanted();
		g.env.setTime(g.env.hours + 3);
		g.rig.yaw = spotRot + kPi;
		state = "playing";
		if (g.hud) {
			if (free) g.hud->help(kind == "busted" ? "Back on the street. Free roam: you keep your weapons and cash." : "Patched up. Free roam: you keep your weapons and cash.", 5);
			else g.hud->help(kind == "busted" ? "The cops took your weapons and $" + Fixed(lost, 0) + "." : "The hospital bill came to $" + Fixed(lost, 0) + ". Your weapons were... misplaced.", 5);
		}
		// (missions.refreshContacts comes with the missions)
		if (afterRespawn) { auto f = afterRespawn; afterRespawn = nullptr; g.setTimeout(0.7, f); }
	};
	if (g.hud) g.hud->fade(0.8, after); else after();
}

// ------------------------------------------------------------------ per frame
void Gameplay::update(double dt) {
	Game& g = game;
	Player& p = *g.player;
	if (state == "menu") { flyover(dt); return; }
	g.stats.playTime += dt;
	if (state == "dead" || state == "busted") {
		const double real = dt / Max(0.05, g.timeScale);
		deathTimer += real;
		const double T = deathTimer;
		Game::PostFx& U = g.post;
		U.desat = Clamp(T / 0.6, 0, 1);
		U.death = Clamp(T / 1.1, 0, 1);
		U.deathBoost = 0.2 + 1.7 * Clamp(g.env.night, 0, 1);
		U.flash = T < 0.08 ? 0.55 : Max(0, 0.55 * (1 - (T - 0.08) / 0.45));
		// a second, softer pulse when the shard lands
		const double sinceAudio = audioStart >= 0 && g.audio ? g.audio->clock() - audioStart : T;
		if (!shardShown && sinceAudio >= shardAt) { shardShown = true; shardTime = T; if (g.hud) g.hud->showWasted(deathKind); }
		if (shardShown) U.flash = Max(U.flash, 0.22 * Max(0, 1 - (T - shardTime) / 0.35));
		// ease the slow motion back a little after the hit
		g.timeScale = deathKind == "busted" ? 0.5 : T < 2.4 ? 0.28 : 0.4;
		if (state == "busted") { p.moveTargetX = 0; p.moveTargetZ = 0; p.animState.handsUp = true; }
		deathCamera(real);
		if (T > 6.3) { const std::string k = state; state = "respawning"; respawn(k == "busted" ? "busted" : "wasted"); }
		return;
	}
	// distance stats
	const V3 pos = p.vehicle ? p.vehicle->pos : p.pos;
	const double d = Hypot(pos.x - lastPos.x, pos.z - lastPos.z);
	if (d < 50) { if (p.vehicle) g.stats.driven += d; else g.stats.walked += d; }
	lastPos = pos;
	// drift scoring & stunts
	Vehicle* v = p.vehicle;
	if (v && p.seat == 0 && !v->isWrecked() && v->def.kind.empty()) {
		const double spd = v->speedAbs();
		const double velYaw = std::atan2(v->vel.x, v->vel.z);
		const double slip = std::fabs(WrapAngle(velYaw - v->yaw));
		const bool drifting = !v->airborne && spd > 9 && slip > 0.3 && slip < 2.2;
		if (drifting) { drift += spd * dt; driftTime = 0; }
		else if (drift > 0) {
			driftTime += dt;
			if (driftTime > 0.6) {
				if (drift > 25) {
					const double cash = std::floor(drift);
					p.money += cash;
					g.stats.bestDrift = Max(g.stats.bestDrift, drift);
					if (g.hud) g.hud->bigMessage("DRIFT " + Fixed(std::floor(drift), 0) + " M", "hint", 1.6, "+$" + Fixed(cash, 0));
				}
				drift = 0;
			}
		}
		if (g.hud) {
			static const std::string DRIFT = "DRIFT";
			if (drift > 15) g.hud->setBar(&DRIFT, Clamp(drift / 200, 0, 1), "#ffd23f");
			else if (!g.missionActive) g.hud->setBar(nullptr);
		}
		if (v->airborne) { airTime += dt; if (!hasAirStart) { airStart = v->pos; hasAirStart = true; } }
		else if (airTime > 0) {
			if (airTime > 1.2 && hasAirStart) {
				const double dist = Hypot(v->pos.x - airStart.x, v->pos.z - airStart.z);
				const double cash = std::floor(dist * 5 + airTime * 40);
				p.money += cash;
				if (g.hud) g.hud->bigMessage("INSANE STUNT BONUS", "passed", 3, "Distance " + Fixed(dist, 0) + "m · Air " + Fixed(airTime, 1) + "s · $" + Fixed(cash, 0));
				if (g.audio) g.audio->play("passed", 0.5);
			}
			airTime = 0; hasAirStart = false;
		}
	}
	IEffects* fx = g.effects;
	for (const auto& vp : g.vehicles.list) {
		Vehicle* veh = vp.get();
		if (veh->isWrecked() && !veh->exploded) continue;
		const double hp = veh->health / (veh->maxHealth ? veh->maxHealth : 1000);
		if (hp < 0.4 && !veh->exploded && !veh->def.pedal && !veh->def.board && Rand() < dt * (hp < 0.15 ? 20 : 8)) {
			const V3 f = veh->fwd();
			V3 hpos = veh->pos + f * (veh->def.L * 0.35);
			hpos.y = veh->pos.y + veh->def.H * 0.7;
			if (fx) fx->engineSmoke(hpos, hp < 0.15 ? 1 : 0);
			if (veh->onFire && Rand() < 0.8 && fx) fx->fire(hpos, 0.9);
		}
		if (veh->exploded && veh->wreckTime < 20 && Rand() < dt * 12 && fx) fx->fire(V3(veh->pos.x, veh->pos.y + 0.8, veh->pos.z), 1.3);
		// tyre smoke & skid marks
		if (veh->skid && Dist2(veh->pos.x, veh->pos.z, g.rig.camPos.x, g.rig.camPos.z) < 150 * 150 && veh->model) {
			const double s = std::sin(veh->yaw), c = std::cos(veh->yaw);
			for (const auto& w : veh->model->wheels) {
				if (w.front && !(veh->input.brake > 0.5 && veh->speed() > 12)) continue;
				const double wx = veh->pos.x + w.x * c + w.z * s, wz = veh->pos.z - w.x * s + w.z * c;
				const double wy = veh->pos.y;
				char key[96]; std::snprintf(key, sizeof key, "%d:%g:%g", veh->vid, w.x, w.z);
				if (fx) fx->skidAdd(key, wx, wy, wz, 0.24, Clamp(veh->slipRear / 6 + 0.4, 0, 1));
				if (Rand() < 0.5 && fx) fx->tireSmoke(V3(wx, wy, wz), Clamp(veh->slipRear / 10 + veh->wheelspin, 0.4, 1.2));
			}
		} else if (veh->skidding && veh->model) {
			for (const auto& w : veh->model->wheels) { char key[96]; std::snprintf(key, sizeof key, "%d:%g:%g", veh->vid, w.x, w.z); if (fx) fx->skidBreak(key); }
		}
		veh->skidding = veh->skid != 0;
		// dust off-road
		if (!veh->skid && !veh->def.board && veh->speedAbs() > 8 && veh->surface < 1 && Rand() < dt * 10 && fx) fx->dust(veh->pos - veh->fwd() * (veh->def.L / 2), 1);
	}
	// player wet / drown
	if (p.swimming && !p.vehicle) p.swimTime += dt; else p.swimTime = 0;
	if (p.vehicle && p.vehicle->sunk && !g.vehicles.isBusy(&p)) { Vehicle* v2 = p.vehicle; const V3 at = v2->pos; v2->takeOut(&p, &at); p.pos.y = v2->pos.y + 1; }
	if (p.health <= 0 && !p.dead) p.die(DamageInfo());
}

void Gameplay::flyover(double dt) {
	Game& g = game;
	flyT += dt;
	const double t = flyT * 0.03;
	const double cx = 120, cz = -120, r = 520;
	const V3 pos(cx + std::cos(t) * r, 150 + std::sin(t * 0.7) * 40, cz + std::sin(t) * r);
	const V3 look(cx + std::cos(t + 1.3) * 120, 30, cz + std::sin(t + 1.3) * 120);
	g.rig.setCinematic(pos, look, 55);
	g.player->pos.set(pos.x, g.map.GroundHeight(pos.x, pos.z), pos.z);
	g.player->visible = false;
}

} // namespace atg
