// Shop system (port of src/game/shops.js): walk-in shops with clerks who serve you when you step up to the counter,
// greets you, and reacts when you point a gun at them — the burger bar and the liquor store hand over the till (and call the cops),
// the Gun Barn's owner pulls a shotgun. Kill the clerk and the shop is shut until you've been gone a while.
#pragma once

#include "Game.h"
#include "Systems.h"
#include "Peds.h"
#include <vector>
#include <map>
#include <functional>

namespace atg {

struct ShopDef {
	std::string id;
	std::string label;
	std::string icon;
	uint32_t color;
	std::vector<int> cash;       // till cash range
	std::string weapon;          // clerk's weapon if hostile
	bool hostile = false;
	std::vector<std::string> greet;
};

struct ShopStockItem {
	std::string name;
	std::string desc;
	double price;
	std::function<void(class Player*)> use;
};

class Ped;

class ShopSystem : public System, public IShopSystem {
public:
	explicit ShopSystem(Game& game);
	~ShopSystem() override = default;

	Game& game;

	// shop interiors (set up by the world generator)
	struct Interior {
		std::string key;
		std::string name;
		V3 service;      // counter position
		V3 center;       // shop center
		V3 clerk;        // clerk spawn position
		std::function<bool(const V3&)> inside;  // whether player is inside
	};
	std::vector<Interior> interiors;

	void populate(const std::map<std::string, Interior>& shopInteriors);

	// IShopSystem implementation
	void serve(const V3& playerPos) override;
	void update(double dt) override;
	void reset() override;

	// Clerk definitions (public for ShopDefFor)
	static const std::map<std::string, ShopDef> CLERKS;

private:
	void _spawnClerk(Interior& s);
	void _think(Interior& s, double dt);
	bool _threatened(class Ped* c);
};

const ShopDef& ShopDefFor(const std::string& id);
const std::vector<ShopStockItem>& StoreStock();
const std::vector<ShopStockItem>& BarStock();
const std::vector<ShopStockItem>& CafeStock();
const std::vector<ShopStockItem>& LiquorStock();

} // namespace atg