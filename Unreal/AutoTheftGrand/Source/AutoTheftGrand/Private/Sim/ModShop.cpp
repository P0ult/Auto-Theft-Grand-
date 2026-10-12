#include "ModShop.h"
#include "Effects.h"
#include "Game.h"
#include "Phone.h"
#include "Pickups.h"
#include "Police.h"

namespace atg {

namespace {
constexpr int NITRO_PRICE = 4000, PAINT_PRICE = 400, RESPRAY_PRICE = 100;
struct Paint { uint32_t c; const char* name; };
const Paint PAINTS[] = {
	{ 0xc1121f, "Red" }, { 0xf77f00, "Orange" }, { 0xfcbf49, "Yellow" }, { 0x2a9d8f, "Teal" }, { 0x1d3557, "Navy" }, { 0x3a86ff, "Blue" },
	{ 0x8338ec, "Purple" }, { 0xff006e, "Pink" }, { 0x111111, "Black" }, { 0xf1faee, "White" }, { 0x6c757d, "Grey" }, { 0x3a5a40, "Green" },
};
std::string Money(double n) {
	std::string s = std::to_string((long long)JsRound(n)), out;
	for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) out += ','; out += s[i]; }
	return "$" + out;
}
std::string Upper(std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }
} // namespace

const std::vector<EngineStage>& EngineStages() {
	static const std::vector<EngineStage> E = { { 1, 1, 0 }, { 1.18, 1.06, 2500 }, { 1.36, 1.12, 6000 }, { 1.6, 1.2, 12000 } };
	return E;
}
const std::vector<ArmorLevel>& ArmorLevels() {
	static const std::vector<ArmorLevel> A = { { 1, 1, 0 }, { 1.5, 0.75, 3000 }, { 2, 0.55, 7000 }, { 2.8, 0.35, 15000 } };
	return A;
}

ModShop::ModShop(Game& g) : game(g) {
	const auto& L = game.map.landmarks;
	auto add = [this](const std::string& name, double x, double z) {
		if (!Finite(x)) return;
		MarkerOpts o; o.color = 0xff9f1c; o.radius = 3.6; o.height = 2.5; o.icon = "wrench"; o.label = name; o.vehicleOnly = true; o.arrow = false;
		o.onEnter = [this, name](Marker*) { open(name); };
		garages.push_back({ name, x, z, game.pickupsSys->addMarker(x, z, o) });
	};
	// the two Spray Shacks became Customs garages, and San Aurelio has its own on Mission Street
	if (auto it = L.find("spray"); it != L.end()) add("Los Soles Customs", it->second.x, it->second.z);
	if (auto it = L.find("spray2"); it != L.end()) add("Corona Customs", it->second.x, it->second.z);
	RoadSpot sp;
	if (FindRoadSpot(game, NCITY.x + 60, NCITY.z + 250, false, sp)) {
		add("Aurelio Customs", sp.x, sp.z);
		Landmark lm; lm.x = sp.x; lm.z = sp.z; lm.name = "Aurelio Customs";
		const_cast<CityMap&>(game.map).landmarks["aurCustoms"] = lm;
	}
	// (the old instant spray is now a menu option)
	std::vector<Marker*> sprays;
	for (Marker* s : game.pickupsSys->services) if (s && s->blip && s->blip->icon == "spray") sprays.push_back(s);
	for (Marker* s : sprays) game.pickupsSys->removeMarker(s);
	auto& sv = game.pickupsSys->services;
	sv.erase(std::remove_if(sv.begin(), sv.end(), [&sprays](Marker* s) { return std::find(sprays.begin(), sprays.end(), s) != sprays.end(); }), sv.end());
}

// ---------------------------------------------------------------- per-vehicle tuning
void ModShop::Apply(Vehicle* v) {
	const EngineStage& e = EngineStages()[v->mods.engine];
	const ArmorLevel& a = ArmorLevels()[v->mods.armor];
	v->forceMul = e.force;
	v->topMul = e.top;
	v->bulletMod = a.bullet;
	const double frac = v->maxHealth > 0 ? v->health / v->maxHealth : 1;
	v->maxHealth = v->def.Health() * a.hp;
	v->health = Max(v->health, v->maxHealth * frac);
}
bool ModShop::CanMod(const Vehicle* v) { return v && v->def.kind.empty() && v->def.bike.empty(); }

