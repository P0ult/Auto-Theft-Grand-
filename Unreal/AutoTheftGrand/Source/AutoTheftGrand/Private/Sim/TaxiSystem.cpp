#include "TaxiSystem.h"
#include "FreeRoam.h"
#include "Game.h"
#include "Gameplay.h"
#include "Missions.h"
#include "Peds.h"
#include "Pickups.h"
#include "Traffic.h"
#include "Hud.h"

namespace atg {

namespace {
const std::vector<std::string> JOB_HELLO = { "Thank god, a cab!", "Finally. I've been waving for ages.", "You free? Great.", "Hey! Over here!", "Am I glad to see you." };
const std::vector<std::string> JOB_FAST = { "Now THAT is driving!", "Keep the change, speed racer.", "Record time! Here, take a tip." };
const std::vector<std::string> JOB_SLOW = { "I could have walked faster.", "Forget it, I'll walk.", "Worst. Cab. Ever." };
const std::vector<std::string> JOB_BYE = { "Thanks, driver.", "Appreciate it.", "Have a good one.", "Cheers!" };
} // namespace

TaxiSystem::TaxiSystem(Game& g) : game(g) {
	g.taxiHandleExit = [this](Character* c) { return c && c->isPlayer && handleExit(c); };
	g.cabSeatFor = [this](Vehicle* v, Character* c) { return seatFor(v, c); };
}

bool TaxiSystem::handleExit(Character* p) {
	Ride* r = ride.get();
	if (!r || p->vehicle != r->cab.get() || !r->cab || r->cab->speedAbs() < 2) return false;
	if (!r->stopRequest) { r->stopRequest = true; r->cab->ai = std::make_unique<HoldDriver>(r->cab.get()); if (game.hud) game.hud->help("Sure thing, pulling over.", 3); }
	return true;
}

int TaxiSystem::seatFor(Vehicle* v, Character* p) {
	if (v->hailedBy.get() != p || !cabDriven(v)) return -1;
	return rearSeat(v, p->pos);
}

int TaxiSystem::rearSeat(Vehicle* v, const V3& pos) const {
	const int n = (int)std::min<size_t>(v->layout.seats.size(), 4);
	std::vector<int> seats = n > 2 ? std::vector<int>{ 2, 3 } : std::vector<int>{ 1 };
	std::vector<int> free;
	for (int s : seats) if (!v->occupants[s]) free.push_back(s);
	if (free.empty()) {
		for (int s = 1; s < 4; s++) if (!v->occupants[s]) return s;
		return -1;
	}
	double lx, lz; v->worldToLocal(pos.x, pos.z, lx, lz);
	const int want = lx > 0 ? 2 : 3;
	for (int s : free) if (s == want) return s;
	return free[0];
}

bool TaxiSystem::cabDriven(Vehicle* v) const {
	return v && !v->remote && v->driver() && !v->driver()->isPlayer && !v->driver()->dead && !v->isWrecked() && !v->removed;
}

bool TaxiSystem::ctl() const {
	return game.input.enabled && !game.cutscene && !game.player->dead && game.gameplay && game.gameplay->state == "playing" && !game.menuOpen;
}

std::vector<FreeDestination> TaxiSystem::destinations() const {
	if (!game.freeroam) return {};
	std::vector<FreeDestination> out;
	for (const auto& d : game.freeroam->destinations()) {
		if (d.group == "Quick" && d.name != "Safehouse") continue;
		std::string lower = d.name; for (char& c : lower) c = (char)std::tolower((unsigned char)c);
		if (lower.find("restricted") != std::string::npos || lower.find("helipad") != std::string::npos) continue;
		out.push_back(d);
	}
	return out;
}

double TaxiSystem::estimate(double x, double z) const {
	const V3 c = ride && ride->cab ? ride->cab->pos : game.player->pos;
	return JsRound(TAXI_FLAG + TAXI_PER_KM * Hypot(x - c.x, z - c.z) * 1.3 / 1000);
}

void TaxiSystem::update(double dt) {
	Player& p = *game.player;
	const bool ok = ctl();
	if (ok && !p.vehicle && game.input.hit("hail")) hailCab();
	if (ok && !p.vehicle && game.input.hit("passenger")) enterAsPassenger();
	if (hail) updateHail(dt);
	if (held.first) updateHeld(dt);

	Vehicle* v = p.vehicle;
	if (!ride && v && p.seat > 0 && v->def.taxi && cabDriven(v) && !game.vehicles.isBusy(&p)) beginRide(v);
	if (ride) updateRide(dt, ok);

	if (v && p.seat == 0 && v->def.taxi) {
		if (ok && game.input.hit("taxiJob")) { if (job) endJob("Off duty. Thanks for driving with Los Soles Cabs."); else startJob(v); }
		else if (!job && !jobHinted && !game.missionActive) { jobHinted = true; if (game.hud) game.hud->help("You're driving a cab. Press <b>J</b> to start picking up fares.", 6); }
	}
	if (job) updateJob(dt);
	// the meter
	if (ride && ride->state == "ride") {
		const V3 c = ride->cab->pos;
		const double left = Hypot(ride->tx - c.x, ride->tz - c.z);
		if (game.hud) {
			char fare[24]; std::snprintf(fare, sizeof fare, "%.2f", ride->fare);
			char km[16]; std::snprintf(km, sizeof km, "%.1f", left / 1000);
			game.hud->setCounter("LOS SOLES CABS", "$" + std::string(fare) + " \xC2\xB7 " + ride->destName + " \xC2\xB7 " + km + " km");
		}
	} else if (ride && ride->state == "leaving") { if (game.hud) game.hud->setCounter("", ""); }
}

void TaxiSystem::hailCab() {
	Player& p = *game.player;
	if (ride) return;
	if (p.anim) p.anim->play("wave");
	game.sound("whistle");
	if (hail && !hail->cab->removed) { if (game.hud) game.hud->help("Your cab is on its way.", 3); return; }
	Vehicle* best = nullptr; double bd = 120 * 120;
	for (const auto& vv : game.vehicles.list) {
		Vehicle* q = vv.get();
		if (!q || !q->def.taxi || !cabDriven(q) || q->hailedBy || (job && q == job->cab.get())) continue;
		bool taken = false;
		for (int i = 1; i < 4; i++) if (q->occupants[i]) { taken = true; break; }
		if (taken) continue;
		const double d = Dist2(q->pos.x, q->pos.z, p.pos.x, p.pos.z);
		if (d < bd) { bd = d; best = q; }
	}
	if (!best) {
		Traffic::Sample smp;
		if (Traffic::SampleLane(game, p.pos.x, p.pos.z, 90, 220, smp, [](const REdge& e) { return e.type != ERoad::Freeway && e.type != ERoad::Ramp; }) && game.traffic) {
			best = game.traffic->spawnCar(smp.start, smp.s0, "taxi");
			if (best) for (int i = 1; i < 4; i++) { auto o = best->occupants[i]; if (o) { best->occupants[i] = nullptr; o->vehicle = nullptr; if (Ped* q = dynamic_cast<Ped*>(o.get())) game.peds->remove(q); } }
		}
	}
	if (!best) { if (game.hud) game.hud->help("No cabs are answering out here.", 3); return; }
	sendCab(best);
	if (game.hud) game.hud->help("Cab on its way \xE2\x80\x94 look for the <b style=\"color:#ffd23f\">yellow</b> blip. Press <b>F</b> by the back door when it pulls up.", 5);
}

void TaxiSystem::sendCab(Vehicle* cab) {
	Player& p = *game.player;
	RoadSpot s;
	if (!FindRoadSpot(game, p.pos.x, p.pos.z, false, s)) s = { p.pos.x, p.pos.z, 0, 0 };
	cab->persistent = true;
	cab->hailedBy = Ref<Character>(p.shared_from_this());
	RouteDriverOpts o; o.speed = 15; o.ignoreLights = false; o.arriveR = 10;
	cab->ai = std::make_unique<RouteDriver>(game, cab, V3(s.x, s.y, s.z), o);
	Blip b; b.x = cab->pos.x; b.z = cab->pos.z; b.icon = "taxi"; b.color = 0xffd23f;
	auto blip = game.addBlip(b);
	hail = std::make_unique<Hail>();
	hail->cab = Ref<Vehicle>(cab);
	hail->t = 0; hail->wait = 0; hail->x = s.x; hail->z = s.z; hail->y = s.y; hail->yaw = s.yaw; hail->blip = blip; hail->told = false;
}

void TaxiSystem::updateHail(double dt) {
	Player& p = *game.player;
	Hail& h = *hail;
	Vehicle* cab = h.cab.get();
	h.t += dt;
	if (!cab || !cabDriven(cab) || p.dead) return clearHail(true, cab && cab->isWrecked() ? "Your cab won't be coming." : nullptr);
	if (p.vehicle == cab) { clearHail(false); return; }
	if (p.vehicle) return clearHail(true, nullptr);
	if (h.blip) { h.blip->x = cab->pos.x; h.blip->z = cab->pos.z; }
	if (Hypot(h.x - p.pos.x, h.z - p.pos.z) > 35 && !game.vehicles.isBusy(&p)) {
		RoadSpot s;
		if (!FindRoadSpot(game, p.pos.x, p.pos.z, false, s)) s = { p.pos.x, p.pos.z, 0, 0 };
		h.x = s.x; h.z = s.z; h.y = s.y; h.yaw = s.yaw;
		RouteDriverOpts o; o.speed = 15; o.ignoreLights = false; o.arriveR = 10; o.snap = false;
		cab->ai = std::make_unique<RouteDriver>(game, cab, V3(s.x, s.y, s.z), o);
	}
	auto* rd = dynamic_cast<RouteDriver*>(cab->ai.get());
	if (rd && rd->arrived && cab->speedAbs() < 1) {
		h.wait += dt;
		if (!h.told) { h.told = true; game.soundAt("horn", cab->pos, 0.6); if (game.hud) game.hud->help("Your cab is here \xE2\x80\x94 press <b>F</b> by the back door to get in.", 5); }
		if (h.wait > 60 && !game.vehicles.isBusy(&p)) clearHail(true, "The cab driver got tired of waiting.");
	}
	if (h.t > 180) clearHail(true, "Looks like your cab got lost.");
}

void TaxiSystem::clearHail(bool release, const char* msg) {
	if (!hail) return;
	Hail h = std::move(*hail);
	hail.reset();
	game.removeBlip(h.blip);
	if (release) {
		Vehicle* cab = h.cab.get();
		if (cab) releaseCab(cab);
	}
	if (msg && game.hud) game.hud->help(msg, 3);
}

void TaxiSystem::releaseCab(Vehicle* cab) {
	cab->hailedBy = nullptr;
	cab->persistent = false;
	if (cabDriven(cab)) cab->ai = std::make_shared<LaneDriver>(game, cab, nullptr);
}

void TaxiSystem::enterAsPassenger() {
	Player& p = *game.player;
	if (game.vehicles.isBusy(&p)) return;
	Vehicle* v = game.vehicles.nearestEnterable(p.pos, 5.5);
	double best = 5.5 * 5.5;
	for (const auto& o : game.vehicles.list) {
		Vehicle* q = o.get();
		if (!q || !q->remote || q->exploded) continue;
		const double d = Dist2(q->pos.x, q->pos.z, p.pos.x, p.pos.z) - q->hz * q->hz;
		if (d < best) { best = d; v = q; }
	}
	if (!v) return;
	if (v->remote) {
		const int n = (int)std::min<size_t>(v->layout.seats.size(), 4);
		for (int s = 1; s < n; s++) if (!v->occupants[s]) { game.vehicles.enter(&p, v, s, true); return; }
		if (game.hud) game.hud->help("No room in there.", 2);
		return;
	}
	if (v->def.aircraft || v->def.train || v->def.tank) return;
	if (game.canEnterVehicle && !game.canEnterVehicle(v)) return;
	if (!v->driver() || v->driver()->isPlayer) { game.tryEnterExit(); return; }
	if (v->speedAbs() > 2.5) return;
	const int seat = rearSeat(v, p.pos);
	if (seat < 0) { if (game.hud) game.hud->help("No room in there.", 2); return; }
	if (cabDriven(v) && v->ai) { v->persistent = true; v->ai = std::make_unique<HoldDriver>(v); held = { Ref<Vehicle>(v), 0 }; }
	game.vehicles.enter(&p, v, seat);
}

void TaxiSystem::updateHeld(double dt) {
	Player& p = *game.player;
	Vehicle* v = held.first.get();
	held.second += dt;
	if (v && game.vehicles.isBusy(&p) && held.second < 6) return;
	if (v && !(p.vehicle == v && v->def.taxi)) { v->persistent = false; if (cabDriven(v)) v->ai = std::make_unique<LaneDriver>(game, v, nullptr); }
	held = { Ref<Vehicle>(), 0 };
}

void TaxiSystem::beginRide(Vehicle* cab) {
	clearHail(false);
	cab->hailedBy = Ref<Character>(game.player.get());
	cab->persistent = true;
	cab->ai = std::make_unique<HoldDriver>(cab);
	ride = std::make_unique<Ride>();
	ride->cab = Ref<Vehicle>(cab);
	ride->state = "choose";
	ride->destName.clear();
	ride->last = cab->pos;
	if (const auto w = game.hud ? game.hud->waypoint() : std::nullopt) setDest("your waypoint", w->x, w->z, true);
	else openPicker();
}

void TaxiSystem::openPicker() {
	if (!game.hud || !ride) return;
	ride->picked = false; ride->picking = true;
	tpGroup.clear();
	PanelOpts o; o.inVehicle = true; o.leave = "Never mind \xE2\x80\x94 let me out";
	o.refresh = [this]() { return pickerRows(); };
	o.onClose = [this]() { if (ride) ride->picking = false; };
	game.hud->openPanel("LOS SOLES CABS", "\"Where to?\" \xE2\x80\x94 or set a waypoint on the map and hop in again.", pickerRows(), o);
}

std::vector<StoreItem> TaxiSystem::pickerRows() {
	std::vector<StoreItem> out;
	const auto all = destinations();
	const double cash = game.player->money;
	if (tpGroup.empty()) {
		std::vector<std::string> groups;
		for (const auto& d : all) if (std::find(groups.begin(), groups.end(), d.group) == groups.end()) groups.push_back(d.group);
		for (const std::string& gname : groups) {
			StoreItem it; it.name = gname;
			int n = 0; for (const auto& d : all) if (d.group == gname) n++;
			it.desc = std::to_string(n) + (n == 1 ? " place" : " places"); it.button = "Open";
			it.use = [this, gname](Player&, Game&) { tpGroup = gname; return std::string(); };
			out.push_back(it);
		}
		return out;
	}
	StoreItem back; back.name = "\xE2\x86\x90 Back"; back.button = "Back";
	back.use = [this](Player&, Game&) { tpGroup.clear(); return std::string(); };
	out.push_back(back);
	for (const auto& d : all) {
		if (d.group != tpGroup) continue;
		const double fare = estimate(d.x, d.z);
		StoreItem it; it.name = d.name;
		it.desc = "$" + std::to_string((int)fare); it.button = "$" + std::to_string((int)fare);
		it.price = 0;
		if (!game.freeroamActive() && fare > game.player->money) it.desc += " \xE2\x80\x94 more than you have";
		it.closes = true;
		it.use = [this, name = d.name, x = d.x, z = d.z](Player&, Game& g) {
			if (ride) { ride->picked = true; setDest(name, x, z); }
			g.sound("ui");
			return std::string();
		};
		out.push_back(it);
	}
	return out;
}

void TaxiSystem::setDest(const std::string& name, double x, double z, bool fromWaypoint) {
	Ride* r = ride.get();
	if (!r) return;
	r->wp = fromWaypoint ? std::optional<V2>(V2{ x, z }) : std::nullopt;
	RoadSpot s;
	if (!FindRoadSpot(game, x, z, false, s)) s = { x, z, game.map.GroundHeight(x, z), 0 };
	r->destName = name;
	r->tx = s.x; r->tz = s.z; r->ty = s.y; r->tyaw = s.yaw;
	r->state = "ride";
	r->est = Max(r->fare, estimate(s.x, s.z));
	if (Vehicle* cab = r->cab.get()) {
		RouteDriverOpts o; o.speed = 19; o.ignoreLights = false; o.arriveR = 13;
		cab->ai = std::make_unique<RouteDriver>(game, cab, V3(s.x, s.y, s.z), o);
	}
	if (!game.missionActive && game.hud) game.hud->routeTo(V2{ s.x, s.z });
	if (game.hud) game.hud->help("Heading to <b>" + name + "</b>. Press <b>Space</b> to skip the trip (about $" + std::to_string((int)r->est) + "), or <b>F</b> to get out.", 6);
}

void TaxiSystem::updateRide(double dt, bool ok) {
	Ride& r = *ride;
	Vehicle* cab = r.cab.get();
	Player& p = *game.player;
	r.t += dt;
	if (r.state == "leaving") {
		if (p.vehicle != cab && !game.vehicles.isBusy(&p)) { if (cab) releaseCab(cab); endRide(); }
		return;
	}
	if (p.vehicle != cab) {
		if (r.state == "ride" && r.dist > 40) charge(r.fare, "Fare so far");
		if (cab) releaseCab(cab);
		endRide();
		return;
	}
	if (!cabDriven(cab)) { if (game.hud) game.hud->help("Your driver is out of action. Looks like you're walking.", 3); endRide(); return; }
	if (r.state == "choose") {
		if (!r.picking && !r.picked) { r.state = "leaving"; if (!game.vehicles.isBusy(&p)) game.vehicles.exit(&p); }
		return;
	}
	const double dd = Hypot(cab->pos.x - r.last.x, cab->pos.z - r.last.z);
	r.last = cab->pos;
	if (dd < 60) r.dist += dd;
	r.fare = TAXI_FLAG + TAXI_PER_KM * r.dist / 1000;
	// a new waypoint mid-ride: change course
	if (const auto w = game.hud ? game.hud->waypoint() : std::nullopt) if (r.wp && (w->x != r.wp->x || w->z != r.wp->z) && !r.skipping) setDest("your waypoint", w->x, w->z, true);
	if (ok && game.input.hit("skipTrip") && !r.skipping && !r.stopRequest) skip();
	if (r.stopRequest) {
		if (cab->speedAbs() < 1) { charge(r.fare, "Fare"); r.state = "leaving"; game.vehicles.exit(&p); }
		return;
	}
	auto* rd = dynamic_cast<RouteDriver*>(cab->ai.get());
	if (rd && rd->arrived && cab->speedAbs() < 1.2) arrive();
}

void TaxiSystem::skip() {
	Ride* r = ride.get();
	if (!r || r->state != "ride") return;
	r->skipping = true;
	const double price = JsRound(Max(r->fare, r->est));
	Ref<Vehicle> cabRef = r->cab;
	auto go = [this, r, price, cabRef]() {
		if (ride.get() != r) return;
		Player& p = *game.player;
		Vehicle* cab = cabRef.get();
		if (!cab || p.vehicle != cab) return;
		cab->pos.set(r->tx, IsSet(r->ty) ? r->ty : game.map.GroundHeight(r->tx, r->tz), r->tz);
		cab->yaw = IsSet(r->tyaw) ? r->tyaw : cab->yaw;
		cab->vel = V3(); cab->r = 0;
		r->last = cab->pos;
		r->fare = price; r->dist = (price - TAXI_FLAG) / TAXI_PER_KM * 1000;
		RouteDriverOpts o; o.speed = 19; o.ignoreLights = false; o.arriveR = 13;
		cab->ai = std::make_unique<RouteDriver>(game, cab, V3(r->tx, r->ty, r->tz), o);
		game.env.setTime(game.env.hours + Clamp(r->dist / 15 / 60, 2, 30) / 60);
		if (game.traffic) game.traffic->populate(10);
		if (game.peds) game.peds->populate(10);
		r->skipping = false;
	};
	if (!game.hud) { go(); return; }
	game.hud->fadeTo(1, 0.35);
	game.setTimeout(0.42, [this, go]() { go(); game.setTimeout(0.3, [this]() { if (game.hud) game.hud->fadeTo(0, 0.7); }); });
}

void TaxiSystem::arrive() {
	Ride& r = *ride;
	charge(r.fare, "Fare");
	if (game.hud) game.hud->help("That's $" + std::to_string((int)JsRound(r.fare)) + ". Thanks for riding with Los Soles Cabs!", 4);
	r.state = "leaving";
	game.vehicles.exit(&*game.player);
}

void TaxiSystem::endRide() {
	if (ride && !game.missionActive && game.hudModel && game.hudModel->gpsTarget && IsSet(ride->tx) && game.hudModel->gpsTarget->x == ride->tx && game.hudModel->gpsTarget->z == ride->tz)
		game.hudModel->routeTo(std::nullopt);
	if (game.hud) game.hud->setCounter("", "");
	ride.reset();
}

void TaxiSystem::charge(double amount, const char* label) {
	Player& p = *game.player;
	const int a = Max(1, (int)JsRound(amount));
	const int paid = (int)Min(p.money, (double)a);
	p.money -= paid;
	if (game.hud) game.hud->moneyFlash(-paid);
	game.sound("cash");
	if (paid < a && game.hud) game.hud->help(std::string(label) + ": $" + std::to_string(a) + ". You're $" + std::to_string(a - paid) + " short \xE2\x80\x94 the driver lets it slide, this time.", 4);
	game.stats.taxiRides++;
}

void TaxiSystem::startJob(Vehicle* cab) {
	if (game.missionActive) { if (game.hud) game.hud->help("Finish what you're doing first.", 3); return; }
	job = std::make_unique<Job>();
	job->cab = Ref<Vehicle>(cab);
	if (game.hud) game.hud->help("On duty! Fares wait at the kerb (<b style=\"color:#ffd23f\">yellow</b> blips). Stop next to them, then get them there before the meter clock runs out. Press <b>J</b> to go off duty.", 8);
	game.sound("checkpoint");
}

void TaxiSystem::endJob(const char* msg) {
	if (!job) return;
	if (job->fare) { dropFare(*job->fare, true); job->fare.reset(); }
	if (game.hud) { game.hud->setTimer(NaN()); game.hud->setCounter("", ""); }
	if (!game.missionActive && game.hud) game.hud->routeTo(std::nullopt);
	if (msg && game.hud) {
		Job& j = *job;
		game.hud->help(std::string(msg) + (j.fares ? " " + std::to_string(j.fares) + " fare" + (j.fares > 1 ? "s" : "") + ", $" + std::to_string((int)j.earned) + " earned." : ""), 5);
	}
	job.reset();
}

void TaxiSystem::updateJob(double dt) {
	Player& p = *game.player;
	Job& j = *job;
	Vehicle* cab = j.cab.get();
	if (p.vehicle != cab || p.seat != 0 || !cab || cab->isWrecked() || game.missionActive || p.dead) {
		if (!game.vehicles.isBusy(&p) || !cab || cab->isWrecked() || p.dead) endJob(!cab || cab->isWrecked() ? "Your cab is wrecked \xE2\x80\x94 shift over." : "Off duty.");
		return;
	}
	if (game.hud) game.hud->setCounter("FARES", std::to_string(j.fares) + " \xC2\xB7 $" + std::to_string((int)j.earned));
	if (!j.fare) {
		j.next -= dt;
		if (j.next <= 0) { j.fare.reset(spawnFare()); if (!j.fare) j.next = 3; }
		return;
	}
	Fare& f = *j.fare;
	Ped* q = f.ped.get();
	if (!q || q->dead || q->removed) { if (game.hud) game.hud->help("Your fare is... no longer with us.", 3); dropFare(f, false); j.fare.reset(); j.next = 4; j.streak = 0; return; }
	if (f.stage == "waiting") {
		const double d = Hypot(q->pos.x - cab->pos.x, q->pos.z - cab->pos.z);
		f.age += dt;
		if (d < 10 && cab->speedAbs() < 1.6 && !game.vehicles.isBusy(q)) {
			const int seat = rearSeat(cab, q->pos);
			if (seat >= 0) { game.vehicles.enter(q, cab, seat, true); f.stage = "boarding"; q->say(RandPick(JOB_HELLO)); }
		} else if (d < 60 && (int)std::floor(f.age * 0.5) != (int)std::floor((f.age - dt) * 0.5)) { if (q->anim) q->anim->play("wave"); }
		if (d > 900 || f.age > 240) { dropFare(f, true); j.fare.reset(); j.next = 1; }
	} else if (f.stage == "boarding") {
		if (q->vehicle == cab && !game.vehicles.isBusy(q)) startFareTrip(f);
		else if (!game.vehicles.isBusy(q) && !q->vehicle) f.stage = "waiting";
	} else if (f.stage == "riding") {
		f.timer -= dt;
		if (game.hud) game.hud->setTimer(f.timer);
		const double d = Hypot(f.tx - cab->pos.x, f.tz - cab->pos.z);
		if (f.marker) f.marker->visible = d < 250;
		if (d < 15 && cab->speedAbs() < 1.6) {
			const double left = Max(0, f.timer);
			const int pay = (int)JsRound(10 + f.km * 22 + left * 0.5);
			j.fares++; j.streak++;
			int bonus = 0;
			if (j.streak % 5 == 0) bonus = 50 * (j.streak / 5);
			p.money += pay + bonus; j.earned += pay + bonus;
			if (game.hud) game.hud->moneyFlash(pay + bonus);
			game.sound("cash");
			q->say(RandPick(left > f.limit * 0.35 ? JOB_FAST : JOB_BYE));
			if (game.hud) game.hud->help(bonus ? "<b>" + std::to_string(j.streak) + " in a row!</b> Fare $" + std::to_string(pay) + " + $" + std::to_string(bonus) + " bonus." : "Fare paid: $" + std::to_string(pay) + (left > f.limit * 0.35 ? " (with a tip for the speed)" : "") + ".", 3);
			fareOut(f);
		} else if (f.timer <= 0 && !f.late) {
			f.late = true; j.streak = 0;
			q->say(RandPick(JOB_SLOW));
			if (game.hud) game.hud->help("Too slow \xE2\x80\x94 your fare wants out. Stop the cab.", 4);
		}
		if (f.late && cab->speedAbs() < 1.6) fareOut(f);
	} else if (f.stage == "leaving") {
		if (!q->vehicle && !game.vehicles.isBusy(q)) { dropFare(f, false); j.fare.reset(); j.next = Rand(2, 5); }
	}
}

TaxiSystem::Fare* TaxiSystem::spawnFare() {
	Job& j = *job;
	Traffic::Sample smp;
	if (!Traffic::SampleLane(game, j.cab->pos.x, j.cab->pos.z, 110, 380, smp, [](const REdge& e) { return e.type != ERoad::Freeway && e.type != ERoad::Ramp && e.T->cls < 2; })) return nullptr;
	RoadSpot sp;
	if (!FindRoadSpot(game, smp.x, smp.z, true, sp)) return nullptr;
	PedOpts o; o.persistent = true; o.hasY = true; o.y = sp.y;
	Ped* ped = game.peds ? game.peds->spawnPed(sp.x, sp.z, o) : nullptr;
	if (!ped) return nullptr;
	ped->setState("guard");
	ped->hasGuardFace = true; ped->guardFace = sp.yaw - kPi / 2;
	ped->taxiFare = true;
	MarkerOpts mo; mo.hasY = true; mo.y = sp.y; mo.color = 0xffd23f; mo.radius = 2.4; mo.icon = "person"; mo.label = "Fare"; mo.arrow = true; mo.onEnter = nullptr;
	Marker* m = game.pickupsSys->addMarker(sp.x, sp.z, mo);
	if (game.hud) game.hud->routeTo(V2{ sp.x, sp.z });
	auto* f = new Fare();
	f->ped = Ref<Ped>(ped); f->marker = m; f->stage = "waiting"; f->age = 0; f->px = sp.x; f->pz = sp.z;
	return f;
}

void TaxiSystem::startFareTrip(Fare& f) {
	Job& j = *job;
	Vehicle* cab = j.cab.get();
	if (f.marker) { game.pickupsSys->removeMarker(f.marker); f.marker = nullptr; }
	std::vector<FreeDestination> opts;
	for (const auto& d : destinations()) {
		const double k = Hypot(d.x - cab->pos.x, d.z - cab->pos.z);
		if (k > 400 && k < 2000 && !d.platform) opts.push_back(d);
	}
	std::string name; double dx, dz;
	if (!opts.empty()) { const FreeDestination& d = opts[(size_t)std::floor(Rand() * opts.size())]; name = d.name; dx = d.x; dz = d.z; }
	else {
		Traffic::Sample s;
		if (Traffic::SampleLane(game, cab->pos.x, cab->pos.z, 400, 1200, s)) { name = game.map.ZoneName(s.x, s.z); dx = s.x; dz = s.z; }
		else { const Landmark& h = game.map.landmarks.at("home"); name = "the safehouse"; dx = h.x; dz = h.z; }
	}
	RoadSpot target;
	if (!FindRoadSpot(game, dx, dz, false, target)) target = { dx, dz, 0, 0 };
	const double km = Hypot(target.x - cab->pos.x, target.z - cab->pos.z) / 1000;
	f.stage = "riding"; f.destName = name; f.tx = target.x; f.tz = target.z; f.ty = target.y; f.km = km;
	f.limit = f.timer = JsRound(km * 1000 * 1.35 / 13 + 25);
	MarkerOpts mo; mo.hasY = true; mo.y = target.y; mo.color = 0xffd23f; mo.radius = 4; mo.height = 2.5; mo.icon = "flag"; mo.label = name; mo.vehicleOnly = true; mo.arrow = false;
	f.marker = game.pickupsSys->addMarker(target.x, target.z, mo);
	if (game.hud) game.hud->routeTo(V2{ target.x, target.z });
	if (game.hud) game.hud->help(RandPick(std::vector<std::string>{ "Take me to", "I need to get to", "Can you get me to", "Head for" }) + " " + name + RandPick(std::vector<std::string>{ ".", ", and step on it!", ", please.", " \xE2\x80\x94 I'm late!" }), 5);
}

void TaxiSystem::fareOut(Fare& f) {
	f.stage = "leaving";
	if (game.hud) game.hud->setTimer(NaN());
	if (f.marker) { game.pickupsSys->removeMarker(f.marker); f.marker = nullptr; }
	if (Ped* q = f.ped.get()) if (q->vehicle) game.vehicles.exit(q);
	if (!game.missionActive && game.hud) game.hud->routeTo(std::nullopt);
}

void TaxiSystem::dropFare(Fare& f, bool despawn) {
	if (f.marker) { game.pickupsSys->removeMarker(f.marker); f.marker = nullptr; }
	if (game.hud) game.hud->setTimer(NaN());
	if (!game.missionActive && game.hud) game.hud->routeTo(std::nullopt);
	Ped* ped = f.ped.get();
	if (!ped || ped->removed) return;
	if (despawn && !ped->vehicle && !ped->dead) { game.peds->remove(ped); return; }
	if (ped->vehicle && !game.vehicles.isBusy(ped)) game.vehicles.exit(ped);
	ped->persistent = false;
	ped->taxiFare = false;
	if (!ped->dead) ped->setState("wander");
}

void TaxiSystem::reset() {
	if (hail) {
		Hail& h = *hail;
		if (h.blip) game.removeBlip(h.blip);
		if (Vehicle* cab = h.cab.get()) releaseCab(cab);
		hail.reset();
	}
	if (ride) {
		if (Vehicle* cab = ride->cab.get()) releaseCab(cab);
		ride.reset();
	}
	if (job) endJob(nullptr);
	held = { Ref<Vehicle>(), 0 };
	jobHinted = false;
	tpGroup.clear();
	jobGroup.clear();
}

} // namespace atg