// Save / load (port of src/game/save.js): progress, cash, weapons, stats, packages and the clock, plus
// properties and the pet, as JSON in a file (the browser uses localStorage; the Unreal side sets the path).
// The save is the browser game's format, so the same structure works for both. Free roam never touches it.
#pragma once

#include "Json.h"
#include "Systems.h"

namespace atg {

class Game;

class SaveSystem : public System {
public:
	explicit SaveSystem(Game& g);
	Game& game;
	std::string path;   // (the Unreal side sets this; empty: keep the save in memory, for the native tests)
	std::string memory; // (the in-memory save)

	bool hasSave() const;
	bool save(bool autoSave = false);
	bool load();
	void clear();

	std::string encode() const;
	bool decode(const std::string& text);
};

} // namespace atg
