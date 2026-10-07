// The phone (port of src/ui/phone.js, GTA V's iFruit): I (or D-pad up on foot) slides it up in the bottom right
// corner; the arrows / D-pad move, Enter / A picks, Backspace / B goes back, and you can still walk.
//   Contacts   Downtown Cab Co., Benny's (your last car delivered to the kerb), Lester (the cops lose your file,
//              for a price), Merryweather (three armed contractors follow you), Pegasus (free roam)
//   Cheats     GTA V-style codes, five minutes each outside free roam
//   Snapmatic  photo mode (the HUD goes), Map, Weather, Stats
// The screens' contents are here; ATGHUD draws the phone.
#pragma once

#include "Systems.h"

namespace atg {

class Game;
class Ped;
struct Blip;

// freeroam.js roadSpot: the kerbside lane of the road nearest a point (or the pavement beside it, on foot)
struct RoadSpot { double x, z, y, yaw; };
bool FindRoadSpot(Game& game, double x, double z, bool onFoot, RoadSpot& out);

extern const std::vector<std::pair<std::string, std::string>> CHEATS;

class Phone : public System {
public:
	explicit Phone(Game& game);
	Game& game;
	bool open = false, photo = false;
	std::string screen = "home";
	int sel = 0;
	struct App { std::string id, name, icon; uint32_t color; };
	struct Item { std::string name, sub, icon; uint32_t color = 0; std::function<void()> go; };
	std::vector<Item> items;
	std::map<std::string, double> timed;         // cheat -> game time it runs out (outside free roam)
	bool hasLastCar = false; std::string lastCarType; uint32_t lastCarColor = 0; // the last road vehicle you drove
	std::vector<std::pair<double, std::function<void()>>> pending; // phone calls being acted on
	std::vector<Ref<Ped>> mercs;
	Ref<Vehicle> delivered;
	std::shared_ptr<Blip> deliveredBlip;

	// the cheats running now (admin.js reads this outside free roam); empty when none
	std::map<std::string, bool> activeCheats();
	bool canOpen() const;
	void earlyInput(double dt) override;
	void show();
	void hide();
	void cheat(const std::string& code);
	void update(double dt) override;
	void reset() override { reset(false); }
	void reset(bool keepCheats);
	std::string statusTime() const;

private:
	bool slow = false, phoneCheats = false;
	void go(const std::string& s);
	void build();
	void app(const std::string& id);
	void setPhoto(bool on);
	void call(const std::string& who, std::function<void()> fn);
	bool pay(double amount);
	void cab();
	double lesterPrice() const;
	void lester();
	void mechanic();
	Vehicle* deliver(const std::string& type, bool hasColor, uint32_t color);
	void merryweather();
	void pegasus();
};

} // namespace atg
