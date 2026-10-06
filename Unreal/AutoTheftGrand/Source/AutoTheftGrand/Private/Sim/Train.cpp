#include "Train.h"
#include "Character.h"
#include "Game.h"
#include "Input.h"
#include "RoadLayout.h"
#include "Vehicles.h"

namespace atg {

std::vector<RailStop> RailStops(const CityMap& map) {
	std::vector<RailStop> out;
	if (!map.roadInfo.hasRail) return out;
	const auto& rs = map.roadInfo.rail.stations;
	for (size_t i = 0; i < map.stations.size() && i < rs.size(); i++) {
		const Landmark& lm = map.stations[i];
		RailStop st;
		st.key = rs[i].key; st.name = lm.name;
		auto s = lm.vals.find("s"); st.s = s != lm.vals.end() ? s->second : rs[i].s;
		auto r = lm.vals.find("rot"); st.rot = r != lm.vals.end() ? r->second : rs[i].yaw;
		st.x = lm.x; st.z = lm.z; st.y = lm.y;
		out.push_back(st);
	}
	return out;
}

namespace {
// a carriage's stand-in for the vehicle manager's contact code: heavy, unbreakable, never in the list
class TrainProxy : public Vehicle {
public:
	TrainProxy(Game& g, const std::string& type, double len) : Vehicle(g, type, 0, 0, 0, SpawnOpts()) {
		proxy = true;
		hx = 1.5; hz = len / 2;
		mass = 2e6; I = 1e9;
		r = 0;
	}
	void damage(double, Character*) override {}
	void dent(double, double, double, double) override {}
	void explode() override {}
	void update(double) override {}
};
}

Train::Train(Game& g, const std::string& type, double x, double z, double yw, const TrainOpts& opts) : Vehicle(g, type, x, z, yw, opts) {
	rail = &game.map.roadInfo.rail;
	const double L = rail->length;
	freight = def.freight;
	track = opts.track >= 0 ? opts.track : (freight ? 1 : 0);
	cruise = freight ? 17 : 24;
	if (freight) stopKeys = { "dry", "union" };
	stops = RailStops(game.map);
	// consist: passenger carriages, or a string of freight wagons
	std::vector<std::pair<std::string, double>> kinds;
	if (freight) {
		const int n = opts.wagons >= 0 ? opts.wagons : 6;
		static const char* TYPES[8] = { "box", "tank", "flat", "hopper", "flat", "box", "tank", "flat" };
		for (int k = 0; k < n; k++) kinds.push_back({ TYPES[(k + (int)opts.seed) % 8], WAG_L });
	} else for (int k = 0; k < TRAIN_NCARS; k++) kinds.push_back({ "coach", CAR_L });
	len = LOCO_L;
	for (const auto& k : kinds) len += k.second + TRAIN_GAP;
	len += TRAIN_GAP;
	s = Clamp(IsSet(opts.s) ? opts.s : L * 0.5, len, L - 1);
	v = 0;
	dirS = IsSet(opts.dirS) ? opts.dirS : -1;
	dwell = IsSet(opts.dwell) ? opts.dwell : 12;
	hold = false;
	hx = 1.5; hz = LOCO_L / 2;
	I = mass * (LOCO_L * LOCO_L) / 12;
	maxHealth = health = 1e9;
	locoModel = &LocoModel(color);
	layout.seats.assign(4, V3(locoModel->seat[0], locoModel->seat[1], locoModel->seat[2]));
	layout.doorPos = V3(locoModel->doorPos[0], locoModel->doorPos[1], locoModel->doorPos[2]);
	for (size_t i = 0; i < kinds.size(); i++) {
		Car c;
		c.type = kinds[i].first; c.len = kinds[i].second; c.coach = c.type == "coach";
		c.seed = i * 3.7 + opts.seed;
		c.model = c.coach ? &CarriageModel(color) : &WagonModel(c.type, c.seed);
		cars.push_back(c);
		proxies.push_back(std::make_shared<TrainProxy>(game, type, c.len));
	}
	hornT = 0;
	place();
}

void Train::setup(const SpawnOpts&) {}

// vehicles.spawn("train" / "freight") makes a Train
static const bool GTrainFactory = [] {
	VehicleManager::Factories().push_back({ [](const VehicleDef& d) { return d.train; }, [](Game& g, const std::string& id, double x, double z, double yaw, const SpawnOpts& o) -> std::shared_ptr<Vehicle> {
		TrainOpts t; static_cast<SpawnOpts&>(t) = o;
		return std::make_shared<Train>(g, id, x, z, yaw, t);
	} });
	return true;
}();

M4 Train::groupMatrix() const { return M4::Compose(pos, Quat::FromEuler(-pitch, yaw, 0, "YXZ")); }
M4 Train::bodyMatrix() const { return groupMatrix(); }

void Train::playerControl(const Input& in, double) {
	input.throttle = in.throttle();
	input.brake = in.brake();
	horn = in.down("horn");
}

// --- doors: the cab (driver) and two doors per side on every carriage (passengers)
std::vector<Train::Door> Train::doorList() const {
	std::vector<Door> out;
	const double sn = std::sin(yaw), c = std::cos(yaw);
	for (int side : { -1, 1 }) out.push_back({ pos.x + side * 2.2 * c + (LOCO_L / 2 - 3.4) * sn, pos.z - side * 2.2 * sn + (LOCO_L / 2 - 3.4) * c, 0 });
	for (size_t k = 0; k < cars.size(); k++) {
		const Car& car = cars[k];
		if (!car.coach) continue;
		const double s2 = std::sin(car.yaw), cs = std::cos(car.yaw);
		for (int side : { -1, 1 }) for (double lz : { -CAR_L / 2 + 2.6, CAR_L / 2 - 2.6 })
			out.push_back({ car.pos.x + side * 2.3 * cs + lz * s2, car.pos.z - side * 2.3 * s2 + lz * cs, 1 + (int)Min(2, (double)k) });
	}
	return out;
}

V3 Train::nearestDoor(const V3& p, int& seat) const {
	double bd = kInf;
	hasDoor = false;
	for (const Door& d : doorList()) { const double dd = Hypot(d.x - p.x, d.z - p.z); if (dd < bd) { bd = dd; door = d; hasDoor = true; } }
	seat = hasDoor ? door.seat : 0;
	return hasDoor ? V3(door.x, 0, door.z) : pos;
}

bool Train::doorFor(int seat, Character* c, V3& out) const {
	Door d{};
	bool has = false;
	if (c) { int sk; nearestDoor(c->pos, sk); d = door; has = hasDoor; }
	else { d = door; has = hasDoor; }
	// on the way out, prefer the platform side when stopped at a station
	if (!c && atStation >= 0) {
		const RailStop& lm = stops[atStation];
		const Door* best = nullptr; double bd = kInf;
		const std::vector<Door> list = doorList();
		for (const Door& q : list) if ((q.seat == 0) == (seat == 0)) { const double dd = Hypot(q.x - lm.x, q.z - lm.z) + (q.seat == seat ? 0 : 30); if (dd < bd) { bd = dd; best = &q; } }
		if (best) { out = V3(best->x, 0, best->z); return true; }
	}
	if (!has) d = doorList()[0];
	out = V3(d.x, 0, d.z);
	return true;
}

void Train::putIn(Character* c, int seat) { Vehicle::putIn(c, seat); c->hiddenInVehicle = true; c->visible = false; }

void Train::takeOut(Character* c, const V3* at) {
	V3 p;
	if (at) p = *at; else doorFor(c->seat >= 0 ? c->seat : 1, nullptr, p);
	Vehicle::takeOut(c, &p);
	c->hiddenInVehicle = false; c->visible = true;
}

void Train::update(double dt) {
	if (removed) return;
	const double L = rail->length;
	Character* drv = driver();
	double accel = 0;
	// (online, a train follows the host's timetable: that comes with multiplayer)
	if (drv && drv->isPlayer) {
		const VehInput& inp = input;
		if (inp.throttle > 0) accel = 0.9 * inp.throttle * (v < -0.2 ? 2.2 : 1);
		if (inp.brake > 0) accel = v > 0.2 ? -2.2 * inp.brake : -0.6 * inp.brake;
		if (!inp.throttle && !inp.brake) accel = -Sgn(v) * Min(std::fabs(v) / dt, 0.05);
		atStation = -1;
		dwell = 0;
	} else accel = autopilot(dt);
	// the cab faces +s: the player can only back up slowly, the autopilot runs push-pull at full speed
	v = Clamp(v + accel * dt, drv && drv->isPlayer ? -12 : -32, 32);
	// buffer stops at both ends
	const double sMax = L - 2, sMin = len + 2;
	s += v * dt;
	if (s > sMax) { s = sMax; if (v > 0) { if (v > 6) game.rig.addShake(0.6); v = 0; } }
	if (s < sMin) { s = sMin; if (v < 0) { if (v < -6) game.rig.addShake(0.6); v = 0; } }
	place();
	contacts(dt);
	wheelRot += v * dt / 0.46;
	// lights: on at night or while moving
	const bool night = game.env.night > 0.3;
	const bool on = night || std::fabs(v) > 0.5;
	headOn = on; tailOn = night;
	lightsOn = on && drv && drv->isPlayer;
	hornT -= dt;
	if (horn && hornT <= 0) { hornT = 1.2; game.soundAt("trainhorn", pos, 1); }
}

// Station-to-station timetable: brake to stop with the train centred on the platform, dwell, carry on.
double Train::autopilot(double dt) {
	std::vector<int> sts;
	for (int i = 0; i < (int)stops.size(); i++) if (stopKeys.empty() || std::find(stopKeys.begin(), stopKeys.end(), stops[i].key) != stopKeys.end()) sts.push_back(i);
	if (sts.empty()) return 0;
	const double c = centreS();
	if (dwell > 0) {
		dwell -= dt;
		// the line ahead isn't clear yet: wait at the platform
		if (dwell <= 0 && hold && !reversing(sts, c)) dwell = 0.5;
		if (dwell <= 0) {
			lastStation = atStation;
			atStation = -1;
			bool any = false;
			for (int i : sts) if (i != lastStation && (stops[i].s - c) * dirS > 5) { any = true; break; }
			if (!any) dirS = -dirS;
			game.soundAt("trainhorn", pos, 0.7);
		}
		return -Sgn(v) * Min(std::fabs(v) / Max(dt, 1e-3), 3);
	}
	// next stop: the nearest station ahead that we didn't just leave (a few metres of overshoot still counts)
	std::vector<int> ahead;
	for (int i : sts) if (i != lastStation && (stops[i].s - c) * dirS > -4) ahead.push_back(i);
	std::stable_sort(ahead.begin(), ahead.end(), [&](int i, int j) { return std::fabs(stops[i].s - c) < std::fabs(stops[j].s - c); });
	if (ahead.empty()) { dirS = -dirS; lastStation = -1; return 0; }
	const int target = ahead[0];
	double signed_ = (stops[target].s - c) * dirS; // distance still to go (negative = overshot)
	const double vDir = v * dirS;
	// a signal short of the station: stop at it and wait for the line to clear
	const double lead = dirS > 0 ? s : s - len;
	const double toSig = dirS > 0 ? limitHi - lead : lead - limitLo;
	if (toSig < signed_) {
		if (toSig < 1.5 && std::fabs(v) < 1.2) { v = 0; return 0; }
		signed_ = Max(0, toSig);
	} else if (std::fabs(signed_) < 2 && std::fabs(v) < 1.6) {
		v = 0; dwell = 16; atStation = target; lastStation = target;
		game.events.trainArrived.emit(this, target);
		return 0;
	}
	// speed along the braking curve (0.8 m/s2), proportional control toward it (a freight train brakes later)
	const double vTarget = signed_ > 0 ? Min(cruise, std::sqrt(2 * 0.8 * Max(0, signed_ - 0.3))) : -Min(2, std::sqrt(-signed_));
	const double acc = Clamp((vTarget - vDir) * 1.4, -1.6, 0.7);
	return acc * dirS;
}

// at a terminus the next move is back the way we came (no station further on)
bool Train::reversing(const std::vector<int>& sts, double c) const {
	for (int i : sts) if (i != atStation && (stops[i].s - c) * dirS > 5) return false;
	return true;
}

void Train::place() {
	const RailInfo& R = *rail;
	const int tr = track;
	// locomotive: its centre is half a loco length behind the nose (+s is the nose direction)
	const RailPoint t = RailAtTrack(R, s - LOCO_L / 2, tr);
	const double yF = RailAtTrack(R, s - 2, tr).y, yB = RailAtTrack(R, s - LOCO_L + 2, tr).y;
	pos.set(t.x, t.y + 0.36, t.z);
	yaw = std::atan2(t.tx, t.tz);
	pitch = std::atan2(yF - yB, LOCO_L - 4);
	vel.set(t.tx * v, 0, t.tz * v);
	r = 0;
	double ss = s - LOCO_L - TRAIN_GAP;
	for (size_t k = 0; k < cars.size(); k++) {
		Car& car = cars[k];
		const double CL = car.len;
		const double sc = ss - CL / 2;
		const RailPoint q = RailAtTrack(R, sc, tr);
		const double y0 = RailAtTrack(R, sc + CL / 2 - 3, tr).y, y1 = RailAtTrack(R, sc - CL / 2 + 3, tr).y;
		car.pos.set(q.x, q.y + 0.36, q.z);
		car.yaw = std::atan2(q.tx, q.tz);
		car.pitch = std::atan2(y0 - y1, CL - 6);
		Vehicle& px = *proxies[k];
		px.pos = car.pos; px.yaw = car.yaw; px.vel.set(q.tx * v, 0, q.tz * v); px.r = 0;
		ss -= CL + TRAIN_GAP;
	}
}

// carriages vs cars and people (the locomotive itself goes through the vehicle manager)
void Train::contacts(double) {
	VehicleManager& vm = game.vehicles;
	const std::vector<Character*> chars = game.allCharacters();
	for (size_t k = 0; k < proxies.size(); k++) {
		Vehicle* px = proxies[k].get();
		const std::vector<std::shared_ptr<Vehicle>> list = vm.list;
		for (const auto& vp : list) {
			Vehicle* o = vp.get();
			if (o == this || o->removed) continue;
			const double dx = o->pos.x - px->pos.x, dz = o->pos.z - px->pos.z;
			if (dx * dx + dz * dz > 30 * 30 || std::fabs(o->pos.y - px->pos.y) > 3) continue;
			vm.carCar(px, o);
		}
		for (Character* c : chars) {
			if (c->vehicle || c->removed || c->ragdolling) continue;
			const double dx = c->pos.x - px->pos.x, dz = c->pos.z - px->pos.z;
			if (dx * dx + dz * dz > 13 * 13 || std::fabs(c->pos.y - px->pos.y) > 2.5) continue;
			vm.carPed(px, c);
		}
		cars[k].pos = px->pos; // (the stand-in is the carriage: a nudge moves it until the next place())
	}
}

} // namespace atg
