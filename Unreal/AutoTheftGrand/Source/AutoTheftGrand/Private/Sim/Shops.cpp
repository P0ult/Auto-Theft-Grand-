#include "Shops.h"
#include "AnimalModels.h"
#include "Camera.h"
#include "Game.h"
#include "Peds.h"
#include "Pets.h"
#include "Pickups.h"
#include "Player.h"
#include "Police.h"
#include "Skeleton.h"
#include "Weapons.h"

#include <cctype>

namespace atg {

namespace {

const std::map<std::string, ClerkDef>& Clerks() {
	static const std::map<std::string, ClerkDef> m = [] {
		std::map<std::string, ClerkDef> c;
		ClerkDef& gun = c["gunshop"];
		gun.greet = { "Welcome to the Gun Barn. Look, don't touch.", "What can I do you for?", "Second Amendment's open for business." };
		gun.threat = { "In MY store? Big mistake!", "You picked the wrong shop, pal!" };
		gun.hostile = true; gun.weapon = "shotgun"; gun.label = "Gun Barn"; gun.color = 0xff5a36; gun.icon = "gun";
		ClerkDef& burger = c["burger"];
		burger.greet = { "Welcome to Big Bun! Can I take your order?", "Hi! Try the Double Stack.", "Big Bun, how can I help you?" };
		burger.threat = { "Please! Take it! Take it all!", "I only work here!" };
		burger.cash[0] = 60; burger.cash[1] = 240; burger.label = "Big Bun Burgers"; burger.color = 0xffd166; burger.icon = "burger";
		ClerkDef& liquor = c["liquor"];
		liquor.greet = { "Hey.", "Store closes when I say it does.", "No loitering." };
		liquor.threat = { "Not again! Here \xE2\x80\x94 just go!", "Okay, okay! It's all yours!" };
		liquor.cash[0] = 150; liquor.cash[1] = 650; liquor.label = "Ray's Liquor"; liquor.color = 0x8ae3ff; liquor.icon = "money";
		ClerkDef& pet = c["petshop"];
		pet.greet = { "Welcome to Pet Palace! Looking for a new best friend?", "Hi! All our dogs and cats are vaccinated and chipped.", "Every one of them needs a good home." };
		pet.threat = { "Please! Don't hurt the animals!", "Take the money \xE2\x80\x94 leave the puppies alone!" };
		pet.cash[0] = 120; pet.cash[1] = 480; pet.color = 0x7be0a0; pet.icon = "paw";
		ClerkDef& store = c["store"];
		store.greet = { "Welcome to 24/7.", "Hot dogs are fresh. Ish.", "Hey, how's it going?" };
		store.threat = { "Whoa, whoa! It's yours!", "Minimum wage doesn't cover this!" };
		store.cash[0] = 80; store.cash[1] = 420; store.color = 0x7cf29c; store.icon = "cart";
		ClerkDef& bar = c["bar"];
		bar.greet = { "What'll it be?", "First one's not on the house.", "Pull up a stool." };
		bar.threat = { "Easy, pal. Take the register.", "You're making a big mistake." };
		bar.cash[0] = 200; bar.cash[1] = 700; bar.color = 0xffa24a; bar.icon = "beer";
		ClerkDef& cafe = c["cafe"];
		cafe.greet = { "Hi! What can I get started for you?", "Welcome to Bean Scene.", "Oat milk's back in stock!" };
		cafe.threat = { "Okay! Okay! Here!", "Please, just take it!" };
		cafe.cash[0] = 60; cafe.cash[1] = 300; cafe.color = 0xe8c39a; cafe.icon = "cup";
		return c;
	}();
	return m;
}

// randomAppearance(rng, CLERKS[key].look): the female choice first (when the look doesn't fix it), the rest
// of the look over the random one
Appearance ClerkLook(const std::string& key, RNG& rng) {
	int female = -1;
	if (key == "gunshop" || key == "liquor" || key == "bar") female = 0;
	else if (key == "petshop" || key == "cafe") female = 1;
	Appearance a = RandomAppearance(rng, female);
	if (key == "gunshop") {
		a.shirtType = "jacket"; a.jacketColor = 0x4b5320; a.shirt = 0x2f2f2f; a.pants = 0x3b3326; a.hairStyle = "cap"; a.hat = 0x222222;
		a.beard = true; a.glasses = true; a.build = 1.2; a.height = 1.03;
	} else if (key == "burger") {
		a.shirtType = "tee"; a.shirt = 0xc1121f; a.pants = 0x222222; a.hairStyle = "cap"; a.hat = 0xc1121f;
	} else if (key == "liquor") {
		a.shirtType = "tank"; a.shirt = 0xe8e8e8; a.pants = 0x3a3a3a; a.hairStyle = "bald"; a.beard = true; a.build = 1.15;
	} else if (key == "petshop") {
		a.shirtType = "tee"; a.shirt = 0x2e8b57; a.pants = 0x2b2b2b; a.hairStyle = "ponytail"; a.glasses = true;
	} else if (key == "store") {
		a.shirtType = "tee"; a.shirt = 0x0f7a3a; a.pants = 0x222222; a.hairStyle = "cap"; a.hat = 0x0f7a3a;
	} else if (key == "bar") {
		a.shirtType = "jacket"; a.jacketColor = 0x1b1b1b; a.shirt = 0xe8e8e8; a.pants = 0x1b1b1b; a.hairStyle = "short"; a.beard = true; a.build = 1.1;
	} else if (key == "cafe") {
		a.shirtType = "tee"; a.shirt = 0x6b4226; a.pants = 0x222222; a.hairStyle = "bun";
	}
	if (female >= 0) a.female = female == 1;
	return a;
}

void Heal(Player& p, double hp) { p.health = Min(p.maxHealth, p.health + hp); }

StoreItem Item(const std::string& name, const std::string& desc, int price, std::function<std::string(Player&, Game&)> use) {
	StoreItem it; it.name = name; it.desc = desc; it.price = price; it.use = std::move(use); return it;
}

const std::vector<StoreItem>& StoreStock() {
	static const std::vector<StoreItem> s = {
		Item("Hot dog", "+20 health", 3, [](Player& p, Game&) { Heal(p, 20); return std::string(); }),
		Item("Chips & soda", "+15 health, some stamina", 3, [](Player& p, Game&) { Heal(p, 15); p.stamina = Min(1.0, p.stamina + 0.4); return std::string(); }),
		Item("First-aid kit", "Full health", 60, [](Player& p, Game&) { p.health = p.maxHealth; return std::string(); }),
		Item("Energy drink", "Full stamina, +10 health", 4, [](Player& p, Game&) { p.stamina = 1; Heal(p, 10); return std::string(); }),
	};
	return s;
}

std::function<std::string(Player&, Game&)> Drink(double k, double hp) {
	return [k, hp](Player& p, Game&) {
		Heal(p, hp);
		p.drunk = Min(1.5, p.drunk + k);
		return p.drunk > 1 ? std::string("The room is starting to spin...") : std::string();
	};
}

const std::vector<StoreItem>& BarStock() {
	static const std::vector<StoreItem> s = {
		Item("Beer", "+5 health \xC2\xB7 a little tipsy", 5, Drink(0.3, 5)),
		Item("Whiskey", "+10 health \xC2\xB7 the room sways", 12, Drink(0.6, 10)),
		Item("Tequila shot", "Liquid courage", 8, Drink(0.45, 5)),
		Item("Glass of water", "Sober up", 1, [](Player& p, Game&) { p.drunk = 0; return std::string("Much better."); }),
	};
	return s;
}

const std::vector<StoreItem>& CafeStock() {
	static const std::vector<StoreItem> s = {
		Item("Espresso", "Full stamina", 3, [](Player& p, Game&) { p.stamina = 1; p.drunk = Max(0.0, p.drunk - 0.4); return std::string(); }),
		Item("Iced latte", "Full stamina, +10 health", 5, [](Player& p, Game&) { p.stamina = 1; Heal(p, 10); return std::string(); }),
		Item("Glazed donut", "+15 health", 2, [](Player& p, Game&) { Heal(p, 15); return std::string(); }),
		Item("Club sandwich", "+40 health", 6, [](Player& p, Game&) { Heal(p, 40); return std::string(); }),
	};
	return s;
}

const std::vector<StoreItem>& LiquorStock() {
	static const std::vector<StoreItem> s = {
		Item("Candy bar", "+10 health", 2, [](Player& p, Game&) { Heal(p, 10); return std::string(); }),
		Item("Sub sandwich", "+35 health", 6, [](Player& p, Game&) { Heal(p, 35); return std::string(); }),
		Item("Energy drink", "Full stamina, +10 health", 4, [](Player& p, Game&) { p.stamina = 1; Heal(p, 10); return std::string(); }),
		Item("Scratch card", "Could be your lucky day", 5, [](Player& p, Game& g) {
			const double r = Rand();
			const int win = r < 0.02 ? 5000 : r < 0.1 ? 250 : r < 0.3 ? 20 : 0;
			if (win) { p.money += win; if (g.hud) g.hud->moneyFlash(win); if (g.audio) g.audio->play("cash"); }
			return win ? "Winner! $" + std::to_string(win) + "." : std::string("No luck this time.");
		}),
	};
	return s;
}

const std::map<std::string, std::string>& PetBlurb() {
	static const std::map<std::string, std::string> m = {
		{ "lab", "Friendly, loyal, loves the car" }, { "shepherd", "Protective \xE2\x80\x94 goes for anyone who hurts you" }, { "husky", "Tireless runner, very talkative" },
		{ "rottweiler", "Big, brave bodyguard" }, { "pug", "Small, snorts a lot, adorable" }, { "poodle", "Clever and well-groomed" },
		{ "tabby", "Independent, naps anywhere" }, { "blackcat", "Mysterious. Lucky?" }, { "siamese", "Chatty and curious" },
	};
	return m;
}

// Pet Palace's animals and treats.
std::vector<StoreItem> PetStock(Game& game) {
	std::vector<StoreItem> out;
	const bool pets = game.pets != nullptr;
	for (const std::string& b : PetBreeds()) {
		const AnimalBreed& br = AnimalBreeds().at(b);
		auto blurb = PetBlurb().find(b);
		StoreItem it = Item(br.name, blurb != PetBlurb().end() ? blurb->second : "", br.price > 0 ? (int)br.price : 800, [b](Player&, Game& g) {
			Animal* a = g.pets ? g.pets->adopt(b) : nullptr;
			return a ? "Meet " + a->petName + "! Whistle (K) to make them stay or come." : std::string("Something went wrong.");
		});
		it.available = pets;
		out.push_back(it);
	}
	out.push_back(Item("Pet treats", "Heal your pet", 15, [](Player&, Game& g) {
		Animal* a = g.pets ? g.pets->pet.get() : nullptr;
		if (!a || a->dead) return std::string("You don't have a pet yet.");
		a->health = a->maxHealth; return a->petName + " loves them.";
	}));
	return out;
}

std::string Upper(std::string s) { for (char& ch : s) ch = (char)std::toupper((unsigned char)ch); return s; }

} // namespace

ShopSystem::ShopSystem(Game& g) : game(g) {
	for (const InteriorShell& it : game.map.interiors) {
		auto d = Clerks().find(it.key);
		if (d == Clerks().end()) continue;
		Shop s; s.it = &it; s.def = &d->second;
		shops.push_back(s);
	}
	for (size_t i = 0; i < shops.size(); i++) {
		Shop& s = shops[i];
		const InteriorShell& it = *s.it;
		MarkerOpts o;
		o.hasY = true; o.y = it.service.y + 0.06;
		o.color = s.def->color; o.radius = 0.9; o.height = 1.2; o.icon = s.def->icon;
		o.label = s.def->label.empty() ? it.name : s.def->label;
		o.footOnly = true; o.arrow = false;
		o.onEnter = [this, i](Marker*) { serve(shops[i]); };
		s.marker = game.pickupsSys->addMarker(it.service.x, it.service.z, o);
		game.pickupsSys->services.push_back(s.marker);
	}
}

void ShopSystem::calmDown() {
	for (Shop& s : shops) if (s.state == "hostile" && s.clerk && !s.clerk->dead) { game.peds->remove(s.clerk.get()); s.clerk.reset(); s.state = "closed"; }
}

ShopSystem::Shop* ShopSystem::shopAt(double x, double z) {
	for (Shop& s : shops) if (s.it->Inside(x, z)) return &s;
	return nullptr;
}

void ShopSystem::serve(Shop& s) {
	Player& p = *game.player;
	Ped* c = s.clerk.get();
	if (!c || c->dead || c->removed || s.state != "calm") {
		if (game.hud) game.hud->help(s.state == "robbed" ? "The clerk is cowering behind the counter. Nobody's serving now." : "There's nobody behind the counter.", 3);
		return;
	}
	c->faceTowards(p.pos.x, p.pos.z, 0);
	const std::string& key = s.it->key;
	const std::string nm = Upper(s.it->name);
	IHud* hud = game.hud;
	if (key == "gunshop") { if (hud) hud->openShop(); }
	else if (key == "burger") game.pickupsSys->eat();
	else if (key == "liquor") { if (hud) hud->openStore("RAY'S LIQUOR", "Snacks, smokes & scratch cards", LiquorStock()); }
	else if (key == "store") {
		std::vector<StoreItem> items = StoreStock();
		items.insert(items.end(), LiquorStock().begin() + 3, LiquorStock().end());
		if (hud) hud->openStore(nm, "Open all day, every day", items);
	}
	else if (key == "bar") { if (hud) hud->openStore(nm, "Cold beer, strong spirits", BarStock()); }
	else if (key == "cafe") { if (hud) hud->openStore(nm, "Coffee & snacks", CafeStock()); }
	else if (key == "petshop") { if (hud) hud->openStore(nm, game.pets && game.pets->pet && !game.pets->pet->dead ? "Adopting a new friend replaces " + game.pets->pet->petName : "Dogs & cats looking for a home", PetStock(game)); }
}

Ped* ShopSystem::spawnClerk(Shop& s) {
	const InteriorShell& it = *s.it;
	RNG rng((uint32_t)(it.key.size() * 131 + 7));
	PedOpts o;
	o.hasAppearance = true; o.appearance = ClerkLook(it.key, rng);
	o.brain = "script"; o.persistent = true;
	o.hasYaw = true; o.yaw = it.clerkYaw;
	o.hasY = true; o.y = it.clerk.y;
	Ped* c = game.peds->spawnPed(it.clerk.x, it.clerk.z, o);
	c->shopClerk = true;
	c->health = c->maxHealth = 80;
	const size_t i = (size_t)(&s - shops.data());
	c->scriptThink = [this, i](double dt) { think(shops[i], dt); };
	s.clerk = std::static_pointer_cast<Ped>(c->shared_from_this());
	s.state = "calm"; s.t = 0; s.greeted = false;
	if (it.key == "petshop" && s.pets.empty()) stockKennels(s);
	return c;
}

void ShopSystem::stockKennels(Shop& s) {
	const InteriorShell& it = *s.it;
	const std::vector<std::string> breeds = { "lab", "pug", "husky", "tabby", "shepherd", "siamese", "poodle", "blackcat" };
	int i = 0;
	for (const auto& pen : it.furniture) if (pen.kind == "pen") {
		for (int k = 0; k < (i % 2 ? 1 : 2); k++) {
			const double u = (pen.u0 + pen.u1) / 2 + (k ? 0.45 : -0.3), w = (pen.w0 + pen.w1) / 2 + (k ? 0.5 : -0.3);
			auto a = std::make_shared<Animal>(game, breeds[(i * 2 + k) % breeds.size()], it.X(u, w), it.Z(u, w), true, it.fy + 0.09, true, it.YawR() + (k ? 0.6 : -0.4));
			a->state = (i + k) % 3 ? "sit" : "idle"; a->display = true;
			s.pets.push_back(a);
		}
		i++;
	}
}

void ShopSystem::think(Shop& s, double dt) {
	Ped* c = s.clerk.get();
	const InteriorShell& it = *s.it;
	Player& p = *game.player;
	s.t += dt;
	c->stop();
	c->animState.handsUp = false; c->animState.cower = false; c->crouching = false; c->animState.talking = false;
	// stay behind the counter
	const double dx = it.clerk.x - c->pos.x, dz = it.clerk.z - c->pos.z;
	if (dx * dx + dz * dz > 0.25 && s.state != "robbed") { c->goTo(it.clerk.x, it.clerk.z, 1.4, dt, 0.3); return; }
	const bool inside = !p.vehicle && it.Inside(p.pos.x, p.pos.z);
	if (s.state == "handsup") {
		c->animState.handsUp = true;
		c->faceTowards(p.pos.x, p.pos.z, dt, 6);
		if (s.t > 2.6) {
			// the till: cash on the counter
			const int amt = RandInt(s.def->cash[0], s.def->cash[1]);
			game.pickups->dropMoney(V3(it.service.x, it.service.y, it.service.z), amt);
			if (game.policeSys) game.policeSys->crime(4, c->pos, true);
			game.events.shopRobbed.emit(it.key, amt);
			s.state = "robbed"; s.t = 0; s.robbedAt = game.time;
		}
		return;
	}
	if (s.state == "robbed") {
		c->animState.cower = true; c->crouching = true;
		if (s.t > 40 && !inside) { s.state = "calm"; s.t = 0; s.greeted = false; }
		return;
	}
	// calm: face the customer when they're in the shop, the door otherwise
	if (inside && std::hypot(p.pos.x - c->pos.x, p.pos.z - c->pos.z) < 12) c->faceTowards(p.pos.x, p.pos.z, dt, 4);
	else c->yaw = it.clerkYaw;
	if (inside && !s.greeted) { s.greeted = true; c->say(RandPick(s.def->greet)); c->animState.talking = true; }
	if (!inside && std::hypot(p.pos.x - it.center.x, p.pos.z - it.center.z) > 25) s.greeted = false;
	// a gun in my face?
	if (inside && threatened(c)) {
		c->say(RandPick(s.def->threat));
		if (s.def->hostile) {
			c->brain = "civilian";
			c->giveWeapon(s.def->weapon, 60); c->equip(s.def->weapon);
			c->threat = &p; c->threatPos = p.pos; c->setState("attack");
			c->accuracy = 0.55;
			if (game.policeSys) game.policeSys->crime(2, c->pos, true);
			s.state = "hostile";
		} else { s.state = "handsup"; s.t = 0; }
	}
}

bool ShopSystem::threatened(Ped* c) {
	Player& p = *game.player;
	if (!p.aiming || p.vehicle || p.dead) return false;
	const WeaponDef& def = p.weaponDef();
	if (def.type != "gun" && def.type != "launcher") return false;
	const double d = std::hypot(c->pos.x - p.pos.x, c->pos.z - p.pos.z);
	if (d > 16) return false;
	const V3 cam = game.rig.camPos, dir = game.rig.lookDir();
	const double tx = c->pos.x - cam.x, ty = c->pos.y + 1.2 - cam.y, tz = c->pos.z - cam.z;
	double tl = std::sqrt(tx * tx + ty * ty + tz * tz);
	if (!tl) tl = 1;
	return (dir.x * tx + dir.y * ty + dir.z * tz) / tl > 0.95;
}

void ShopSystem::update(double dt) {
	Player& p = *game.player;
	const V3 pp = p.vehicle ? p.vehicle->pos : p.pos;
	// a few drinks: the world sways (walks it off over a minute or two)
	if (p.drunk > 0) {
		p.drunk = Max(0.0, p.drunk - dt * 0.012);
		const double k = Min(1.0, p.drunk), t = game.time;
		game.rig.yaw += std::sin(t * 0.9) * 0.0035 * k + std::sin(t * 2.3) * 0.0012 * k;
		game.rig.pitch = game.rig.pitch + std::sin(t * 1.3) * 0.0015 * k;
	}
	for (Shop& s : shops) {
		const InteriorShell& it = *s.it;
		const double d = std::hypot(pp.x - it.center.x, pp.z - it.center.z);
		if (!s.pets.empty()) {
			if (d > 110) { for (const auto& a : s.pets) a->remove(); s.pets.clear(); }
			else if (d < 60) for (const auto& a : s.pets) {
				a->t += dt; a->happy = Hypot(p.pos.x - a->pos.x, p.pos.z - a->pos.z) < 4; a->animate(dt);
				if (a->happy && Rand() < dt * 0.15) game.soundAt(a->kind == "dog" ? "bark" : "meow", a->pos, 0.5);
			}
		}
		Ped* c = s.clerk.get();
		if (c && (c->removed || c->dead)) {
			if (s.state != "dead") {
				s.state = "dead";
				s.respawnAt = game.time + 60;
				if (!c->removed) game.pickups->dropMoney(c->pos, RandInt(40, 160));
			}
			// the body goes once you're out of sight; a new clerk turns up later
			if (d > 70 && !c->removed) game.peds->remove(c);
			if (d > 70 && game.time > s.respawnAt) { s.clerk.reset(); s.state = "closed"; }
			continue;
		}
		if (!c && d < 85 && s.state != "dead") spawnClerk(s);
		else if (c && d > 130) { game.peds->remove(c); s.clerk.reset(); s.state = "closed"; }
		if (s.clerk && s.state == "hostile" && d > 60) { game.peds->remove(s.clerk.get()); s.clerk.reset(); s.state = "closed"; }
		s.marker->visible = s.state == "calm";
	}
}

} // namespace atg
