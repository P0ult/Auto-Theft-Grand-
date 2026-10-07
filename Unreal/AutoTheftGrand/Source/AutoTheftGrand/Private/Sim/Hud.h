// The HUD's state (the message and overlay half of src/ui/hud.js): help text, big messages, subtitles and
// objectives, the bar, money pops, dispatch lines, speech bubbles, the screen fade, the damage flash and
// the WASTED / BUSTED shard. The timers run as the browser HUD's do; the renderer (ATGHUD) only draws
// what is here. Text keeps the browser game's markup (<b>…</b> for keys).
#pragma once

#include "Systems.h"

namespace atg {

class Game;

class HudModel : public IHud {
public:
	explicit HudModel(Game& game);
	Game& game;

	struct Line { std::string text; double t = 0; };
	Line helpLine, subs, zone, veh, radio, money, dispatchLine;
	std::string subsSpeaker, dispatchWhere, radioGenre;
	struct Big { std::string text, style, sub; double t = 0; } big;
	std::string objectiveText;
	struct Bar { bool on = false; std::string label; double v = 0; std::string color; } bar;
	struct Speech { Ref<Character> who; std::string text; double t = 3; };
	std::vector<Speech> speeches;
	std::string interactText;
	// the black fade: opacity runs from `from` to `to` over `dur` real seconds
	struct Fade { double from = 0, to = 0, dur = 0.5, t = 0; double value() const; } fadeState;
	double damageFlash = 0;
	double vignette = 0;         // (uDamage: the flash, or a pulse when health is low)
	std::string lastZone; Vehicle* lastVeh = nullptr;
	bool dead = false;            // (deathMode: the rest of the HUD hidden)
	std::string shard;            // "wasted" / "busted" once the shard is up
	double shardT = 0;            // seconds since the shard came up (its CSS animation)
	double timerSeconds = NaN();
	std::string counterLabel, counterValue;
	bool letterboxed = false;
	std::optional<V2> gpsTarget;
	std::vector<V2> route;
	double routeTimer = 0;
	void setTimer(double sec) override { timerSeconds = sec; }
	void setCounter(const std::string& label, const std::string& value) override { counterLabel = label; counterValue = value; }
	void letterbox(bool on) override { letterboxed = on; }
	void routeTo(std::optional<V2> target) override { gpsTarget = target; routeTimer = 0; if (!target) route.clear(); }
	double creditsT = NaN();
	void showCredits() override { creditsT = 0; }
	void dismissCredits();

	void help(const std::string& text, double seconds = 5) override;
	void speech(Character* who, const std::string& text) override;
	void bigMessage(const std::string& text, const std::string& style = "title", double dur = 4, const std::string& sub = "") override;
	void subtitle(const std::string& text, const std::string& speaker = "", double dur = 4) override;
	void objective(const std::string& text, double dur = 7) override;
	void clearObjective() override { objectiveText.clear(); }
	void setBar(const std::string* label, double v = 0, const std::string& color = "#e63946") override;
	void moneyFlash(double amount) override;
	void promptSave() override;
	void openPause(const std::string& tab) override { pauseRequest = tab; }
	std::string pauseRequest; // (the Unreal side opens the map and clears it)
	void dispatch(const std::string& text, const std::string& where = "") override;
	void showRadio(const std::string& name, const std::string& genre = "") override { radio.text = name; radioGenre = genre; radio.t = 3; }
	void interact(const std::string& text) override { interactText = text; }
	void fade(double dur = 0.5, std::function<void()> mid = nullptr) override;
	void fadeTo(double v, double dur = 0.5) override;
	void damage(double amount) override { damageFlash = Min(1, damageFlash + amount / 40); }
	void showWasted(const std::string& kind) override { shard = kind == "busted" ? "busted" : "wasted"; shardT = 0; }
	void deathMode(bool on) override { dead = on; if (!on) shard.clear(); }

	// the shop menus (hud.js openShop / openStore, with padnav.js's highlight): one row per button, the last
	// one leaving. ATGHUD draws them and turns clicks, keys and the pad into menuPress / menuMove / closeOverlay.
	struct MenuRow { std::string icon, name, desc, button; bool enabled = true, primary = false; };
	struct Menu {
		std::string kind;          // "" (closed), "gunshop" or "store"
		std::string title, sub, note;
		std::vector<MenuRow> rows;
		int focus = 0;
	} menu;
	void openShop() override;
	void openStore(const std::string& title, const std::string& sub, const std::vector<StoreItem>& items) override;
	void closeOverlay();
	void menuPress(int row);
	void menuMove(int dir); // (the highlight to the next button up, -1, or down, +1)

	// after game.update, with the real frame time
	void update(double dt);

private:
	std::vector<StoreItem> storeItems;
	void menuRender();
};

} // namespace atg