std::vector<StoreItem> ModShop::rows(Vehicle* v) {
	std::vector<StoreItem> out;
	if (!v || v->removed) return out;
	auto row = [&out](const std::string& title, const std::string& sub, const std::string& label, int price, bool ok, std::function<void()> fn) {
		StoreItem it; it.name = title; it.desc = sub; it.button = label; it.price = price; it.available = ok;
		it.use = [fn](Player&, Game& g) { fn(); g.events.purchase.emit("mod"); return std::string(); };
		out.push_back(it);
	};
	Ref<Vehicle> ref(v);
	const int eng = v->mods.engine, arm = v->mods.armor;
	const EngineStage* nextE = eng + 1 < (int)EngineStages().size() ? &EngineStages()[eng + 1] : nullptr;
	const ArmorLevel* nextA = arm + 1 < (int)ArmorLevels().size() ? &ArmorLevels()[arm + 1] : nullptr;
	row("Engine \xE2\x80\x94 Stage " + std::to_string(eng),
		nextE ? "Stage " + std::to_string(eng + 1) + ": +" + std::to_string((int)JsRound((nextE->force - 1) * 100)) + "% power, +" + std::to_string((int)JsRound((nextE->top - 1) * 100)) + "% top speed" : "Fully tuned",
		nextE ? "Stage " + std::to_string(eng + 1) + " " + Money(nextE->price) : "Maxed", nextE ? nextE->price : 0, nextE != nullptr,
		[ref, eng]() { if (Vehicle* c = ref.get()) { c->mods.engine = eng + 1; Apply(c); } });
	char hp[16]; if (nextA) std::snprintf(hp, sizeof hp, "%g", nextA->hp);
	row("Armour \xE2\x80\x94 Level " + std::to_string(arm),
		nextA ? "Level " + std::to_string(arm + 1) + ": " + hp + "\xC3\x97 body strength, bullets do " + std::to_string((int)JsRound(nextA->bullet * 100)) + "%" : "Built like a tank",
		nextA ? "Level " + std::to_string(arm + 1) + " " + Money(nextA->price) : "Maxed", nextA ? nextA->price : 0, nextA != nullptr,
		[ref, arm]() { if (Vehicle* c = ref.get()) { c->mods.armor = arm + 1; Apply(c); c->health = c->maxHealth; } });
	const int nb = v->mods.nitro;
	row("Nitrous", nb ? std::to_string(nb) + " of 3 bottles left \xE2\x80\x94 hold <b>Shift</b> (A on a pad) to fire one" : "Three bottles, three seconds each. <b>Shift</b> to boost.",
		nb >= 3 ? "Full" : nb ? "Refill " + Money(NITRO_PRICE / 2) : "Fit " + Money(NITRO_PRICE), nb ? NITRO_PRICE / 2 : NITRO_PRICE, nb < 3,
		[ref]() { if (Vehicle* c = ref.get()) c->mods.nitro = 3; });
	// paint swatches (one row each here; the browser lays them out in one row of buttons)
	for (const Paint& p : PAINTS) {
		const uint32_t c = p.c;
		row(std::string("Paint \xE2\x80\x94 ") + p.name, Money(PAINT_PRICE) + " a coat", Money(PAINT_PRICE), PAINT_PRICE, true,
			[ref, c]() { if (Vehicle* car = ref.get()) { car->color = c; car->painted = true; } });
		out.back().swatch = c;
	}
	const bool wanted = game.policeSys && game.policeSys->level > 0;
	row("Respray & repair", wanted ? "Fix it up and the cops won't recognise you" : "Fix it up, good as new", "Respray " + Money(RESPRAY_PRICE), RESPRAY_PRICE, true,
		[this, ref]() {
			Vehicle* c = ref.get();
			if (!c) return;
			const double h = Rand(), s = Rand(0.4, 0.8), l = Rand(0.25, 0.55);
			c->color = HslHex(h, s, l); c->painted = true;
			c->health = c->maxHealth; c->onFire = false; c->burnTime = 0; c->flat = false;
			c->undent();
			if (!game.missionNoSpray && game.policeSys) game.policeSys->clear();
			game.stats.sprays++;
		});
	return out;
}

