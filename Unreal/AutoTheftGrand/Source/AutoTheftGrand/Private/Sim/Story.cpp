#include "Story.h"
#include "Combat.h"
#include "Game.h"
#include "Hud.h"
#include "Police.h"

namespace atg {

Appearance StoryLook(const std::string& key) {
	Appearance a;
	if (key == "lou") { a.skin = 0x5f3a24; a.hairStyle = "cap"; a.hat = 0xf2b705; a.shirt = 0xf2b705; a.pants = 0x2b2b2b; a.shoes = 0xffffff; a.build = 1.22; a.height = 1.04; a.beard = true; a.jacketColor = 0x111111; }
	else if (key == "marisol") { a.female = true; a.skin = 0x8a5536; a.hair = 0x1a1a1a; a.hairStyle = "ponytail"; a.shirt = 0xf2f2f2; a.shirtType = "jacket"; a.jacketColor = 0xc9a227; a.pants = 0x1d3b5c; a.shoes = 0xeeeeee; a.build = 0.95; a.height = 0.96; }
	else if (key == "deacon") { a.skin = 0x3f2618; a.shirt = 0xf2b705; a.shirtType = "jacket"; a.jacketColor = 0x1a1a1a; a.pants = 0x222222; a.height = 1.02; a.glasses = true; }
	else if (key == "voss") { a.skin = 0xf1c7a5; a.hair = 0x4a2c16; a.shirt = 0xf2f2f2; a.shirtType = "jacket"; a.jacketColor = 0x5a4632; a.pants = 0x4a3b2a; a.shoes = 0x2b1a10; a.build = 1.1; a.height = 1.03; a.glasses = true; }
	else if (key == "ruiz") { a.skin = 0xc68863; a.hairStyle = "buzz"; a.shirt = 0x9aa7b5; a.shirtType = "jacket"; a.jacketColor = 0x3d405b; a.pants = 0x2b2d42; a.build = 1.12; a.beard = true; }
	else if (key == "rico") { a.skin = 0xa86b4a; a.hairStyle = "cap"; a.hat = 0x222222; a.shirt = 0x2a4d69; a.shirtType = "jacket"; a.jacketColor = 0xe07a1f; a.pants = 0x2a4d69; a.shoes = 0x333333; a.height = 0.98; }
	else if (key == "maddox") { a.skin = 0xf1c7a5; a.hair = 0xb08a52; a.hairStyle = "long"; a.shirt = 0xffffff; a.shirtType = "long"; a.pants = 0xe9d8a6; a.shoes = 0x8b5a2b; a.build = 0.98; a.glasses = true; a.beard = true; a.jacketColor = 0xffffff; }
	else if (key == "salazar") { a.skin = 0xc68863; a.hairStyle = "bald"; a.shirt = 0xc1121f; a.shirtType = "jacket"; a.jacketColor = 0x111111; a.pants = 0x111111; a.shoes = 0x5a0a0a; a.build = 1.15; a.height = 1.02; a.glasses = true; a.beard = true; }
	else if (key == "chino") { a.skin = 0xc68863; a.hairStyle = "buzz"; a.shirt = 0xc1121f; a.shirtType = "tank"; a.pants = 0x1d3557; a.shorts = true; a.shoes = 0xffffff; a.build = 1.05; a.glasses = true; a.bandana = 0xc1121f; a.jacketColor = 0x111111; }
	else if (key == "elseco") { a.skin = 0xc68863; a.hair = 0x999999; a.shirt = 0x7a1f1f; a.shirtType = "jacket"; a.jacketColor = 0xe9d8a6; a.pants = 0xe9d8a6; a.shoes = 0x8b5a2b; a.build = 1.12; a.glasses = true; a.beard = true; }
	else if (key == "benny") { a.skin = 0xe0ac87; a.shirt = 0x4a78b5; a.shirtType = "long"; a.pants = 0x6d5c43; a.shoes = 0x3b2f2a; a.build = 0.95; a.height = 0.97; a.glasses = true; a.jacketColor = 0x111111; }
	else if (key == "king") {
		// Evaluate the random appearance options before constructing it, as JavaScript does.
		RNG rng((uint32_t)(Rand() * 1e9));
		const std::string shirt = RandPick(std::vector<std::string>{ "tee", "tank", "jacket" }), hair = RandPick(std::vector<std::string>{ "cap", "buzz", "short" });
		const int64_t bandana = Rand() < 0.5 ? 0xf2b705 : -1;
		a = RandomAppearance(rng, 0); a.shirt = 0xf2b705; a.shirtType = shirt; a.jacketColor = 0x1a1a1a; a.hairStyle = hair; a.hat = 0xf2b705; a.bandana = bandana;
	}
	return a;
}

StoryRoadSpot StoryRoadNear(Game& game, double x, double z, int lane) {
	const RoadNet& net = game.map.roads; LaneStart st;
	if (NearestLane(net, x, z, 0, false, st, [](const REdge& e) { return e.hasGrid; })) {
		const auto& e = net.edges[st.e]; const auto pts = net.LanePath(e, st.dir, Min(lane, (st.dir == 0 ? e.lanesF : e.lanesB) - 1));
		int bi = 0; double bd = kInf;
		for (int i = 0; i < (int)pts.size() - 1; i++) { const double d = Dist2(pts[i].x, pts[i].z, x, z); if (d < bd) { bd = d; bi = i; } }
		if (pts.size() > 1) { bi = (int)Clamp(bi, 1, Max(1, (int)pts.size() - 3)); const auto& a = pts[bi]; const auto& b = pts[Min((int)pts.size() - 1, bi + 1)]; return { a.x, a.z, std::atan2(b.x - a.x, b.z - a.z) }; }
	}
	const int bi = game.map.NearestX(x), bj = game.map.NearestZ(z);
	if (std::fabs(XS[bi] - x) < std::fabs(ZS[bj] - z)) return { XS[bi] + 5.4 * (x > XS[bi] ? 1 : -1), Clamp(z, ZS.front() + HALF_ROAD + 6, ZS.back() - HALF_ROAD - 6), x > XS[bi] ? kPi : 0 };
	return { Clamp(x, XS.front() + HALF_ROAD + 6, XS.back() - HALF_ROAD - 6), ZS[bj] + 5.4 * (z > ZS[bj] ? 1 : -1), z > ZS[bj] ? kPi / 2 : -kPi / 2 };
}
void StoryTeleport(Game& game, double x, double z, double yaw, double y) {
	Player& p = *game.player;
	if (auto* v = p.vehicle) { v->pos.set(x, IsSet(y) ? y : game.map.GroundHeight(x, z), z); v->yaw = yaw; v->vel = V3(); v->r = 0; }
	else { if (IsSet(y)) p.setPosition(x, y, z); else p.setPosition(x, z); p.yaw = yaw; p.vel = V3(); }
	game.rig.yaw = yaw + kPi;
}
MissionTask FadeTeleport(MissionContext& m, double x, double z, double yaw, double y) {
	m.game.hud->fadeTo(1, 0.4); co_await m.wait(0.5); StoryTeleport(m.game, x, z, yaw, y);
	if (!IsSet(y)) { m.game.traffic->populate(10); m.game.peds->populate(10); }
	co_await m.wait(0.3); m.game.hud->fadeTo(0, 0.5);
}
Appearance GangLook(const std::string& gang) {
	const auto& g = Gangs().at(gang); RNG rng((uint32_t)(Rand() * 1e9));
	const std::string shirt = RandPick(std::vector<std::string>{ "tee", "tank", "jacket" }); const int64_t bandana = Rand() < 0.6 ? g.color : -1;
	const std::string hair = RandPick(std::vector<std::string>{ "cap", "buzz", "bald" });
	Appearance a = RandomAppearance(rng, 0); a.shirt = g.color; a.shirtType = shirt; a.jacketColor = 0x1a1a1a; a.bandana = bandana; a.hairStyle = hair; a.hat = g.color; return a;
}
std::vector<V3> StoryRing(double x, double z, int n, double radius, double phase) { std::vector<V3> out; for (int i = 0; i < n; i++) { const double a = phase + (double)i / n * kPi * 2; out.push_back(V3(x + std::cos(a) * radius, 0, z + std::sin(a) * radius)); } return out; }
std::vector<Ped*> StoryCrew(MissionContext& m, const std::string& gang, const std::vector<V3>& pts, const std::vector<std::string>& weapons, const MissionEnemyOpts& opts) {
	std::vector<Ped*> out;
	for (size_t i = 0; i < pts.size(); i++) { MissionEnemyOpts o = opts; o.gang = gang; o.weapon = weapons[i % weapons.size()]; o.hasAppearance = true; o.appearance = GangLook(gang); out.push_back(m.enemy(pts[i].x, pts[i].z, o)); }
	return out;
}
std::function<void()> AggroWhenNear(MissionContext& m, const std::vector<Ped*>& list, double range) {
	return m.tick([&m, list, range](double) {
		const V3 pp = m.player().vehicle ? m.player().vehicle->pos : m.player().pos;
		for (const auto* e : list) {
			if (e->dead || e->state == "attack") continue;
			if (Hypot(e->pos.x - pp.x, e->pos.z - pp.z) < range || e->health < e->maxHealth) { for (auto* o : list) if (!o->dead) { o->threat = &m.player(); o->setState("attack"); } break; }
		}
	});
}
std::function<void()> CrewSupport(MissionContext& m, std::vector<Ped*> crew, std::function<std::vector<Ped*>()> enemies) {
	return m.tick([&m, crew = std::move(crew), enemies = std::move(enemies), timer = 0.0](double dt) mutable {
		timer -= dt; if (timer > 0) return; timer = 0.45;
		const auto list = enemies();
		for (auto* c : crew) {
			if (c->dead) continue;
			Ped* best = nullptr; double bd = 35; const V3 cp = c->vehicle ? c->vehicle->pos : c->pos;
			for (auto* e : list) if (!e->dead && !e->removed) { const double d = Hypot(e->pos.x - cp.x, e->pos.z - cp.z); if (d < bd) { bd = d; best = e; } }
			if (!best) { c->threat = nullptr; c->aiming = false; continue; }
			if (!c->vehicle) { c->threat = best; continue; }
			if (Rand() < 0.6) {
				const V3 from = cp + V3(0, 1.3, 0), to = best->pos + V3(Rand(-0.6, 0.6), 1.2, Rand(-0.6, 0.6)), dir = (to - from).normalized();
				CombatHit hit; auto* combat = dynamic_cast<Combat*>(m.game.combat); const bool h = combat->raycast(from.x, from.y, from.z, dir.x, dir.y, dir.z, 60, c, hit);
				m.game.effects->tracer(from, h ? hit.point : from + dir * 60); m.game.effects->muzzleFlash(from, dir, false);
				if (h && hit.kind == CombatHit::Char && hit.ch->isPlayer) continue;
				WeaponDef wd; wd.id = "smg"; wd.damage = 22; if (h) combat->applyHit(hit, wd, &m.player(), dir);
				SoundOpts so; so.gun = true; m.game.soundAt("smg", from, 0.8, so);
			}
		}
	});
}

namespace {
class ChaseDriver : public RouteDriver {
public:
	ChaseDriver(Game& g, Vehicle* v, double speed = 30, double paceIn = 8) : RouteDriver(g, v, g.player->pos, RouteDriverOpts{ speed }), pace(paceIn) {}
	double pace, rev = 0;
	bool chooseNext(const LanePath& cur, Exit& out) override { dest = game.player->vehicle ? game.player->vehicle->pos : game.player->pos; route.reset(); return RouteDriver::chooseNext(cur, out); }
	void update(double dt) override {
		Vehicle& v = *veh; const V3 tp = game.player->vehicle ? game.player->vehicle->pos : game.player->pos;
		const double d = Hypot(tp.x - v.pos.x, tp.z - v.pos.z);
		if (d > 70) { dest = tp; cruise = 32; arrived = false; LaneDriver::update(dt); return; }
		double lx, lz; v.worldToLocal(tp.x, tp.z, lx, lz); const double ang = std::atan2(lx, Max(0.5, lz)); double steer = ang / (v.def.steer * 0.7);
		auto probe = [&](double a) { RayHit h; RayOpts o; o.ignoreProps = true; return game.collision->raycast(v.pos.x, v.pos.y + 0.8, v.pos.z, std::sin(v.yaw + a), 0, std::cos(v.yaw + a), 14, h, o) ? h.t : 14; };
		if (probe(0) < 8) steer += probe(0.4) > probe(-0.4) ? 1 : -1;
		v.input.steer = Clamp(steer, -1, 1);
		if (rev > 0) { rev -= dt; v.input.throttle = 0; v.input.brake = 1; v.input.steer = -v.input.steer; return; }
		if (std::fabs(v.speed()) < 1 && d > 10) { stuck += dt; if (stuck > 1.5) { rev = 1.2; stuck = 0; } } else stuck = 0;
		const double tv = game.player->vehicle ? game.player->vehicle->speedAbs() : 0, want = d < pace ? tv : tv + 10 + d * 0.3;
		v.input.throttle = v.speed() < want ? 1 : 0; v.input.brake = v.speed() > want + 5 ? 0.5 : 0; v.input.handbrake = std::fabs(ang) > 1.2 && v.speed() > 10;
	}
};
MissionPedOpts Cast(const std::string& who, bool invincible = false) { MissionPedOpts o; o.hasAppearance = true; o.appearance = StoryLook(who); o.invincible = invincible; return o; }
V3 LM(const Game& g, const std::string& key, double dx = 0, double dz = 0) { const auto& p = g.map.landmarks.at(key); return V3(p.x + dx, 0, p.z + dz); }
const P3& Point(const Game& g, const std::string& key, const std::string& point) { return g.map.landmarks.at(key).pts.at(point); }
}

MissionPedOpts StoryCast(const std::string& key, bool invincible) { return Cast(key, invincible); }
MissionTask StoryCountdown(MissionContext& m) {
	for (const auto& n : { "3", "2", "1" }) { m.game.hud->bigMessage(n, "hint", 0.9); m.game.sound("ui"); co_await m.wait(1); }
	m.game.hud->bigMessage("GO!", "passed", 1.2); m.game.sound("checkpoint");
}
V3 StoryLandmark(const Game& game, const std::string& key, double dx, double dz) { return LM(game, key, dx, dz); }
const P3& StoryPoint(const Game& game, const std::string& key, const std::string& point) { return Point(game, key, point); }

ChaseCar StoryChaseCar(MissionContext& m, const std::string& type, double x, double z, double yaw, const std::string& gang, int shooters, std::function<Appearance()> look) {
	SpawnOpts o; o.hasColor = true; o.color = look ? 0xc2a878 : gang == "vipers" ? 0x9d0208 : 0x1f7a8c;
	Vehicle* v = m.car(type, x, z, yaw, o);
	auto cast = [&]() { MissionPedOpts p; p.hasAppearance = true; p.appearance = look ? look() : GangLook(gang); return p; };
	Ped* drv = m.ped(x, z, cast()); v->putIn(drv); std::vector<Ped*> guns;
	for (int k = 0; k < shooters; k++) { Ped* s = m.ped(x, z, cast()); s->giveWeapon("smg", 999); s->equip("smg"); v->putIn(s, k + 1); m.driveBy(s); guns.push_back(s); }
	v->ai = std::make_shared<ChaseDriver>(m.game, v); m.blipEntity(v, 0xff3030, "car", true); return { v, drv, guns };
}

std::vector<MissionDef> BuildStory() {
	std::vector<MissionDef> story;
	MissionDef welcome; welcome.id = "welcome"; welcome.title = "Welcome Home"; welcome.contact = "V"; welcome.autoStart = true; welcome.reward = 100;
	welcome.log = "Dre came home for Tino's funeral. Detective Voss robbed him and dumped him in Viper turf.";
	welcome.start = [](const CityMap& map) { const auto& p = map.landmarks.at("plaza"); return V3(p.x, 0, p.z + 9); };
	welcome.run = [](MissionContext& m, Game& game) -> MissionTask {
		game.env.setTime(18.6); const V3 plaza = LM(game, "plaza"); const double px = plaza.x - 20, pz = plaza.z + 39;
		StoryTeleport(game, px, pz, 0); SpawnOpts locked; locked.locked = true;
		Vehicle* car = m.car("police", px + 4, pz + 5.2, -kPi / 2, locked);
		Ped* voss = m.ped(px - 1.6, pz + 0.8, Cast("voss", true)), *ruiz = m.ped(px + 1.7, pz + 0.6, Cast("ruiz", true));
		m.speakers = { {"Voss", voss}, {"Ruiz", ruiz}, {"Dre", &m.player()} };
		m.face(voss, &m.player()); m.face(ruiz, &m.player()); m.face(&m.player(), voss);
		co_await m.cutscene([&m, &game, px, pz, voss, ruiz]() -> MissionTask {
			m.shot(V3(px - 30, 18, pz - 40), V3(px, 2, pz), 50);
			game.hud->bigMessage("LOS SOLES, SAN MORENA", "chapter", 4, "Five years later"); co_await m.wait(3.5);
			m.twoShot(&m.player(), voss, 1, 3.4);
			co_await m.lines({ {"Voss", "Well, well. Andre Castillo. Five years back East and you come crawling home."}, {"Dre", "My brother's dead, Voss. I'm here for the funeral. That's it."} });
			m.overShoulder(&m.player(), ruiz); co_await m.say("Ruiz", "Funny thing about funerals. People get emotional. Do stupid things.");
			m.overShoulder(&m.player(), voss); co_await m.say("Voss", "Like carrying large amounts of cash. Which, as it happens, is evidence.");
			game.player->money = 0; m.overShoulder(voss, &m.player()); co_await m.say("Dre", "Evidence of what?");
			m.overShoulder(&m.player(), voss);
			co_await m.lines({ {"Voss", "Of whatever I say it is. Tino got mixed up with the wrong people. Don't make his mistakes."}, {"Ruiz", "Get in. We'll give you a lift. Scenic route."} });
		});
		game.hud->fadeTo(1, 0.5); co_await m.wait(0.7); voss->remove(); ruiz->remove(); car->remove();
		const V3 drop = LM(game, "vipers", -55, 55); StoryTeleport(game, drop.x, drop.z, kPi);
		Vehicle* cruiser = m.car("police", drop.x + 6, drop.z + 3, -kPi / 2, locked);
		Ped* v2 = m.ped(drop.x, drop.z, Cast("voss")), *r2 = m.ped(drop.x, drop.z, Cast("ruiz")); cruiser->putIn(v2); cruiser->putIn(r2, 1);
		co_await m.wait(0.4); game.hud->fadeTo(0, 0.8);
		co_await m.say("Voss", "Welcome home, Castillo. Vipers own this block now. Try not to get shot.", 4);
		RouteDriverOpts drive; drive.speed = 16; drive.ignoreLights = false; cruiser->ai = std::make_shared<RouteDriver>(game, cruiser, LM(game, "police"), drive);
		MissionEnemyOpts guards; guards.guard = true; guards.face = 0;
		const auto vip = StoryCrew(m, "vipers", StoryRing(drop.x - 30, drop.z - 20, 3, 3), { "bat", "fist", "fist" }, guards); AggroWhenNear(m, vip, 22);
		m.objective("You're in <span class=\"r\">Vipers</span> territory. Steal a car and get back to <span class=\"y\">Cedar Row</span>.");
		m.help("Walk up to any car and press <b>F</b> to steal it. <b>W/S</b> drive, <b>A/D</b> steer and <b>Space</b> is the handbrake \xE2\x80\x94 use it to drift.", 10);
		m.car("meridian", drop.x - 8, drop.z + 6, 0);
		const V3 home = LM(game, "home"); GoToOpts go; go.radius = 3.5; go.text = "Get back to <span class=\"y\">Cedar Row</span> \xE2\x80\x94 your family's house.";
		co_await m.goTo(home.x, home.z, go);
		if (m.player().vehicle) { UntilOpts o; o.timeout = 4; o.resolveTimeout = true; co_await m.until([&m]() { return !m.player().vehicle || m.player().vehicle->speedAbs() < 2; }, o); if (m.player().vehicle) game.vehicles.exit(&m.player()); co_await m.wait(1.2); }
		const auto door = Point(game, "home", "door"); Ped* mari = m.ped(door.x, door.z + 0.7, Cast("marisol", true));
		m.speakers = { {"Marisol", mari}, {"Dre", &m.player()} }; game.policeSys->clearWanted();
		co_await m.cutscene([&m, mari, home]() -> MissionTask {
			m.player().setPosition(home.x, home.z + 0.5); m.face(mari, &m.player()); m.face(&m.player(), mari); m.twoShot(&m.player(), mari, -1, 3.2);
			co_await m.lines({ {"Marisol", "Dre? Oh my god... Dre!"}, {"Dre", "Hey, Mari. You look... older."}, {"Marisol", "Five years will do that. You weren't here, Dre. You weren't here when they shot Tino."}, {"Dre", "I'm here now."} });
			m.overShoulder(&m.player(), mari);
			co_await m.lines({ {"Marisol", "Cedar Row isn't what it was. Vipers push their stuff on every corner and the cops just watch."}, {"Marisol", "Lou's been asking about you. He's always down at the court. Here \xE2\x80\x94 it's not much."}, {"Dre", "Big Lou. Good. Somebody's going to tell me what really happened to my little brother."} });
		});
		mari->missionKeep = true;
		m.help("Your home is a <b>safehouse</b>: walk into the green marker at the door to save. Yellow letters on the radar are story missions.", 9);
	}; story.push_back(std::move(welcome));

	MissionDef old; old.id = "oldfriends"; old.title = "Old Friends"; old.contact = "L"; old.requiresIds = { "welcome" }; old.reward = 300;
	old.log = "Reunited with Big Lou and Deacon. The Vipers tried to kill them outside Big Bun Burgers.";
	old.start = [](const CityMap& map) { const auto& p = map.landmarks.at("court"); return V3(p.x, 0, p.z + 12); };
	old.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 c = LM(game, "court", 0, 12); Ped* lou = m.ped(c.x - 2, c.z - 2, Cast("lou", true)), *deacon = m.ped(c.x + 2, c.z - 2.4, Cast("deacon", true));
		m.speakers = { {"Lou", lou}, {"Deacon", deacon}, {"Dre", &m.player()} };
		const auto road = StoryRoadNear(game, c.x, c.z + 30); SpawnOpts paint; paint.hasColor = true; paint.color = 0xf2b705; Vehicle* ride = m.car("bouncer", road.x, road.z, road.yaw, paint);
		co_await m.cutscene([&m, lou, deacon]() -> MissionTask {
			m.face(lou, &m.player()); m.face(deacon, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, 1, 4);
			co_await m.lines({ {"Lou", "Look what the cat dragged in! DRE! Come here, man!"}, {"Dre", "Big Lou. Deacon. Been a long time."}, {"Deacon", "Sorry about Tino, man. Kid was family to all of us."}, {"Lou", "C'mon, let's roll to Big Bun. Talk over some food, like old times. You drive \xE2\x80\x94 see if you remember how."} });
		});
		lou->invincible = deacon->invincible = false; m.follower(lou, 0); m.follower(deacon, 2); m.keepAlive(lou, "Lou died."); m.keepAlive(deacon, "Deacon died.");
		co_await m.getIn(ride, "Get in the <span class=\"b\">lowrider</span>.");
		UntilOpts boarding; boarding.timeout = 15; boarding.resolveTimeout = true; co_await m.until([lou, deacon, ride]() { return lou->vehicle == ride && deacon->vehicle == ride; }, boarding);
		m.help("Press <b>N</b> to change radio stations. <b>G</b> works the hydraulics on lowriders.", 7);
		game.setTimeout(3, [&game]() { game.hud->subtitle("Man, Vipers been getting bold. They hit Tino's corner three times this month.", "Deacon", 4); });
		const V3 burger = LM(game, "burger"); GoToOpts go; go.vehicle = true; go.radius = 5; go.text = "Drive to <span class=\"y\">Big Bun Burgers</span>.";
		co_await m.goTo(burger.x, burger.z - 9, go); co_await m.say("Lou", "Hold up... that red Brawler. That's Vipers! GET DOWN!", 3);
		const auto r2 = StoryRoadNear(game, burger.x - 60, burger.z); const auto chased = StoryChaseCar(m, "brawler", r2.x, r2.z, r2.yaw, "vipers", 2);
		game.missionMaxWanted = 0; m.objective("Take out the <span class=\"r\">Vipers</span> or lose them!");
		m.help("Shoot from the car: hold <b>right mouse</b> to aim and <b>left mouse</b> to fire (you need a pistol or SMG). Or just ram them.", 8);
		m.player().giveWeapon("pistol", 34); auto enemies = chased.guns; enemies.insert(enemies.begin(), chased.driver); CrewSupport(m, { lou, deacon }, [enemies]() { return enemies; });
		co_await m.until([&m, chased]() { return chased.v->isWrecked() || (chased.driver->dead && std::all_of(chased.guns.begin(), chased.guns.end(), [](const Ped* g) { return g->dead; })) || m.distTo(chased.driver) > 200; });
		game.missionMaxWanted = NaN(); co_await m.say("Lou", "Everybody breathing? Dre, get us back to the Row.", 3);
		const V3 home = LM(game, "home"); go.text = "Take Lou and Deacon back to <span class=\"y\">Cedar Row</span>."; co_await m.goTo(home.x, home.z + 14, go);
		co_await m.lines({ {"Lou", "Welcome back, homie. Just like old times, huh?"}, {"Deacon", "Yeah... just like old times."} });
		if (m.player().vehicle) game.vehicles.exit(lou); if (m.player().vehicle) game.vehicles.exit(deacon);
	}; story.push_back(std::move(old));

