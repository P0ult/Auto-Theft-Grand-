#include "Game.h"
#include "WeaponWheel.h"
#include "Hud.h"

namespace atg {

const Quality& QualityPreset(const std::string& name) {
	static const std::map<std::string, Quality> Q = {
		{ "low", { 18, 14, 1400 } }, { "medium", { 28, 22, 2200 } }, { "high", { 38, 28, 3000 } }, { "ultra", { 46, 34, 3200 } },
	};
	auto it = Q.find(name);
	return it == Q.end() ? Q.at("high") : it->second;
}

std::vector<ColPrim> PropCircles(const std::vector<PropInstance>& props, const std::map<std::string, PropTemplate>& defs) {
	std::vector<ColPrim> out;
	for (size_t i = 0; i < props.size(); i++) {
		const PropInstance& p = props[i];
		auto def = defs.find(p.type);
		if (def == defs.end()) continue;
		const bool multi = p.type == "boothbar" || p.type == "sandbags";
		ColPrim c; c.kind = ColPrim::Circle; c.x = p.x; c.z = p.z; c.r = multi ? 1.4 : def->second.r; c.h = p.y + def->second.h; c.y0 = p.y - 0.5;
		c.type = "prop"; c.prop = (int)i; c.breakable = def->second.breakable;
		out.push_back(c);
	}
	return out;
}

std::vector<ColPrim> VegetationCircles(const CityMap& map) {
	std::vector<ColPrim> out;
	const Vegetation& V = map.vegetation;
	struct K { const char* kind; const std::vector<float>* list; double r, h; };
	const K kinds[] = { { "pine", &V.pine, 0.45, 12 }, { "oak", &V.oak, 0.4, 6 }, { "bush", &V.bush, 0, 0 }, { "cactus", &V.cactus, 0.4, 5 },
		{ "rock", &V.rock, 1.3, 1.4 }, { "deadtree", &V.deadtree, 0.3, 4 }, { "palm", &V.palm, 0.35, 9 } };
	for (const K& k : kinds) {
		if (k.r <= 0) continue;
		const bool rock = std::string(k.kind) == "rock";
		const std::vector<float>& a = *k.list;
		for (size_t i = 0; i + 4 < a.size(); i += 5) {
			const double sc = a[i + 4];
			ColPrim c; c.kind = ColPrim::Circle; c.x = a[i]; c.z = a[i + 2]; c.r = k.r * (rock ? sc : Min(1.3, sc)); c.h = a[i + 1] + k.h * sc; c.y0 = a[i + 1] - 1;
			c.type = rock ? "rock" : "tree";
			out.push_back(c);
		}
	}
	return out;
}

Game::Game(const WorldData& w, const Settings& s)
	: map(*w.map), settings(s), quality(QualityPreset(s.quality)), rig(*this), vehicles(*this) {
	collision = std::make_unique<CollisionWorld>(map);
	// (the walk-in shops' interiors aren't built yet: their shells stand in as solid buildings)
	{
		size_t shells = 0;
		for (const InteriorShell& it : map.interiors) shells += it.colliders.size();
		const auto& all = collision->all();
		const size_t keep = all.size() - (std::min)(shells, all.size());
		for (size_t i = keep; i < all.size(); i++) collision->remove(all[i].get());
		for (const InteriorShell& it : map.interiors) {
			const Building& b = map.buildings[it.building];
			collision->addBox(b.x0, b.y0 - 0.2, b.z0, b.x1, b.y1 + (b.roof == "gable" ? 2.5 : 0), b.z1, "building");
		}
	}
	props = w.props;
	if (w.propDefs) {
		propColliders.assign(props.size(), nullptr);
		for (const ColPrim& c : PropCircles(props, *w.propDefs)) propColliders[c.prop] = collision->add(c);
	}
	for (const ColPrim& c : w.roadPrims) collision->add(c);
	for (const ColPrim& c : w.roadDecks) collision->add(c);
	for (const ColPrim& c : VegetationCircles(map)) collision->add(c);
	input.sensitivity = settings.sensitivity;
	input.invertY = settings.invertY;
	player = makeCharacter<Player>();
}

Game::~Game() = default;

std::vector<Character*> Game::allCharacters() {
	std::vector<Character*> out;
	out.push_back(player.get());
	for (auto& src : characterSources) src(out);
	return out;
}

std::vector<Character*> Game::liveCharacters() {
	std::vector<Character*> out;
	for (size_t i = 0; i < characterRegistry.size();) {
		auto c = characterRegistry[i].lock();
		if (!c || c->removed) { characterRegistry.erase(characterRegistry.begin() + i); continue; }
		out.push_back(c.get());
		i++;
	}
	return out;
}

void Game::removeCharacter(Character* c) {
	if (!c) return;
	graveyard(c->shared_from_this());
	if (characterRemover) characterRemover(c);
	else c->remove();
}

void Game::breakProp(CollObj* o) {
	if (!o || o->prop < 0 || o->broken) return;
	o->broken = true;
	brokenProps.insert(o->prop);
	propVersion++;
}
void Game::restoreProp(int i) {
	if (!brokenProps.count(i)) return;
	brokenProps.erase(i);
	if (i >= 0 && i < (int)propColliders.size() && propColliders[i]) propColliders[i]->broken = false;
	propVersion++;
}

void Game::frame(double dt) {
	input.frameDt = dt;
	input.inVehicle = player && player->vehicle;
	const double sdt = paused ? 0 : dt * timeScale * fxScale();
	if (!paused) update(sdt, dt);
	if (hudModel) hudModel->update(dt);
	input.endFrame();
	// timers run on real time
	for (size_t i = 0; i < timers.size();) {
		timers[i].t -= dt;
		if (timers[i].t <= 0) { auto fn = std::move(timers[i].fn); timers.erase(timers.begin() + i); fn(); }
		else i++;
	}
	graveV.clear();
	graveC.clear();
}

void Game::update(double dt, double realDt) {
	time += dt;
	Player& p = *player;
	const bool controlsEnabled = input.enabled && !cutscene;
	for (auto& s : systems) s.second->earlyInput(dt);
	if (controlsEnabled) {
		if (!p.vehicle) p.control(dt, input, rig);
		else if (p.seat == 0 && !vehicles.isBusy(&p)) p.vehicle->playerControl(input, dt);
		if (input.hit("enter") && !p.dead && !p.ragdolling) tryEnterExit();
		if (p.vehicle) {
			rig.lookBehind = input.down("lookBehind");
			rig.cineHeld = input.down("cinematic") && !phoneOpen;
			if (input.hit("camera")) rig.vehicleCamIndex++;
			Vehicle* pv0 = p.vehicle;
			if (pv0->armed() && p.seat == 0) p.aiming = pv0->showCrosshair(); // (mounted guns fire from playerControl)
			else {
				bool aim = input.aimDown() && (pv0->def.kind.empty() || pv0->def.kind == "boat") && !weaponWheelOpen();
				if (aim && !p.carWeaponOk(p.weapon)) { const std::string b = p.bestCarWeapon(); if (!b.empty()) p.switchTo(b); else aim = false; }
				p.aiming = aim;
				if (input.hit("nextWeapon") || input.mouse.wheel > 0) p.cycleCarWeapon(1);
				if (input.hit("prevWeapon") || input.mouse.wheel < 0) p.cycleCarWeapon(-1);
				if (p.reloading > 0) { p.reloading -= dt; if (p.reloading <= 0) p.finishReload(); }
				else if (input.key("KeyR")) p.startReload();
				p.fireCooldown -= dt;
				if (aim) {
					const V3 dir = rig.lookDir();
					p.hasAimDir = true; p.aimDir = dir;
					p.aimPitch = std::asin(Clamp(dir.y, -1, 1));
					p.hasAimYaw = true; p.aimYaw = std::atan2(dir.x, dir.z);
					const bool ready = p.seat == 0 || p.leanK > 0.7;
					if (input.driveByFire() && p.fireCooldown <= 0 && p.reloading <= 0 && ready) p.fire(rig);
				}
			}
		}
	} else {
		p.moveTargetX = p.moveTargetZ = 0;
		if (p.vehicle && p.seat == 0 && cutscene) { p.vehicle->input.throttle = 0; p.vehicle->input.brake = 0.5; p.vehicle->input.steer = 0; }
	}
	for (auto& s : systems) s.second->preUpdate(dt);
	vehicles.update(dt);
	p.update(dt);
	for (auto& s : systems) s.second->update(dt);
	Vehicle* pv = p.vehicle;
	if (pv && pv->lightsOn) {
		const V3 f = pv->fwd();
		headlight.pos = pv->pos + f * (pv->def.L / 2); headlight.pos.y = pv->pos.y + 0.8;
		headlight.target = headlight.pos + f * 20; headlight.target.y = pv->pos.y - 1.5;
		headlight.intensity = 60;
	} else headlight.intensity = 0;
	env.update(dt);
	rig.update(realDt, controlsEnabled ? &input : nullptr, p);
}

void Game::tryEnterExit() {
	Player& p = *player;
	if (vehicles.isBusy(&p)) return;
	if (p.vehicle) { if (taxiHandleExit && taxiHandleExit(&p)) return; vehicles.exit(&p); return; }
	Vehicle* v = vehicles.nearestEnterable(p.pos, 5);
	if (!v) return;
	if (canEnterVehicle && !canEnterVehicle(v)) return;
	if (v->npcRemote && takeRemoteCar) takeRemoteCar(v);
	if (cabSeatFor) { const int cab = cabSeatFor(v, &p); if (cab >= 0) { vehicles.enter(&p, v, cab); return; } }
	int seat = 0;
	if (v->hasDoors()) v->nearestDoor(p.pos, seat);
	if (seat > 0 && v->occupants[seat]) {
		const int n = (int)v->layout.seats.size() < 2 ? 2 : (int)v->layout.seats.size();
		for (int k = 1; k <= 3; k++) if (k < n && !v->occupants[k]) { vehicles.enter(&p, v, k); return; }
	}
	vehicles.enter(&p, v, seat);
}

bool Game::weaponWheelOpen() const { return wheel && wheel->open; }

void Game::respawnPlayer(double x, double z, double yaw) {
	Player& p = *player;
	if (p.vehicle) p.vehicle->takeOut(&p);
	p.setPosition(x, z);
	p.yaw = yaw;
	rig.yaw = yaw + kPi;
}

} // namespace atg
