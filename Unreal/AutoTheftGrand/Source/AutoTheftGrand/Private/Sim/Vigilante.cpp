#include "Vigilante.h"
#include "Combat.h"
#include "Game.h"
#include "Gameplay.h"
#include "NpcCrime.h"
#include "Peds.h"
#include "Races.h"
#include "Skeleton.h"
#include "Traffic.h"

namespace atg {

namespace {
const std::vector<std::string> CARS = { "kestrel", "brawler", "meridian", "summit", "bouncer", "pico" };
const std::vector<std::string> CRIMES = { "armed robbery", "a hit and run", "carjacking", "a jewellery heist", "assault on an officer", "a drive-by", "a bank job" };
std::string Thousands(double v) {
	std::string s = std::to_string((long long)JsRound(v)), out;
	for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) out += ','; out += s[i]; }
	return out;
}
}

Vigilante::Vigilante(Game& g) : game(g) {}

bool Vigilante::ctl() const { return game.input.enabled && !game.cutscene && !game.player->dead && game.gameplay && game.gameplay->state == "playing" && !game.menuOpen; }

void Vigilante::update(double dt) {
	Player& p = *game.player;
	Vehicle* v = p.vehicle;
	const bool inCop = v && v->def.police && v->def.kind != "boat" && p.seat == 0 && v->driver() == &p;
	if (inCop && ctl() && !game.missionActive && !(game.races && game.races->isActive())) {
		if (game.input.hit("taxiJob")) { if (active) stop("Off duty."); else start(); }
		else if (!active && !hinted) { hinted = true; if (game.hud) game.hud->help("You're in a police car. Press <b>J</b> to go on <b>vigilante</b> patrol.", 6); }
	}
	game.vigilanteActive = active;
	if (!active) return;
	if (game.missionActive) { stop(nullptr); return; }
	if (between > 0) { between -= dt; if (between <= 0) dispatch(); return; }
	clock -= dt;
	if (game.hud) game.hud->setTimer(Max(0, clock));
	std::vector<Suspect*> left;
	for (auto& s : suspects) { Ped* q = s.ped.get(); if (q && !q->dead && !q->removed) left.push_back(&s); }
	if (game.hud) game.hud->setCounter("SUSPECTS", std::to_string(total - (int)left.size()) + "/" + std::to_string(total));
	// out of the car too long, or out of time
	if (!p.vehicle || !p.vehicle->def.police) { outT += dt; if (outT > 60) { stop("You abandoned your patrol car."); return; } } else outT = 0;
	if (clock <= 0) { stop("The suspects got away.", true); return; }
	// shooters in the car (from level 3)
	for (Suspect* s : left) {
		Ped* q = s->ped.get();
		if (!q->vehicle || q->seat == 0 || level < 3) continue;
		s->fireT = (IsSet(s->fireT) ? s->fireT : Rand(1, 2)) - dt;
		const V3 pp = p.vehicle ? p.vehicle->pos : p.pos;
		const double d = Hypot(pp.x - q->vehicle->pos.x, pp.z - q->vehicle->pos.z);
		if (d < 40 && s->fireT <= 0) {
			s->fireT = Rand(0.25, 0.7);
			const V3 from = q->vehicle->pos + V3(0, 1.3, 0);
			const V3 to(pp.x + Rand(-1.8, 1.8), pp.y + 1, pp.z + Rand(-1.8, 1.8));
			const V3 dir = (to - from).normalized();
			CombatHit hit; auto* combat = dynamic_cast<Combat*>(game.combat);
			const bool h = combat && combat->raycast(from.x, from.y, from.z, dir.x, dir.y, dir.z, 60, q, hit);
			if (game.effects) { game.effects->tracer(from, h ? hit.point : from + dir * 60); game.effects->muzzleFlash(from, dir, false); }
			WeaponDef wd; wd.id = "pistol"; wd.damage = 8;
			if (h) combat->applyHit(hit, wd, q, dir);
			SoundOpts so; so.gun = true; game.soundAt("pistol", from, 0.8, so);
		}
	}
	// on foot (bailed out of a wreck): they fight
	for (Suspect* s : left) { Ped* q = s->ped.get(); if (!q->vehicle && q->state != "attack") { q->threat = Ref<Character>(&p); q->setState("attack"); } }
	// keep the blips on them
	for (auto& s : suspects) {
		if (!s.blip) continue;
		Ped* q = s.ped.get();
		if (!q || q->dead || q->removed) { game.removeBlip(s.blip); s.blip.reset(); continue; }
		const V3 at = q->vehicle ? q->vehicle->pos : q->pos;
		s.blip->x = at.x; s.blip->z = at.z;
	}
	if (left.empty()) cleared();
}

void Vigilante::start() {
	active = true;
	level = 0;
	earned = 0;
	between = 0;
	game.vigilanteActive = true;
	if (game.hud) game.hud->bigMessage("VIGILANTE", "title", 2.5, "Take down the suspects");
	dispatch();
}