void ModShop::open(const std::string& name) {
	Player& p = *game.player;
	Vehicle* v = p.vehicle;
	if (!v || v->isWrecked() || v->driver() != &p) return;
	if (!CanMod(v)) { if (game.hud) game.hud->help("Customs only works on cars and trucks.", 3); return; }
	if (game.missionNoSpray && game.policeSys && game.policeSys->level > 0) { if (game.hud) game.hud->help("Not now \xE2\x80\x94 you're on a job.", 3); return; }
	v->input.throttle = 0; v->input.brake = 1; v->vel *= 0.2;
	if (!game.hud) return;
	Ref<Vehicle> ref(v);
	PanelOpts o; o.inVehicle = true; o.leave = "Drive out (Esc)";
	o.refresh = [this, ref]() { return rows(ref.get()); };
	game.hud->openPanel(Upper(name), v->def.name + " \xE2\x80\x94 make it yours", rows(v), o);
}

// ---------------------------------------------------------------- nitrous
void ModShop::update(double dt) {
	Player& p = *game.player;
	Vehicle* v = p.vehicle;
	if (game.rig.boostFov && boostT <= 0) game.rig.boostFov = Max(0, game.rig.boostFov - dt * 20);
	if (!v || v->driver() != &p || (!v->mods.nitro && boostT <= 0)) { boostT = 0; return; }
	if (boostT <= 0 && v->mods.nitro > 0 && game.input.hit("nitro") && !game.paused && v->speedAbs() > 2) {
		v->mods.nitro--;
		boostT = 3;
		game.soundAt("swoosh", v->pos, 1);
		if (game.hud) game.hud->help("NITROUS! " + std::to_string(v->mods.nitro) + " bottle" + (v->mods.nitro == 1 ? "" : "s") + " left", 1.5);
	}
	if (boostT > 0) {
		boostT -= dt;
		const double fx = std::sin(v->yaw), fz = std::cos(v->yaw);
		const double top = v->top() * 1.4;
		const double along = v->vel.x * fx + v->vel.z * fz;
		if (along < top && !v->airborne) { v->vel.x += fx * 13 * dt; v->vel.z += fz * 13 * dt; }
		game.rig.boostFov = Min(10, game.rig.boostFov + dt * 40);
		// blue flame out of the exhaust
		flameT -= dt;
		if (flameT <= 0) {
			flameT = 0.02;
			Effects* fx2 = dynamic_cast<Effects*>(game.effects);
			if (fx2) {
				const double back = -(v->def.L ? v->def.L : 4.5) / 2 - 0.2;
				Particle q;
				q.x = v->pos.x + fx * back; q.y = v->pos.y + 0.45; q.z = v->pos.z + fz * back;
				q.vx = -fx * 6 + Rand(-0.5, 0.5); q.vy = Rand(0, 0.4); q.vz = -fz * 6 + Rand(-0.5, 0.5);
				q.age = 0; q.life = 0.18; q.size0 = 0.5; q.size1 = 0.15; q.rot = 0; q.spin = 0; q.grav = 0; q.drag = 1;
				q.alpha = 1; q.fadeIn = 0.01; q.fadePow = 1;
				q.color[0] = 0.6f; q.color[1] = 1.4f; q.color[2] = 4.5f;
				q.color1[0] = 1.5f; q.color1[1] = 0.6f; q.color1[2] = 3; q.hasColor1 = true;
				fx2->addPool.spawn(std::move(q));
			}
		}
	}
}

} // namespace atg
