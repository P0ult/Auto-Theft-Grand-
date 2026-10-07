#include "Shops.h"
#include "Player.h"
#include "Peds.h"
#include "Collision.h"
#include "Pickups.h"
#include "Police.h"
#include "Game.h"
#include "Systems.h"
#include <random>
#include <algorithm>

namespace atg {

// ------------------------------------------------------------------ Shop stock definitions

const std::vector<ShopStockItem>& StoreStock() {
	static const std::vector<ShopStockItem> S = {
		{ "Hot dog", "+20 health", 3, [](Player* p) { p->health = std::min(p->maxHealth, p->health + 20); } },
		{ "Chips & soda", "+15 health, some stamina", 3, [](Player* p) { p->health = std::min(p->maxHealth, p->health + 15); p->stamina = std::min(1.0, p->stamina + 0.4); } },
		{ "First-aid kit", "Full health", 60, [](Player* p) { p->health = p->maxHealth; } },
		{ "Energy drink", "Full stamina, +10 health", 4, [](Player* p) { p->stamina = 1; p->health = std::min(p->maxHealth, p->health + 10); } },
	};
	return S;
}

const std::vector<ShopStockItem>& BarStock() {
	static const std::vector<ShopStockItem> S = {
		{ "Beer", "+5 health", 5, [](Player* p) { p->health = std::min(p->maxHealth, p->health + 5); } },
		{ "Whiskey", "+10 health", 12, [](Player* p) { p->health = std::min(p->maxHealth, p->health + 10); } },
		{ "Tequila shot", "+5 health", 8, [](Player* p) { p->health = std::min(p->maxHealth, p->health + 5); } },
		{ "Glass of water", "Sober up", 1, [](Player* p) { } },
	};
	return S;
}

const std::vector<ShopStockItem>& CafeStock() {
	static const std::vector<ShopStockItem> S = {
		{ "Espresso", "Full stamina", 3, [](Player* p) { p->stamina = 1; } },
		{ "Iced latte", "Full stamina, +10 health", 5, [](Player* p) { p->stamina = 1; p->health = std::min(p->maxHealth, p->health + 10); } },
		{ "Glazed donut", "+15 health", 2, [](Player* p) { p->health = std::min(p->maxHealth, p->health + 15); } },
		{ "Club sandwich", "+40 health", 6, [](Player* p) { p->health = std::min(p->maxHealth, p->health + 40); } },
	};
	return S;
}

const std::vector<ShopStockItem>& LiquorStock() {
	static const std::vector<ShopStockItem> S = {
		{ "Candy bar", "+10 health", 2, [](Player* p) { p->health = std::min(p->maxHealth, p->health + 10); } },
		{ "Sub sandwich", "+35 health", 6, [](Player* p) { p->health = std::min(p->maxHealth, p->health + 35); } },
		{ "Energy drink", "Full stamina, +10 health", 4, [](Player* p) { p->stamina = 1; p->health = std::min(p->maxHealth, p->health + 10); } },
		{ "Scratch card", "Could be your lucky day", 5, [](Player* p) {
			const double r = (double)rand() / RAND_MAX;
			const int win = r < 0.02 ? 5000 : r < 0.1 ? 250 : r < 0.3 ? 20 : 0;
			if (win) { p->money += win; if (p->game.hud) p->game.hud->moneyFlash(win); if (p->game.audio) p->game.audio->play("cash"); } } },
	};
	return S;
}

// ------------------------------------------------------------------ Clerk definitions

namespace {
std::map<std::string, ShopDef> MakeClerks() {
	std::map<std::string, ShopDef> M;
	M["gunshop"] = { "gunshop", "Gun Barn", "gun", 0xff5a36, { 60, 240 }, "shotgun", false,
		{ "Welcome to the Gun Barn. Look, don't touch.", "What can I do you for?", "Second Amendment's open for business." } };
	M["burger"] = { "burger", "Big Bun Burgers", "burger", 0xffd166, { 240, 600 }, "", false,
		{ "Welcome to Big Bun! Can I take your order?", "Hi! Try the Double Stack.", "Big Bun, how can I help you?" } };
	M["liquor"] = { "liquor", "Ray's Liquor", "money", 0x8ae3ff, { 150, 650 }, "", false,
		{ "Hey.", "Store closes when I say it does.", "No loitering." } };
	M["store"] = { "store", "24/7", "cart", 0x7cf29c, { 80, 420 }, "", false,
		{ "Welcome to 24/7.", "Hot dogs are fresh. Ish.", "Hey, how's it going?" } };
	M["bar"] = { "bar", "Bar", "beer", 0xffa24a, { 200, 700 }, "", false,
		{ "What'll it be?", "First one's not on the house.", "Pull up a stool." } };
	M["cafe"] = { "cafe", "Bean Scene", "cup", 0xe8c39a, { 60, 300 }, "", false,
		{ "Hi! What can I get started for you?", "Welcome to Bean Scene.", "Oat milk's back in stock!" } };
	M["petshop"] = { "petshop", "Pet Shop", "pet", 0xffb3ba, { 100, 300 }, "", false,
		{ "Welcome! Looking for a companion?", "We have dogs, cats, and more!", "Adopt a friend today!" } };
	return M;
}
} // namespace

const std::map<std::string, ShopDef> ShopSystem::CLERKS = MakeClerks();

const ShopDef& ShopDefFor(const std::string& id) {
	static const ShopDef empty = { "", "", "", 0, { 0, 0 }, "", false, {} };
	auto it = ShopSystem::CLERKS.find(id);
	return it != ShopSystem::CLERKS.end() ? it->second : empty;
}

// ------------------------------------------------------------------ ShopSystem

ShopSystem::ShopSystem(Game& g) : game(g) {
	game.shops = this;
}

void ShopSystem::populate(const std::map<std::string, Interior>& shopInteriors) {
	for (const auto& [key, it] : shopInteriors) {
		Interior s = it;
		s.key = key;
		if (s.name.empty()) s.name = key;
		interiors.push_back(s);
	}
	for (auto& s : interiors) {
		if (s.clerk.x == 0 && s.clerk.z == 0) _spawnClerk(s);
	}
}

void ShopSystem::reset() {
	interiors.clear();
}

void ShopSystem::serve(const V3& playerPos) {
	// Find nearest shop interior that contains the player
	for (auto& s : interiors) {
		if (s.inside && s.inside(playerPos)) {
			// Check if clerk is alive and not hostile
			// For now, just trigger the serve logic
			_spawnClerk(s);
			break;
		}
	}
}

void ShopSystem::update(double dt) {
	for (auto& s : interiors) {
		_think(s, dt);
	}
}

void ShopSystem::_spawnClerk(Interior& s) {
	// Clerk spawning logic - create a Ped at the clerk position
	if (!game.peds) return;
	const ShopDef& d = CLERKS.at(s.key);
	// The ped system handles clerk creation
	// For now, mark that clerk should exist
	s.clerk = s.service; // placeholder
}

void ShopSystem::_think(Interior& s, double dt) {
	// Clerk AI logic - greet player, react to threats, handle robbery
}

bool ShopSystem::_threatened(Ped* c) {
	// Check if player is aiming at clerk
	return false;
}

} // namespace atg