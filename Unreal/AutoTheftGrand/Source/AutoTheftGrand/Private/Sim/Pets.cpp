#include "Pets.h"
#include "Combat.h"
#include "Game.h"
#include "Gameplay.h"
#include "Ragdoll.h"
#include "Wildlife.h"

namespace atg {
namespace {
const std::vector<std::string> DogNames = { "Rex", "Chop", "Buddy", "Luna", "Max", "Bella", "Rocky", "Daisy", "Duke", "Coco", "Bruno", "Nala", "Tank", "Biscuit" };
const std::vector<std::string> CatNames = { "Mittens", "Salem", "Whiskers", "Luna", "Oliver", "Cleo", "Tiger", "Smokey", "Pepper" };
}

PetSystem::PetSystem(Game& g) : game(g) {
	g.events.enteredVehicle.on([this](Character* c, Vehicle* v) { if (c == game.player.get()) board(v); });
	g.events.exitedVehicle.on([this](Character* c, Vehicle* v) { if (c == game.player.get()) alight(v); });
	g.animalExtras = [this](std::vector<Animal*>& out) { animals(out); };
}

void PetSystem::animals(std::vector<Animal*>& out) const {
	if (pet && !pet->removed) out.push_back(pet.get());
	for (const auto& kv : remote) out.push_back(kv.second.get());
}

Animal* PetSystem::adopt(const std::string& breed, const std::string& name) {
	if (!AnimalBreeds().count(breed)) return nullptr;
	release();
	Player& p = *game.player;
	const std::string kind = AnimalBreeds().at(breed).species;
	const std::string nm = name.empty() ? RandPick(kind == "cat" ? CatNames : DogNames) : name;
	const double x = p.pos.x + std::sin(p.yaw) * 1.3, z = p.pos.z + std::cos(p.yaw) * 1.3;
	auto a = std::make_shared<Animal>(game, breed, x, z, true, game.collision->floorHeight(x, z, p.pos.y + 1), true, p.yaw + kPi);
	a->pet = true; a->owner = &p; a->petName = nm;
	a->health = a->maxHealth = a->maxHealth * 3;
	Ref<Animal> weak(a);
	a->onHurt = [this, weak](Character*) { if (auto* animal = weak.get()) game.soundAt(animal->kind == "dog" ? "yelp" : "meow", animal->pos, 0.8); };
	pet = a; info = { breed, nm }; stay = false; target = nullptr; mourned = false;
	game.soundAt(kind == "dog" ? "bark" : "meow", a->pos, 0.9);
	game.events.petAdopted.emit(a.get());
	return a.get();
}

void PetSystem::release() {
	if (pet) { if (pet->inVehicle) pet->inVehicle->petSeat = -1; pet->remove(); }
	pet.reset(); info = Info(); target = nullptr;
}

void PetSystem::command() {
	Animal* a = pet.get(); Player& p = *game.player;
	if (!a || a->dead) { if (!a && game.hud) game.hud->help("You don't have a pet. Pet Palace (paw icon on the map) has dogs and cats looking for a home.", 5); return; }
	game.sound("whistle");
	if (p.aiming && a->kind == "dog" && !p.vehicle) {
		const V3 o = game.rig.camPos, dir = game.rig.lookDir();
		CombatHit hit;
		auto* combat = dynamic_cast<Combat*>(game.combat);
		if (combat && combat->raycast(o.x, o.y, o.z, dir.x, dir.y, dir.z, 60, &p, hit) && hit.kind == CombatHit::Char && !hit.ch->dead && !hit.ch->isPlayer) {
			target = hit.ch; stay = false;
			if (game.hud) game.hud->help("<b>" + a->petName + "</b>: get 'em!", 2);
			return;
		}
	}
	target = nullptr; stay = !stay;
	if (game.hud) game.hud->help("<b>" + a->petName + (stay ? "</b>: stay." : "</b>: come!"), 2);
}

void PetSystem::update(double dt) {
	if (game.input.hit("pet") && game.gameplay && game.gameplay->state == "playing" && !game.cutscene) command();
	if (pet) {
		auto a = pet;
		if (a->removed) pet.reset();
		else {
			if (a->dead) {
				if (!mourned) {
					mourned = true; deadT = 0;
					if (game.hud) game.hud->help("<b>" + a->petName + "</b> didn't make it. Pet Palace has more looking for a home.", 6);
					if (auto* v = a->inVehicle.get()) { v->petSeat = -1; a->getOut(v->pos.x + 2, v->pos.z); a->die(); }
				}
				deadT += dt;
				if (deadT > 25) { a->remove(); pet.reset(); info = Info(); }
			} else think(*a, dt);
			if (pet) a->update(dt);
		}
	}
	if (peerPresent) {
		std::vector<std::string> gone;
		for (const auto& kv : remote) if (!peerPresent(kv.first)) gone.push_back(kv.first);
		for (const auto& id : gone) dropRemote(id);
	}
}

void PetSystem::think(Animal& a, double dt) {
	Player& p = *game.player;
	if (auto* v = a.inVehicle.get()) {
		if (v->exploded) { v->petSeat = -1; a.getOut(v->pos.x + 2.5, v->pos.z); a.die(); }
		return;
	}
	if (a.kind == "dog" && p.lastDamager && game.time - p.lastHitTime < 0.6) {
		Character* src = p.lastDamager.get();
		if (!src->isPlayer && !src->dead && (!src->remote || src->npcProxy)) target = src;
	}
	if (Character* t = target.get()) {
		const V3 tp = t->ragdolling && t->ragdoll ? t->ragdoll->center() : t->pos;
		const double d = Hypot(tp.x - a.pos.x, tp.z - a.pos.z);
		if (t->dead || t->removed || t->vehicle || d > 70) target = nullptr;
		else {
			a.state = "follow"; a.happy = false;
			if (d > 1.1) a.goTo(tp.x, tp.z, a.sp.run);
			else {
				a.stop(); a.yaw = std::atan2(tp.x - a.pos.x, tp.z - a.pos.z); a.biteT -= dt;
				if (a.biteT <= 0) {
					a.biteT = 0.85;
					DamageInfo di; di.animalSource = &a; di.type = "melee"; di.part = "limb";
					di.hasImpulse = true; di.impulse = V3(std::sin(a.yaw) * 2, 1.2, std::cos(a.yaw) * 2);
					di.knockdown = Rand() < 0.3 && !t->isPlayer; di.hasHitPoint = true; di.hitPoint = tp;
					t->takeDamage(a.scale < 0.7 ? 5 : 13, di); game.soundAt("bark", a.pos, 0.8);
				}
			}
			return;
		}
	}
	if (stay) { a.stop(); a.state = "sit"; return; }
	FollowOwner(a, p, dt, true, p.vehicle ? 45 : 70);
	if (Rand() < dt * 0.03 && a.kind == "dog" && a.speed < 0.2) game.soundAt("bark", a.pos, 0.6);
}

void PetSystem::board(Vehicle* v) {
	Animal* a = pet.get();
	if (!a || a->dead || a->inVehicle || stay || !v->def.bike.empty() || !v->def.kind.empty()) return;
	if (Hypot(a->pos.x - v->pos.x, a->pos.z - v->pos.z) > 25) return;
	const int n = v->layout.seats.empty() ? 2 : (int)v->layout.seats.size();
	for (int s : { 1, 2, 3 }) if (s < n && !v->occupants[s]) { v->petSeat = s; a->sitIn(v, s); return; }
}

void PetSystem::alight(Vehicle* v) {
	if (!pet || pet->inVehicle != v) return;
	v->petSeat = -1;
	const V3 out = v->localToWorld(-(v->hx + 0.9), 0, 0.2);
	pet->getOut(out.x, out.z);
}

std::vector<double> PetSystem::netState() const {
	if (!pet || pet->removed || pet->dead) return {};
	const auto& breeds = PetBreeds();
	const auto b = std::find(breeds.begin(), breeds.end(), info.breed);
	auto round = [](double x) { return std::floor(x * 100 + 0.5) / 100; };
	return { (double)(b == breeds.end() ? -1 : b - breeds.begin()), round(pet->inVehicle ? pet->inVehicle->pos.x : pet->pos.x), round(pet->pos.y), round(pet->inVehicle ? pet->inVehicle->pos.z : pet->pos.z), round(pet->yaw), round(pet->speed), (double)((pet->state == "sit" ? 1 : 0) | (pet->inVehicle ? 2 : 0)) };
}

void PetSystem::poseRemote(const std::string& id, const std::vector<double>& st, double dt) {
	const auto& breeds = PetBreeds();
	bool valid = st.size() >= 7 && Finite(st[0]) && st[0] >= 0 && st[0] < breeds.size() && st[0] == std::floor(st[0]);
	if (valid) for (int i = 1; i < 6; i++) valid = valid && Finite(st[i]);
	if (!valid) { dropRemote(id); return; }
	const std::string& breed = breeds[(int)st[0]];
	auto& a = remote[id];
	if (!a || a->removed || a->breed != breed) { if (a) a->remove(); a = std::make_shared<Animal>(game, breed, st[1], st[3], true, st[2]); }
	const double k = Clamp(dt * 8, 0, 1);
	a->pos.x += (st[1] - a->pos.x) * k; a->pos.y += (st[2] - a->pos.y) * k; a->pos.z += (st[3] - a->pos.z) * k;
	a->yaw = st[4]; a->speed = st[5]; const int flags = Finite(st[6]) ? (int)st[6] : 0;
	a->state = flags & 1 ? "sit" : "follow"; a->visible = !(flags & 2); a->rootRoll = 0;
	a->t += dt; a->animate(dt);
}

void PetSystem::dropRemote(const std::string& id) { auto it = remote.find(id); if (it != remote.end()) { it->second->remove(); remote.erase(it); } }
} // namespace atg