	MissionDef sweep; sweep.id = "cleansweep"; sweep.title = "Clean Sweep"; sweep.contact = "D"; sweep.requiresIds = { "oldfriends" }; sweep.reward = 400;
	sweep.log = "Beat three Viper dealers off the Cedar Row corner \xE2\x80\x94 no guns, Kings rules.";
	sweep.start = [](const CityMap& map) { const auto& p = map.landmarks.at("projects"); return V3(p.x + 10, 0, p.z); };
	sweep.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 s = LM(game, "projects", 10, 0); Ped* deacon = m.ped(s.x + 1.8, s.z - 1, Cast("deacon", true)); m.speakers = { {"Deacon", deacon}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, deacon]() -> MissionTask {
			m.face(deacon, &m.player()); m.face(&m.player(), deacon); m.twoShot(&m.player(), deacon, 1, 3.3);
			co_await m.lines({ {"Deacon", "Three Viper pushers slinging on our corner, out front of Ray's Liquor. Four blocks east."}, {"Deacon", "Kings don't use guns on our own streets. Show 'em some old-fashioned Cedar Row hospitality."}, {"Dre", "With my fists? Sounds like a Tuesday."} });
		});
		m.player().switchTo(m.player().weapons.count("bat") ? "bat" : "fist"); const V3 spot = LM(game, "liquor", 0, 3);
		MissionEnemyOpts guard; guard.guard = true; guard.health = 70; guard.face = kPi;
		const auto dealers = StoryCrew(m, "vipers", StoryRing(spot.x, spot.z, 3, 2.5), { "fist", "fist", "knife" }, guard); AggroWhenNear(m, dealers, 6);
		game.missionMaxWanted = 0;
		m.failIf([&m]() { return m.player().weaponDef().type != "melee" && m.player().aiming; }, "Kings rules: no guns on our own block!");
		std::weak_ptr<MissionContext> weak = m.shared_from_this();
		const int shotListener = game.events.gunshot.on([weak](Character* shooter, V3, const std::string&) { if (auto ctx = weak.lock()) if (shooter == &ctx->player()) ctx->reject(std::make_exception_ptr(MissionFail("Kings rules: no guns on our own block!"))); });
		m.onCleanup([&game, shotListener]() { game.events.gunshot.off(shotListener); });
		m.gps(spot.x, spot.z); m.objective("Go to <span class=\"y\">Ray's Liquor</span> on Cedar Row. Three Viper <span class=\"r\">dealers</span> are slinging out front.");
		m.help("Follow the purple GPS line on the radar. Targets have a <b style=\"color:#ff4a3a\">red arrow</b> over their heads.", 7);
		co_await m.until([&m, spot, dealers]() { return m.distTo(spot) < 28 || std::any_of(dealers.begin(), dealers.end(), [](const Ped* d) { return d->state == "attack"; }); });
		m.gpsOff(); m.help("Left mouse throws punches. Chain them into a <b>jab-cross-kick</b> combo. A bat hits harder.", 7);
		co_await m.killAll(dealers, "Beat down the <span class=\"r\">dealers</span>.", "DEALERS"); game.events.gunshot.off(shotListener); game.missionMaxWanted = NaN();
		co_await m.say("Deacon", "(phone) Heard it from here, man. That's how it's done. Tino would've been proud.", 4);
	}; story.push_back(std::move(sweep));

	MissionDef tools; tools.id = "toolingup"; tools.title = "Tooling Up"; tools.contact = "L"; tools.requiresIds = { "cleansweep" }; tools.reward = 500;
	tools.log = "Bought a gun at the Gun Barn and hit the Viper lot in El Corona."; tools.start = story[1].start;
	tools.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 c = LM(game, "court", 0, 12); Ped* lou = m.ped(c.x - 2, c.z - 2, Cast("lou", true)); m.speakers = { {"Lou", lou}, {"Dre", &m.player()} };
		co_await m.cutscene([&m, lou]() -> MissionTask {
			m.face(lou, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, -1, 3.3);
			co_await m.lines({ {"Lou", "Word is you put three Vipers in the hospital with your bare hands. Respect."}, {"Lou", "But the Vipers won't send dealers next time. They'll send shooters. Here's five hundred."}, {"Lou", "Gun Barn, Market District. Get yourself a piece. Then pay the Vipers a visit at their lot in El Corona."} });
		});
		m.cash(500); const V3 gs = LM(game, "gunshop"); m.gps(gs.x, gs.z);
		m.objective("Go to the <span class=\"r\">Gun Barn</span> in the Market District and buy a <b>pistol</b>.");
		m.help("Walk in and step up to the counter (the red marker) to shop. Buy the <b>9mm Pistol</b>.", 8);
		co_await m.until([&m]() { return m.player().weapons.count("pistol") != 0; }); m.gpsOff();
		m.help("Hold <b>right mouse</b> to aim, <b>left mouse</b> to shoot, <b>R</b> to reload. Headshots are deadly.", 9);
		const V3 v = LM(game, "vipers"); MissionEnemyOpts guard; guard.guard = true;
		const auto vip = StoryCrew(m, "vipers", StoryRing(v.x, v.z, 6, 9, 0.4), { "pistol", "bat", "pistol", "fist", "pistol", "knife" }, guard); AggroWhenNear(m, vip, 30);
		m.gps(v.x, v.z); co_await m.killAll(vip, "Take out the <span class=\"r\">Vipers</span> at their lot in El Corona.", "VIPERS"); m.gpsOff();
		co_await m.loseWanted("Lose the heat."); const V3 home = LM(game, "home"); GoToOpts go; go.radius = 5; go.text = "Head back to <span class=\"y\">Cedar Row</span>."; co_await m.goTo(home.x, home.z + 14, go);
	}; story.push_back(std::move(tools));

	MissionDef drive; drive.id = "driveby"; drive.title = "Drive-By"; drive.contact = "L"; drive.requiresIds = { "toolingup" }; drive.reward = 1000;
	drive.log = "Rode through El Corona with Lou and the Kings. The Vipers felt it."; drive.chapterEnd = { "CHAPTER II", "Streets on Fire" }; drive.start = story[1].start;
	drive.run = [](MissionContext& m, Game& game) -> MissionTask {
		const V3 c = LM(game, "court", 0, 12); Ped* lou = m.ped(c.x - 2, c.z - 2, Cast("lou", true)), *k1 = m.ped(c.x + 2, c.z - 2, Cast("king")), *k2 = m.ped(c.x + 3.5, c.z - 1, Cast("king"));
		for (auto* k : { lou, k1, k2 }) { k->giveWeapon("smg", 999); k->equip("smg"); }
		m.speakers = { {"Lou", lou}, {"Dre", &m.player()} }; const auto road = StoryRoadNear(game, c.x, c.z + 30); SpawnOpts paint; paint.hasColor = true; paint.color = 0x2a9d8f; Vehicle* ride = m.car("bouncer", road.x, road.z, road.yaw, paint);
		co_await m.cutscene([&m, lou]() -> MissionTask {
			m.face(lou, &m.player()); m.face(&m.player(), lou); m.twoShot(&m.player(), lou, 1, 4);
			co_await m.lines({ {"Lou", "Tino died in a drive-by. Nobody saw nothing, cops found nothing."}, {"Lou", "Time the Vipers learned what that feels like. You drive. Me and the boys handle the rest."}, {"Dre", "Let's ride."} });
		});
		lou->invincible = false; m.follower(lou, 0); m.follower(k1, 1); m.follower(k2, 2); m.keepAlive(lou, "Lou died.");
		co_await m.getIn(ride, "Get in the car."); UntilOpts boarding; boarding.timeout = 15; boarding.resolveTimeout = true;
		co_await m.until([lou, k1, k2, ride]() { for (auto* k : { lou, k1, k2 }) if (!k->dead && k->vehicle != ride) return false; return true; }, boarding);
		const V3 base = LM(game, "vipers"); const std::vector<V3> spots = { base + V3(-60, 0, -12), base + V3(55, 0, 12), base + V3(0, 0, 70) }; std::vector<Ped*> all;
		MissionEnemyOpts guard; guard.guard = true;
		for (const auto& s : spots) { const auto crew = StoryCrew(m, "vipers", StoryRing(s.x, s.z, 3, 2.2), { "pistol", "smg", "bat" }, guard); AggroWhenNear(m, crew, 25); all.insert(all.end(), crew.begin(), crew.end()); }
		CrewSupport(m, { lou, k1, k2 }, [all]() { return all; });
		m.help("Drive past the Viper crews slowly \xE2\x80\x94 the Kings will shoot. You can aim with <b>right mouse</b> and fire too.", 8);
		co_await m.killAll(all, "Hit the <span class=\"r\">Viper</span> crews in El Corona.", "VIPERS"); m.wanted(2); co_await m.loseWanted("The cops are onto you. Lose your wanted level.");
		const V3 home = LM(game, "home"); GoToOpts go; go.vehicle = true; go.radius = 5; go.text = "Take the crew back to <span class=\"y\">Cedar Row</span>."; co_await m.goTo(home.x, home.z + 14, go);
		co_await m.say("Lou", "That's for Tino. Tomorrow the whole city knows the Kings are back.", 4);
	}; story.push_back(std::move(drive));
	AddChapterTwo(story);
	AddChapterThree(story);
	AddChapterFour(story);
	AddChapterFive(story);
	return story;
}
} // namespace atg
