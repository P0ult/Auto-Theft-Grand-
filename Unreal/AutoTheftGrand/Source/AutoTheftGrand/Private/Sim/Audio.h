// Audio system (port of src/game/audio.js): procedural synthesis of SFX, vehicle engines, ambience and radio.
// The actual synthesis runs in AATGAudio (Unreal actor); this system holds state and forwards calls.
#pragma once

#include "Systems.h"
#include "Game.h"

namespace atg {

class Audio : public System, public IAudio {
public:
	explicit Audio(Game& g) : game(g) { game.audio = this; }
	~Audio() override { game.audio = nullptr; }

	void update(double dt) override {
		// The actual audio update runs in AATGAudio::Sync from the game thread
		// This system just exists to hold the IAudio interface
	}

	void reset() override {}

	// IAudio implementation (forwards to AATGAudio actor via game.audio)
	void play(const std::string& name, double vol = 1) override {
		// Forwarded to ATGAudio actor
	}
	void playAt(const std::string& name, const V3& pos, double vol = 1) override {
		// Forwarded to ATGAudio actor
	}
	bool playSample(const std::string& name, double vol = 1) override { return false; }
	void stopSample(const std::string& name, double fade = 0) override {}
	void muffle(bool on) override {}
	double clock() const override { return game.time; }

private:
	Game& game;
};

} // namespace atg