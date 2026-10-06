#include "Vehicles.h"
#include "Camera.h"
#include "Character.h"
#include "Game.h"
#include "Player.h"

namespace atg {

std::vector<std::pair<std::function<bool(const VehicleDef&)>, VehicleManager::Factory>>& VehicleManager::Factories() {
	static std::vector<std::pair<std::function<bool(const VehicleDef&)>, Factory>> F;
	return F;
}

bool VehicleManager::Supported(const VehicleDef& def) {
	if (def.kind.empty() && def.bike.empty()) return true;
	for (auto& f : Factories()) if (f.first(def)) return true;
	return false;
}

Vehicle* VehicleManager::spawn(const std::string& id, double x, double z, double yaw, const SpawnOpts& opts) {
	const VehicleDef* def = FindVehicle(id);
	if (!def) return nullptr;
	std::shared_ptr<Vehicle> v;
	for (auto& f : Factories()) if (f.first(*def)) { v = f.second(game, id, x, z, yaw, opts); break; }
	if (!v) { v = std::make_shared<Vehicle>(game, id, x, z, yaw, opts); v->setup(opts); }
	list.push_back(v);
	return v.get();
}

std::shared_ptr<Vehicle> VehicleManager::shared(Vehicle* v) const {
	for (const auto& p : list) if (p.get() == v) return p;
	return v ? v->shared_from_this() : nullptr;
}

void VehicleManager::remove(Vehicle* v) {
	if (!v) return;
	auto keep = shared(v);
	for (size_t i = 0; i < list.size(); i++) if (list[i].get() == v) { list.erase(list.begin() + i); break; }
	v->remove();
	game.graveyard(keep);
}

void VehicleManager::update(double dt) {
	for (int i = (int)list.size() - 1; i >= 0; i--) if (list[i]->removed) { game.graveyard(list[i]); list.erase(list.begin() + i); }
	const std::vector<std::shared_ptr<Vehicle>> snap = list;
	for (const auto& v : snap) {
		if (v->ai && !v->traffic && !v->policeUnit && !v->armyUnit && !v->remote && v->driver() && !v->driver()->isPlayer && !v->driver()->dead && !v->isWrecked()) v->ai->update(dt);
	}
	for (const auto& v : snap) if (!v->remote && !v->removed) v->update(dt);
	for (size_t i = 0; i < list.size(); i++) {
		Vehicle* A = list[i].get();
		for (size_t j = i + 1; j < list.size(); j++) {
			Vehicle* B = list[j].get();
			const double r = Max(A->hz, A->hx) + Max(B->hz, B->hx);
			if (!(Dist2(A->pos.x, A->pos.z, B->pos.x, B->pos.z) <= r * r)) continue;
			if (!(std::fabs(A->pos.y - B->pos.y) <= 2.2)) continue;
			carCar(A, B);
		}
	}
	const std::vector<Character*> chars = game.allCharacters();
	for (size_t i = 0; i < list.size(); i++) {
		Vehicle* v = list[i].get();
		if (v->removed) continue;
		const double spd = v->speedAbs();
		const double R = Hypot(v->hx, v->hz) + 0.5;
		for (Character* c : chars) {
			if (c->vehicle || c->removed) continue;
			if (!(std::fabs(c->pos.y - v->pos.y) <= 1.8)) continue;
			if (!(Dist2(c->pos.x, c->pos.z, v->pos.x, v->pos.z) <= R * R)) continue;
			if (c->ragdolling) { runOver(v, c, spd); continue; }
			carPed(v, c);
		}
	}
	for (int i = (int)seqs.size() - 1; i >= 0; i--) {
		Seq s = seqs[i];
		const bool done = runSeq(s, dt);
		// (runSeq may have added or removed sequences)
		for (size_t k = 0; k < seqs.size(); k++) if (seqs[k].chr == s.chr && seqs[k].veh == s.veh && seqs[k].enter == s.enter) {
			if (done) seqs.erase(seqs.begin() + k); else seqs[k] = s;
			break;
		}
	}
	streamTimer -= dt;
	if (streamTimer <= 0) { streamTimer = 0.5; streamParked(); }
	const V3 p = game.player->pos;
	for (int i = (int)list.size() - 1; i >= 0; i--) {
		Vehicle* v = list[i].get();
		if (v->isWrecked()) {
			v->wreckTime += dt;
			if (v->wreckTime > 40 && Dist2(v->pos.x, v->pos.z, p.x, p.z) > 80 * 80 && !v->persistent) remove(v);
		}
	}
}

// --------------------------------------------------------------- collisions
void VehicleManager::carCar(Vehicle* A, Vehicle* Bc) {
	auto corners = [](Vehicle* v, double out[4][2]) {
		const double s = std::sin(v->yaw), c = std::cos(v->yaw);
		const int sg[4][2] = { { 1, 1 }, { 1, -1 }, { -1, -1 }, { -1, 1 } };
		for (int k = 0; k < 4; k++) { const double lx = v->hx * sg[k][0], lz = v->hz * sg[k][1]; out[k][0] = v->pos.x + lx * c + lz * s; out[k][1] = v->pos.z - lx * s + lz * c; }
	};
	double ca[4][2], cb[4][2];
	corners(A, ca); corners(Bc, cb);
	double axes[4][2];
	{ int k = 0; for (Vehicle* v : { A, Bc }) { const double s = std::sin(v->yaw), c = std::cos(v->yaw); axes[k][0] = s; axes[k][1] = c; axes[k + 1][0] = c; axes[k + 1][1] = -s; k += 2; } }
	double minOv = kInf, nx = 0, nz = 0;
	for (const auto& a : axes) {
		const double ax = a[0], az = a[1];
		double aMin = kInf, aMax = -kInf, bMin = kInf, bMax = -kInf;
		for (const auto& p : ca) { const double d = p[0] * ax + p[1] * az; aMin = Min(aMin, d); aMax = Max(aMax, d); }
		for (const auto& p : cb) { const double d = p[0] * ax + p[1] * az; bMin = Min(bMin, d); bMax = Max(bMax, d); }
		const double ov = Min(aMax, bMax) - Max(aMin, bMin);
		if (ov <= 0) return;
		if (ov < minOv) {
			minOv = ov;
			const double dir = ((Bc->pos.x - A->pos.x) * ax + (Bc->pos.z - A->pos.z) * az) > 0 ? 1 : -1;
			nx = ax * dir; nz = az * dir;
		}
	}
	double px = 0, pz = 0; int n = 0;
	auto inside = [](Vehicle* v, double x, double z) { double lx, lz; v->worldToLocal(x, z, lx, lz); return std::fabs(lx) <= v->hx + 0.02 && std::fabs(lz) <= v->hz + 0.02; };
	for (const auto& p : ca) if (inside(Bc, p[0], p[1])) { px += p[0]; pz += p[1]; n++; }
	for (const auto& p : cb) if (inside(A, p[0], p[1])) { px += p[0]; pz += p[1]; n++; }
	if (n) { px /= n; pz /= n; } else { px = (A->pos.x + Bc->pos.x) / 2; pz = (A->pos.z + Bc->pos.z) / 2; }
	const double imA = 1 / A->mass, imB = 1 / Bc->mass;
	const double tot = imA + imB;
	A->pos.x -= nx * minOv * imA / tot; A->pos.z -= nz * minOv * imA / tot;
	Bc->pos.x += nx * minOv * imB / tot; Bc->pos.z += nz * minOv * imB / tot;
	const double rax = px - A->pos.x, raz = pz - A->pos.z, rbx = px - Bc->pos.x, rbz = pz - Bc->pos.z;
	const double vax = A->vel.x + A->r * raz, vaz = A->vel.z - A->r * rax;
	const double vbx = Bc->vel.x + Bc->r * rbz, vbz = Bc->vel.z - Bc->r * rbx;
	const double rvn = (vbx - vax) * nx + (vbz - vaz) * nz;
	Vehicle* tank = A->def.tank ? A : Bc->def.tank ? Bc : nullptr;
	if (tank) {
		Vehicle* other = tank == A ? Bc : A;
		if (!other->def.tank && (other->def.kind.empty() || other->proxy) && tank->speedAbs() > 1.2 && game.time - other->crushT > 0.35) {
			other->crushT = game.time;
			other->damage(240, tank->driver());
			other->dent(px, other->pos.y + 1.1, pz, 30);
			other->bodyYV -= 2.5;
			game.soundAt("crash", V3(px, other->pos.y + 0.8, pz), 0.8);
			if (game.effects) game.effects->sparks(V3(px, other->pos.y + 0.8, pz), 12);
		}
	}
	if (rvn >= 0) return;
	const double raN = raz * nx - rax * nz, rbN = rbz * nx - rbx * nz;
	const double e = 0.25;
	const double j = -(1 + e) * rvn / (imA + imB + raN * raN / A->I + rbN * rbN / Bc->I);
	A->vel.x -= j * nx * imA; A->vel.z -= j * nz * imA; A->r -= raN * j / A->I;
	Bc->vel.x += j * nx * imB; Bc->vel.z += j * nz * imB; Bc->r += rbN * j / Bc->I;
	const double impact = -rvn;
	A->onCrashVehicle(impact, Bc); Bc->onCrashVehicle(impact, A);
	if (impact > 7) {
		const double kA = Min(2.4, 2 * Bc->mass / (A->mass + Bc->mass)), kB = Min(2.4, 2 * A->mass / (A->mass + Bc->mass));
		const V3 cp(px, (A->pos.y + Bc->pos.y) / 2 + 0.5, pz);
		A->crashParts(cp, impact * kA); Bc->crashParts(cp, impact * kB);
		if (!A->tb) A->crashTumble(V3(-nx, 0, -nz), impact, kA);
		if (!Bc->tb) Bc->crashTumble(V3(nx, 0, nz), impact, kB);
	}
	if (impact > 2.5) {
		const double dmg = (impact - 2.5) * 12;
		A->damage(dmg * (Bc->mass / (A->mass + Bc->mass)) * 2, Bc->driver());
		Bc->damage(dmg * (A->mass / (A->mass + Bc->mass)) * 2, A->driver());
		A->dent(px, A->pos.y + 0.6, pz, impact); Bc->dent(px, Bc->pos.y + 0.6, pz, impact);
		if (game.time - A->lastHit > 0.3) {
			A->lastHit = game.time;
			game.soundAt("crash", V3(px, A->pos.y + 0.5, pz), Clamp(impact / 16, 0.25, 1));
			if (game.effects) game.effects->sparks(V3(px, A->pos.y + 0.5, pz), impact);
			Player* pl = game.player.get();
			if (pl->vehicle == A || pl->vehicle == Bc) game.rig.addShake(Min(0.9, impact / 20));
		}
		game.events.carCrash.emit(A, Bc, impact);
	}
}

void VehicleManager::carPed(Vehicle* v, Character* c) {
	double lx, lz; v->worldToLocal(c->pos.x, c->pos.z, lx, lz);
	const double r = c->radius;
	const double qx = Clamp(lx, -v->hx, v->hx), qz = Clamp(lz, -v->hz, v->hz);
	const double dx = lx - qx, dz = lz - qz;
	const double d2 = dx * dx + dz * dz;
	if (d2 > r * r) return;
	const double s = std::sin(v->yaw), co = std::cos(v->yaw);
	double nlx = dx, nlz = dz;
	double d = std::sqrt(d2);
	if (d < 1e-4) {
		const double ex = v->hx - std::fabs(lx), ez = v->hz - std::fabs(lz);
		if (ex < ez) { nlx = Sgn(lx) != 0 ? Sgn(lx) : 1; nlz = 0; } else { nlx = 0; nlz = Sgn(lz) != 0 ? Sgn(lz) : 1; }
		d = 0;
	} else { nlx /= d; nlz /= d; }
	const double nx = nlx * co + nlz * s, nz = -nlx * s + nlz * co;
	const double px = c->pos.x - v->pos.x, pz = c->pos.z - v->pos.z;
	const double vpx = v->vel.x + v->r * pz, vpz = v->vel.z - v->r * px;
	const double carN = vpx * nx + vpz * nz;
	const double pedN = c->vel.x * nx + c->vel.z * nz;
	const double rel = carN - Max(0, pedN);
	if (rel > 3.2 && carN > 3.2 && !c->invincible) {
		const double spd = Hypot(vpx, vpz);
		const double dmg = std::pow(rel - 2.5, 2) * 2.4 + 6;
		const V3 imp(vpx * 0.85 + nx * 2, 2.2 + spd * 0.22, vpz * 0.85 + nz * 2);
		c->vel.set(0, 0, 0);
		auto keep = c->shared_from_this();
		game.events.pedHitByCar.emit(c, v, rel);
		DamageInfo di; di.type = "vehicle"; di.source = v->driver(); di.hasImpulse = true; di.impulse = imp; di.knockdown = true;
		c->takeDamage(dmg, di);
		if (c->dead && !c->ragdolling) c->startRagdoll(imp);
		game.soundAt("bodyhit", c->pos, Clamp(rel / 12, 0.3, 1));
		if (game.effects) game.effects->blood(c->chestPos(), V3(nx, 0.5, nz), Min(20, rel * 2));
		v->vel *= 1 - Clamp(80 / v->mass, 0.01, 0.08);
		v->bodyYV += 0.4;
	} else {
		const double push = r - d + 0.02;
		c->pos.x += nx * push; c->pos.z += nz * push;
		if (pedN < 0) { c->vel.x -= nx * pedN; c->vel.z -= nz * pedN; }
		if (carN > 0.6 && !c->isPlayer) c->onBumped(v);
	}
}

void VehicleManager::runOver(Vehicle* v, Character* c, double spd) {
	if (spd < 3 || !c->ragdoll) return;
	double lx, lz; v->worldToLocal(c->ragdoll->pos[0], c->ragdoll->pos[2], lx, lz);
	if (std::fabs(lx) < v->hx && std::fabs(lz) < v->hz) {
		if (c->runOverT < 0 || game.time - c->runOverT > 0.5) {
			c->runOverT = game.time;
			v->bodyYV += 1.2;
			v->bodyRollV += (lx > 0 ? 1 : -1) * 0.8;
			auto keep = c->shared_from_this();
			if (!c->dead) { DamageInfo di; di.type = "vehicle"; di.source = v->driver(); c->takeDamage(spd * 4, di); }
			c->ragdoll->push(0, v->vel.x * 0.5, 1, v->vel.z * 0.5);
			game.soundAt("bodyhit", c->pos, 0.5);
			game.events.pedRunOver.emit(c, v);
		}
	}
}

// --------------------------------------------------------------- enter / exit
Vehicle* VehicleManager::nearestEnterable(const V3& p, double maxDist) {
	Vehicle* best = nullptr;
	double bd = maxDist * maxDist;
	for (const auto& vp : list) {
		Vehicle* v = vp.get();
		if (v->isWrecked() || v->removed || (v->locked && !v->npcRemote)) continue;
		int seat;
		const V3 dp = v->hasDoors() ? v->nearestDoor(p, seat) : v->doorWorld();
		const double d = Dist2(p.x, p.z, dp.x, dp.z);
		const double dc = v->hasDoors() ? kInf : Dist2(p.x, p.z, v->pos.x, v->pos.z);
		const double dd = Min(d, dc * 0.8);
		if (dd < bd && std::fabs(v->pos.y - p.y) < 2) { bd = dd; best = v; }
	}
	return best;
}

bool VehicleManager::isBusy(const Character* c) const { for (const Seq& s : seqs) if (s.chr.get() == c) return true; return false; }

bool VehicleManager::enter(Character* c, Vehicle* v, int seat, bool force) {
	if (isBusy(c) || v->isWrecked()) return false;
	if (v->locked && !force) { game.sound("locked"); return false; }
	Seq s; s.enter = true; s.chr = c->shared_from_this(); s.veh = shared(v); s.seat = seat; s.phase = "approach"; s.force = force;
	seqs.push_back(s);
	return true;
}

bool VehicleManager::seatNow(Character* c, Vehicle* v, int seat) {
	if (c->vehicle) c->vehicle->takeOut(c);
	for (size_t i = 0; i < seqs.size();) { if (seqs[i].chr.get() == c) seqs.erase(seqs.begin() + i); else i++; }
	c->ragdolling = false;
	if (c->isPlayer) { Player* p = static_cast<Player*>(c); p->skydive = false; p->closeChute(); }
	v->putIn(c, seat);
	c->onEnteredVehicle(v);
	game.events.enteredVehicle.emit(c, v);
	return true;
}

bool VehicleManager::exit(Character* c) {
	Vehicle* veh = c->vehicle;
	if (!veh || isBusy(c)) return false;
	if (veh->def.aircraft && (!veh->isGrounded() || veh->speedAbs() > 9)) {
		const double alt = veh->altitude();
		const V3 side = veh->localPoint(veh->hx + 1.6, veh->cgY(), 0);
		veh->takeOut(c, &side);
		c->pos.y = side.y - 1;
		c->vel = veh->vel * 0.8;
		c->grounded = false;
		if (c->isPlayer) static_cast<Player*>(c)->bailOut(alt);
		game.events.exitedVehicle.emit(c, veh);
		return true;
	}
	if (veh->speedAbs() > 9 && c->isPlayer) {
		const V3 at = veh->localToWorld(veh->hx + 1.0, 0, 0);
		veh->takeOut(c, &at);
		c->vel.set(veh->vel.x * 0.7, 2, veh->vel.z * 0.7);
		c->knockDown(V3(veh->vel.x * 0.1, 1.5, veh->vel.z * 0.1));
		DamageInfo di; di.type = "fall";
		c->takeDamage(Min(30, veh->speedAbs() * 0.8), di);
		return true;
	}
	Seq s; s.enter = false; s.chr = c->shared_from_this(); s.veh = shared(veh); s.phase = "open";
	seqs.push_back(s);
	return true;
}

V3 VehicleManager::doorTarget(Vehicle* veh, int seat, Character* c) {
	V3 out;
	if (veh->doorFor(seat, c, out)) return out;
	const V3& d = veh->layout.doorPos;
	const double x = seat % 2 == 0 ? d.x : -d.x;
	const auto& S = veh->layout.seats;
	const double z = seat < 2 || S.size() < 3 ? d.z : d.z + (S[2].z - S[0].z);
	return veh->localToWorld(x, 0, z);
}

bool VehicleManager::runSeq(Seq& s, double dt) {
	Character* chr = s.chr.get();
	Vehicle* veh = s.veh.get();
	s.t += dt;
	if (chr->dead || chr->removed || chr->ragdolling) { veh->doorOpen = 0; return true; }
	if (s.enter) {
		if (veh->isWrecked()) return true;
		if (s.phase == "approach") {
			const V3 tp = doorTarget(veh, s.seat, chr);
			const double dx = tp.x - chr->pos.x, dz = tp.z - chr->pos.z;
			const double d = Hypot(dx, dz);
			if (d < 0.35 || s.t > 2.5) {
				chr->moveTargetX = chr->moveTargetZ = 0; chr->vel.set(0, 0, 0);
				if (s.t > 2.5 && d > 1.5) { chr->pos.x = tp.x; chr->pos.z = tp.z; }
				s.phase = "open"; s.t = 0;
				if (veh->speedAbs() > 4 && !s.force) return true;
			} else {
				const double sp = Min(chr->isPlayer ? 4.5 : 3.5, d * 6);
				chr->moveTargetX = dx / d * sp; chr->moveTargetZ = dz / d * sp;
				chr->faceTowards(tp.x, tp.z, dt, 14);
			}
			return false;
		}
		chr->moveTargetX = chr->moveTargetZ = 0;
		const double sideYaw = veh->yaw + (s.seat % 2 == 0 ? -kPi / 2 : kPi / 2);
		chr->yaw = sideYaw;
		if (s.phase == "open") {
			if (s.seat == 0) veh->doorOpen = Min(1, s.t / 0.3);
			if (s.t >= 0.3) {
				Character* occupant = veh->occupants[s.seat].get();
				if (occupant && occupant != chr) { s.phase = "jack"; s.t = 0; chr->anim->play("pull"); if (veh->def.bike.empty()) game.sound("doorOpen"); }
				else { s.phase = "enter"; s.t = 0; beginSit(s); }
			}
			return false;
		}
		if (s.phase == "jack") {
			auto occupant = veh->occupants[s.seat];
			if (s.t > 0.35 && occupant && occupant.get() != chr) {
				const V3 at = doorTarget(veh, s.seat) + V3(std::sin(sideYaw + kPi), 0, std::cos(sideYaw + kPi)) * -0.6;
				veh->takeOut(occupant.get(), &at);
				occupant->knockDown(V3(-std::sin(sideYaw) * 3.5, 1.5, -std::cos(sideYaw) * 3.5));
				occupant->onCarjacked(chr);
				game.events.carjack.emit(chr, occupant.get(), veh);
			}
			if (s.t > 0.8) { s.phase = "enter"; s.t = 0; beginSit(s); }
			return false;
		}
		if (s.phase == "enter") {
			const double k = Min(1, s.t / 0.45);
			chr->rootLocalPos = s.fromLocal.lerp(s.toLocal, k);
			if (k >= 1) { s.phase = "close"; s.t = 0; }
			return false;
		}
		if (s.phase == "close") {
			if (s.seat == 0) veh->doorOpen = Max(0, 1 - s.t / 0.3);
			if (s.t >= 0.3) {
				veh->doorOpen = 0;
				if (veh->def.bike.empty()) game.soundAt("doorClose", veh->pos, 0.7);
				chr->onEnteredVehicle(veh);
				game.events.enteredVehicle.emit(chr, veh);
				return true;
			}
			return false;
		}
	} else {
		if (s.phase == "open") {
			if (chr->seat == 0) veh->doorOpen = Min(1, s.t / 0.3);
			veh->input.throttle = 0; veh->input.brake = 1;
			if (s.t >= 0.3) {
				const int seat = chr->seat;
				const V3 tp = doorTarget(veh, seat);
				veh->takeOut(chr, &tp);
				chr->yaw = veh->yaw + (seat % 2 == 0 ? kPi / 2 : -kPi / 2);
				veh->input.brake = 0; veh->input.handbrake = true;
				s.phase = "close"; s.t = 0; s.seat = seat;
				game.events.exitedVehicle.emit(chr, veh);
			}
			return false;
		}
		if (s.phase == "close") {
			if (s.seat == 0) veh->doorOpen = Max(0, 1 - s.t / 0.3);
			if (s.t >= 0.3) {
				veh->doorOpen = 0;
				if (veh->def.bike.empty()) game.soundAt("doorClose", veh->pos, 0.6);
				if (chr->bailFlee) { Vehicle* from = chr->bailFlee; chr->bailFlee = nullptr; if (!chr->isPlayer) chr->fleeFrom(from->pos); }
				return true;
			}
			return false;
		}
	}
	return true;
}

void VehicleManager::beginSit(Seq& s) {
	Character* chr = s.chr.get();
	Vehicle* veh = s.veh.get();
	const V3 door = doorTarget(veh, s.seat);
	veh->putIn(chr, s.seat);
	double lx, lz; veh->worldToLocal(door.x, door.z, lx, lz);
	s.toLocal = chr->rootLocalPos;
	s.fromLocal = V3(lx, 0, lz);
	chr->rootLocalPos = s.fromLocal;
	chr->yaw = veh->yaw;
}

// --------------------------------------------------------------- parked cars
void VehicleManager::streamParked() {
	const auto& spots = game.map.parkingSpots;
	const V3 p = game.player->vehicle ? game.player->vehicle->pos : game.player->pos;
	for (auto it = parked.begin(); it != parked.end();) {
		Vehicle* v = it->second.get();
		if (!v || v->removed) { it = parked.erase(it); continue; }
		const double d2 = Dist2(v->pos.x, v->pos.z, p.x, p.z);
		const bool touched = !v->parked || v->driver() || v->health < 1000 || v->persistent;
		if (touched) { consumedSpots.insert(it->first); it = parked.erase(it); continue; }
		if (d2 > 170 * 170) { remove(v); it = parked.erase(it); continue; }
		++it;
	}
	if (consumedSpots.size() > 200) consumedSpots.clear();
	if ((int)parked.size() >= maxParked) return;
	int added = 0;
	std::vector<std::pair<std::string, double>> pool;
	for (const auto& e : TrafficPool()) if (e.first != "boxer") pool.push_back(e);
	for (int i = 0; i < (int)spots.size() && added < 3; i++) {
		const ParkingSpot& s = spots[i];
		const double d2 = Dist2(s.x, s.z, p.x, p.z);
		if (d2 > 110 * 110 || d2 < 30 * 30) continue;
		if (parked.count(i) || consumedSpots.count(i)) continue;
		if (Hash2(i, 77) > 0.5) continue;
		bool blocked = false;
		for (const auto& v : list) if (Dist2(v->pos.x, v->pos.z, s.x, s.z) < 36) { blocked = true; break; }
		if (blocked) continue;
		RNG rng((uint32_t)(i * 31 + 7));
		std::string type;
		if (s.police) type = "police";
		else if (s.fancy) type = rng.Pick(std::vector<std::string>{ "zenith", "kestrel", "summit" });
		else if (s.district == "hood") type = rng.Weighted(std::vector<std::pair<std::string, double>>{ { "bouncer", 3 }, { "meridian", 4 }, { "brawler", 2 }, { "hauler", 2 }, { "summit", 1 } });
		else if (s.district == "docks") type = rng.Weighted(std::vector<std::pair<std::string, double>>{ { "boxer", 2 }, { "parcel", 3 }, { "hauler", 3 } });
		else if (rng.Chance(0.12)) type = rng.Pick(BikeIds());
		else type = rng.Weighted(pool);
		if (!Supported(*FindVehicle(type))) continue; // (parked motorbikes come with bikes.js)
		SpawnOpts o; o.parked = true;
		Vehicle* v = spawn(type, s.x, s.z, s.rot, o);
		if (!v) continue;
		if (s.police) v->locked = false;
		parked[i] = Ref<Vehicle>(v);
		added++;
		if ((int)parked.size() >= maxParked) break;
	}
}

} // namespace atg
