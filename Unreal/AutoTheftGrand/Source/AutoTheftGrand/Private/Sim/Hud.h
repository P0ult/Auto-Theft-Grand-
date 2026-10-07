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
	std::string subsSpeaker, dispatchWhere;
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
	void interact(const std::string& text) override { interactText = text; }
	void fade(double dur = 0.5, std::function<void()> mid = nullptr) override;
	void fadeTo(double v, double dur = 0.5) override;
	void damage(double amount) override { damageFlash = Min(1, damageFlash + amount / 40); }
	void showWasted(const std::string& kind) override { shard = kind == "busted" ? "busted" : "wasted"; shardT = 0; }
	void deathMode(bool on) override { dead = on; if (!on) shard.clear(); }

	// after game.update, with the real frame time
	void update(double dt);
};

} // namespace atg
