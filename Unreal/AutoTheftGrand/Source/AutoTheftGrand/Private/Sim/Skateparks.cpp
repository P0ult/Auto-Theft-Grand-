#include "Skateparks.h"
#include "Collision.h"
#include "Game.h"
#include "Peds.h"
#include "RoadLayout.h"
#include "Skateboard.h"
#include "Vehicles.h"

namespace atg {

namespace {
// the ramp layout in park space (x along the park, z across): wedges rise from a to b, tables are flat tops
struct SkRamp {
	enum Kind { Deck, Table, Ledge, Rail } kind;
	double ax = 0, az = 0, bx = 0, bz = 0, h0 = 0, h1 = 0, hw = 0; // (wedges)
	double x0 = 0, x1 = 0, z0 = 0, z1 = 0, h = 0, z = 0;          // (tables, ledges, the rail)
};
std::vector<SkRamp> SkLayout() {
	std::vector<SkRamp> R;
	auto wedge = [&](double ax, double az, double bx, double bz, double h0, double h1, double hw) { SkRamp r{ SkRamp::Deck }; r.ax = ax; r.az = az; r.bx = bx; r.bz = bz; r.h0 = h0; r.h1 = h1; r.hw = hw; R.push_back(r); };
	auto flat = [&](SkRamp::Kind k, double x0, double x1, double z0, double z1, double h) { SkRamp r{ k }; r.x0 = x0; r.x1 = x1; r.z0 = z0; r.z1 = z1; r.h = h; R.push_back(r); };
	// funbox: a 7 x 4 table at 0.7 with ramps both ends
	wedge(-6.1, 0, -3.5, 0, 0, 0.7, 2); wedge(-3.5, 0, 3.5, 0, 0.7, 0.7, 2); wedge(6.1, 0, 3.5, 0, 0, 0.7, 2);
	// jump line: kicker, gap, landing
	wedge(10, -6.5, 12.6, -6.5, 0, 0.9, 1.5); wedge(17.2, -6.5, 20.8, -6.5, 0.9, 0, 1.5);
	flat(SkRamp::Table, 12.6, 13.2, -8, -5, 0.9); flat(SkRamp::Table, 16.6, 17.2, -8, -5, 0.9);
	// pyramid: a 3 x 3 top at 0.6 with four ramps
	wedge(-22.2, 0, -20, 0, 0, 0.6, 1.5); wedge(-15.8, 0, -18, 0, 0, 0.6, 1.5);
	wedge(-19, -3.7, -19, -1.5, 0, 0.6, 1.5); wedge(-19, 3.7, -19, 1.5, 0, 0.6, 1.5);
	wedge(-20, 0, -18, 0, 0.6, 0.6, 1.5);
	// banks along the seaward side
	wedge(-10, 12.8, -10, 10.4, 1.1, 0, 5); wedge(8, 12.8, 8, 10.4, 1.1, 0, 5);
	// ledges and a rail (low: walk over them, bump a board)
	flat(SkRamp::Ledge, -9, -1, -10.6, -10.1, 0.45); flat(SkRamp::Ledge, 3, 9, -10.6, -10.1, 0.45);
	SkRamp rail{ SkRamp::Rail }; rail.x0 = -6; rail.x1 = 4; rail.z = 7.5; rail.h = 0.4; R.push_back(rail);
	return R;
}

// a local skating laps of the park: steer for the next point, keep a pace, ollie off the kickers
class SkaterAI : public VehicleAI {
public:
	SkaterAI(Game& g, Vehicle* v, const std::vector<std::array<double, 2>>& loop, int idx) : game(g), v(v), loop(loop), idx(idx) { pace = Rand(4.5, 6.5); olT = Rand(2, 6); }
	void update(double dt) override {
		const auto& tp = loop[idx];
		const double d = Hypot(tp[0] - v->pos.x, tp[1] - v->pos.z);
		if (d < 2.2) idx = (idx + 1) % (int)loop.size();
		double lx, lz; v->worldToLocal(tp[0], tp[1], lx, lz);
		const double ang = std::atan2(lx, Max(0.3, lz));
		VehInput& in = v->input;
		in.steer = Clamp(ang / (v->def.steer * 0.8), -1, 1);
		const double want = std::fabs(ang) > 0.8 ? pace * 0.6 : pace;
		in.throttle = v->speed() < want ? 1 : 0;
		in.brake = v->speed() > want + 1.5 ? 0.5 : 0;
		// tricks: ollie now and then, flick the board in the air
		olT -= dt;
		if (olT <= 0 && v->speed() > 3 && !v->airborne) {
			olT = Rand(3, 8);
			if (Skateboard* s = dynamic_cast<Skateboard*>(v)) s->ollie();
			if (Rand() < 0.6) { static const std::vector<double> Sides = { -1, 1 }; trick = RandPick(Sides); }
			else trick = 0;
			trickT = 0.1;
		}
		if (v->airborne && trickT > 0) { trickT -= dt; if (trickT <= 0 && trick != 0) in.steer = trick; }
		// someone in the way: slow up
		for (Character* c : game.allCharacters()) {
			if (c == v->driver() || c->vehicle) continue;
			double ox, oz; v->worldToLocal(c->pos.x, c->pos.z, ox, oz);
			if (oz > 0 && oz < 4 && std::fabs(ox) < 1) { in.throttle = 0; in.brake = 1; break; }
		}
	}
private:
	Game& game;
	Vehicle* v;
	std::vector<std::array<double, 2>> loop;
	int idx;
	double pace, olT, trick = 0, trickT = 0;
};
}

Skateparks::Skateparks(Game& g) : game(g) {
	for (const SkateparkDef& d : SKATEPARKS) {
		Park p; p.key = d.key; p.name = d.name; p.x = d.x; p.z = d.z; p.yaw = d.yaw; p.hx = d.hx; p.hz = d.hz; p.y = d.y;
		parks.push_back(p);
	}
	for (Park& p : parks) build(p);
}

void Skateparks::toWorld(const Park& p, double lx, double lz, double& x, double& z) const {
	const double s = std::sin(p.yaw), c = std::cos(p.yaw);
	x = p.x + lx * c + lz * s; z = p.z - lx * s + lz * c;
}

void Skateparks::build(Park& p) {
	CollisionWorld& col = *game.collision;
	Part conc, metal;
	const double y0 = p.y + 0.02;
	auto grey = [&](double k) { conc.color(0.66 * k, 0.64 * k, 0.6 * k); };
	for (const SkRamp& r : SkLayout()) {
		if (r.kind == SkRamp::Deck) {
			double ax, az, bx, bz; toWorld(p, r.ax, r.az, ax, az); toWorld(p, r.bx, r.bz, bx, bz);
			col.addDeck(ax, az, y0 + r.h0, bx, bz, y0 + r.h1, r.hw, r.hw, -1, false, true);
			// the mesh: the sloped (or flat) top and the sides down to the pad
			const double dx = bx - ax, dz = bz - az, L = Hypot(dx, dz), ux = dx / L, uz = dz / L, nx = -uz, nz = ux;
			auto P = [&](double e, double s, double h) { return Pt3{ ax + ux * e + nx * s, h, az + uz * e + nz * s }; };
			const double ya = y0 + r.h0, yb = y0 + r.h1;
			grey(1.05);
			conc.poly({ P(0, -r.hw, ya), P(L, -r.hw, yb), P(L, r.hw, yb), P(0, r.hw, ya) }, { 0, 1, 0 });
			grey(0.85);
			for (double sgn : { -1.0, 1.0 }) conc.poly({ P(0, sgn * r.hw, y0), P(L, sgn * r.hw, y0), P(L, sgn * r.hw, yb), P(0, sgn * r.hw, ya) }, { nx * sgn, 0, nz * sgn });
			for (const double e : { 0.0, L }) {
				const double h = e ? yb : ya;
				if (h > y0 + 0.02) conc.poly({ P(e, -r.hw, y0), P(e, r.hw, y0), P(e, r.hw, h), P(e, -r.hw, h) }, { ux * (e ? 1 : -1), 0, uz * (e ? 1 : -1) });
			}
			// steel coping along the lip of a kicker
			if (r.h1 > r.h0 + 0.3) { metal.color(0.75, 0.75, 0.78); metal.geo(Geo::Cylinder(0.035, 0.035, r.hw * 2, 8), Mat4::Compose(bx, yb, bz, 0, std::atan2(nx, nz), kPi / 2, 1, 1, 1, "YXZ")); }
		} else if (r.kind == SkRamp::Table || r.kind == SkRamp::Ledge) {
			double x0, z0, x1, z1; toWorld(p, r.x0, r.z0, x0, z0); toWorld(p, r.x1, r.z1, x1, z1);
			const double minX = Min(x0, x1), maxX = Max(x0, x1), minZ = Min(z0, z1), maxZ = Max(z0, z1);
			const bool ledge = r.kind == SkRamp::Ledge;
			grey(ledge ? 0.75 : 0.9);
			conc.box(minX, y0, minZ, maxX, y0 + r.h, maxZ);
			if (ledge) { metal.color(0.7, 0.7, 0.72); metal.box(minX, y0 + r.h - 0.02, minZ - 0.01, maxX, y0 + r.h + 0.01, minZ + 0.06); }
			if (!ledge) col.addDeck(minX, (minZ + maxZ) / 2, y0 + r.h, maxX, (minZ + maxZ) / 2, y0 + r.h, (maxZ - minZ) / 2, (maxZ - minZ) / 2, -1, false, true);
			else col.addBox(minX, y0 - 0.2, minZ, maxX, y0 + r.h, maxZ, "ledge");
		} else {
			double x0, z0, x1, z1; toWorld(p, r.x0, r.z, x0, z0); toWorld(p, r.x1, r.z, x1, z1);
			metal.color(0.8, 0.8, 0.82);
			const double len = Hypot(x1 - x0, z1 - z0), yaw = std::atan2(x1 - x0, z1 - z0);
			metal.geo(Geo::Cylinder(0.03, 0.03, len, 8), Mat4::Compose((x0 + x1) / 2, y0 + r.h, (z0 + z1) / 2, kPi / 2, yaw, 0, 1, 1, 1, "YXZ"));
			for (int k = 0; k <= 3; k++) { const double f = k / 3.0; metal.box(x0 + (x1 - x0) * f - 0.025, y0, z0 + (z1 - z0) * f - 0.025, x0 + (x1 - x0) * f + 0.025, y0 + r.h, z0 + (z1 - z0) * f + 0.025); }
		}
	}
	// a low wall round the landward side with painted panels, benches and floodlights
	auto wall = [&](double ax, double az, double bx, double bz) {
		double x0, z0, x1, z1; toWorld(p, ax, az, x0, z0); toWorld(p, bx, bz, x1, z1);
		grey(0.8);
		conc.box(Min(x0, x1) - 0.15, y0, Min(z0, z1) - 0.15, Max(x0, x1) + 0.15, y0 + 0.9, Max(z0, z1) + 0.15);
		col.addBox(Min(x0, x1) - 0.15, y0 - 0.2, Min(z0, z1) - 0.15, Max(x0, x1) + 0.15, y0 + 0.9, Max(z0, z1) + 0.15, "wall");
	};
	wall(-p.hx, -p.hz, -6, -p.hz); wall(6, -p.hz, p.hx, -p.hz); // (a gap in the middle to walk in)
	static const std::vector<Pt3> Tags = { { 0.9, 0.2, 0.3 }, { 0.2, 0.6, 0.9 }, { 0.95, 0.75, 0.1 }, { 0.3, 0.8, 0.4 }, { 0.7, 0.3, 0.9 } };
	for (int k = 0; k < 9; k++) {
		const double lx = -p.hx + 2 + k * 3.2 + (k > 3 ? 12.6 : 0);
		if (lx > p.hx - 2) break;
		double x, z; toWorld(p, lx, -p.hz + 0.16, x, z);
		const Pt3& c = RandPick(Tags);
		conc.color(c[0], c[1], c[2]);
		const double l = Rand(0.5, 1.2), rr = Rand(0.5, 1.2), top = Rand(0.5, 0.8);
		conc.box(x - l, y0 + 0.15, z, x + rr, y0 + top, z + 0.012);
	}
	for (double lx : { -26.0, 26.0 }) {
		double x, z; toWorld(p, lx, -p.hz + 1.2, x, z);
		metal.color(0.3, 0.3, 0.32);
		metal.geo(Geo::Cylinder(0.08, 0.1, 7, 8), Mat4::Compose(x, y0 + 3.5, z));
		metal.color(1, 1, 0.9); metal.box(x - 0.4, y0 + 6.9, z - 0.15, x + 0.4, y0 + 7.2, z + 0.15);
	}
	for (double lx : { -12.0, 14.0 }) {
		double x, z; toWorld(p, lx, -p.hz + 1.1, x, z);
		conc.color(0.45, 0.33, 0.22); conc.box(x - 1, y0 + 0.4, z - 0.25, x + 1, y0 + 0.47, z + 0.25);
		grey(0.7); conc.box(x - 0.9, y0, z - 0.15, x - 0.7, y0 + 0.4, z + 0.15); conc.box(x + 0.7, y0, z - 0.15, x + 0.9, y0 + 0.4, z + 0.15);
	}
	p.concrete = std::move(conc.m);
	p.metal = std::move(metal.m);
	// where boards lie, and the lap the locals skate
	const double spots[4][3] = { { -12, -p.hz + 2.2, 0.4 }, { 14, -p.hz + 2.4, 2.8 }, { -26, 6, 1.2 }, { 24, 6, -1.4 } };
	for (const auto& s : spots) { Spot sp; toWorld(p, s[0], s[1], sp.x, sp.z); sp.yaw = s[2] + p.yaw; p.boardSpots.push_back(sp); }
	const double loop[11][2] = { { -26, 0 }, { -19, 0 }, { -12, 0 }, { -6, 0 }, { 6, 0 }, { 12, -6.5 }, { 22, -6.5 }, { 26, 4 }, { 8, 7 }, { -10, 7 }, { -26, 6 } };
	for (const auto& q : loop) { double x, z; toWorld(p, q[0], q[1], x, z); p.loop.push_back({ x, z }); }
}

void Skateparks::update(double dt) {
	t -= dt;
	if (t > 0) return;
	t = 1;
	const V3 pl = game.player->vehicle ? game.player->vehicle->pos : game.player->pos;
	for (Park& p : parks) {
		const double d = Hypot(pl.x - p.x, pl.z - p.z);
		if (d < 140 && !p.active) activate(p);
		else if (d > 220 && p.active) deactivate(p);
		if (p.active) {
			// keep the locals going (and replace any who've wandered off or been knocked flying)
			std::vector<Skater> keep;
			for (const Skater& s : p.skaters) { Vehicle* v = s.v.get(); Ped* ped = s.ped.get(); if (v && ped && !v->removed && v->driver() == ped && !ped->dead) keep.push_back(s); }
			p.skaters = keep;
			if (p.skaters.size() < 2 && d > 25) spawnSkater(p);
		}
	}
}

void Skateparks::activate(Park& p) {
	p.active = true;
	for (const Spot& s : p.boardSpots) {
		bool taken = false;
		for (const auto& v : game.vehicles.list) if (Hypot(v->pos.x - s.x, v->pos.z - s.z) < 1.5) { taken = true; break; }
		if (taken) continue;
		SpawnOpts o; o.parked = true;
		Vehicle* b = game.vehicles.spawn("skateboard", s.x, s.z, s.yaw, o);
		if (!b) continue;
		b->persistent = true;
		p.boards.push_back(Ref<Vehicle>(b));
	}
	for (int i = 0; i < 2; i++) spawnSkater(p);
}

void Skateparks::spawnSkater(Park& p) {
	if (!game.peds) return;
	const int n = (int)p.loop.size();
	const int k = (int)std::floor(Rand() * n);
	const auto &a = p.loop[k], &b = p.loop[(k + 1) % n];
	const double yaw = std::atan2(b[0] - a[0], b[1] - a[1]);
	Vehicle* board = game.vehicles.spawn("skateboard", a[0], a[1], yaw);
	if (!board) return;
	board->persistent = true;
	PedOpts o; o.persistent = true;
	Ped* ped = game.peds->spawnPed(a[0], a[1], o);
	if (!ped) { game.vehicles.remove(board); return; }
	board->putIn(ped, 0);
	board->ai = std::make_shared<SkaterAI>(game, board, p.loop, (k + 1) % n);
	p.skaters.push_back({ Ref<Vehicle>(board), Ref<Ped>(ped) });
}

void Skateparks::deactivate(Park& p) {
	p.active = false;
	for (const auto& r : p.boards) if (Vehicle* b = r.get()) if (!b->removed && !b->driver()) game.vehicles.remove(b);
	for (const Skater& s : p.skaters) if (Vehicle* v = s.v.get()) if (!v->removed && v->driver() == s.ped.get()) game.vehicles.remove(v);
	p.boards.clear(); p.skaters.clear();
}

} // namespace atg