void Vigilante::dispatch() {
	Player& p = *game.player;
	level++;
	const V3 pp = p.vehicle ? p.vehicle->pos : p.pos;
	Traffic::Sample smp;
	if (!Traffic::SampleLane(game, pp.x, pp.z, 220, 420, smp, [](const REdge& e) { return e.type != ERoad::Freeway && e.type != ERoad::Ramp && e.type != ERoad::Dirt; })
		&& !Traffic::SampleLane(game, pp.x, pp.z, 120, 300, smp)) { stop("No calls right now."); return; }
	const REdge& e = game.map.roads.edges[smp.start.e];
	const Line pts = game.map.roads.LanePath(e, smp.start.dir, smp.start.lane);
	if (pts.empty()) { stop("No calls right now."); return; }
	const P3 a = pts[Min((int)pts.size() - 1, 1)], b = pts[Min((int)pts.size() - 1, 2)];
	SpawnOpts so; so.hasY = true; so.y = smp.y; so.persistent = true;
	Vehicle* c = game.vehicles.spawn(RandPick(CARS), smp.x, smp.z, std::atan2(b.x - a.x, b.z - a.z), so);
	if (!c) { stop("No calls right now."); return; }
	c->health = c->maxHealth = 700 + level * 80;
	const int n = (int)Clamp(1 + std::floor(level / 2.0), 1, 4);
	suspects.clear();
	for (int k = 0; k < n; k++) {
		PedOpts po; po.persistent = true; po.brain = "gang"; po.gang = "vipers";
		po.weapon = RandPick(std::vector<std::string>{ "pistol", "smg", "pistol", "shotgun" });
		// randomAppearance(this.rng, { female, shirtType, bandana, hairStyle, glasses }): the options are drawn first
		const bool female = rng.Chance(0.15);
		const std::string shirt = RandPick(std::vector<std::string>{ "tank", "tee", "jacket" });
		const bool bandana = rng.Chance(0.5);
		const std::string hair = RandPick(std::vector<std::string>{ "cap", "buzz", "short" });
		const bool glasses = rng.Chance(0.4);
		po.hasAppearance = true; po.appearance = RandomAppearance(rng, female ? 1 : 0);
		po.appearance.shirtType = shirt; po.appearance.bandana = bandana ? 0x111111 : -1; po.appearance.hairStyle = hair; po.appearance.glasses = glasses;
		Ped* s = game.peds->spawnPed(smp.x, smp.z, po);
		if (!s) continue;
		s->criminal = true;
		s->accuracy = 0.3 + level * 0.03;
		c->putIn(s, k);
		Blip bl; bl.x = smp.x; bl.z = smp.z; bl.color = 0xff3030; bl.icon = "dot"; bl.small = k > 0;
		suspects.push_back({ Ref<Ped>(s), game.addBlip(bl), NaN() });
	}
	c->ai = std::make_shared<FleeDriver>(game, c, [this]() { Player& pl = *game.player; return pl.vehicle ? pl.vehicle->pos : pl.pos; });
	car = Ref<Vehicle>(c);
	total = n;
	clock = 100 + level * 12;
	if (game.hud) {
		game.hud->help("Dispatch: level " + std::to_string(level) + ". " + std::to_string(n) + " suspect" + (n > 1 ? "s" : "") + " wanted for " + RandPick(CRIMES) + ". Take them down.", 5);
		game.hud->routeTo(std::nullopt);
	}
}

void Vigilante::cleared() {
	Player& p = *game.player;
	const int pay = level * 500 + (int)JsRound(Max(0, clock) * 5);
	p.money += pay;
	earned += pay;
	if (game.hud) { game.hud->moneyFlash(pay); game.hud->bigMessage("SUSPECTS DOWN", "passed", 2.5, "$" + Thousands(pay)); }
	game.sound("cash");
	game.stats.vigilanteBest = Max(game.stats.vigilanteBest, level);
	if (Vehicle* c = car.get()) c->persistent = false;
	car.reset();
	suspects.clear();
	between = 3.5; // the next call comes in shortly
	if (game.hud) game.hud->setTimer(NaN());
}

void Vigilante::stop(const char* msg, bool failed) {
	if (!active) return;
	active = false;
	game.vigilanteActive = false;
	for (auto& s : suspects) {
		if (s.blip) game.removeBlip(s.blip);
		s.blip.reset();
		if (Ped* q = s.ped.get()) if (!q->dead) q->persistent = false;
	}
	if (Vehicle* c = car.get()) c->persistent = false;
	car.reset();
	suspects.clear();
	if (game.hud) {
		game.hud->setTimer(NaN()); game.hud->setCounter("", "");
		if (msg) game.hud->bigMessage(failed ? "VIGILANTE OVER" : "OFF DUTY", failed ? "failed" : "hint", 3, std::string(msg) + " Reached level " + std::to_string(level) + " \xC2\xB7 earned $" + Thousands(earned));
	}
}

} // namespace atg
