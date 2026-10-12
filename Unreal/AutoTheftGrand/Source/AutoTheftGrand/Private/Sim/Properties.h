// Property (port of src/game/properties.js): buy safehouses and businesses around the state. A "For Sale"
// sign (green house on the map) marks each one; walk up to it to see the price. Safehouses give you a place
// to save and patch up. Businesses earn money every in-game hour, piling up in the till (up to five days'
// worth) until you come and collect.
#pragma once

#include "Systems.h"

namespace atg {

class Game;
class Marker;

struct Property {
	std::string id, name, kind; // kind: safehouse, business, both
	int price = 0, income = 0;   // (income: $ a day)
	double x = 0, z = 0;
	std::string blurb;
	Marker* marker = nullptr;
};

class Properties : public System {
public:
	explicit Properties(Game& g);
	Game& game;
	std::vector<Property> list;
	std::map<std::string, double> owned; // id -> the till
	double lastHours = NaN();

	void open(int index);
	void update(double dt) override;
	// properties.js serialize / load: [{ id, till }]
	std::vector<std::pair<std::string, double>> serialize() const;
	void load(const std::vector<std::pair<std::string, double>>& data);

private:
	void markers();
	std::vector<StoreItem> rows(int index);
};

} // namespace atg
