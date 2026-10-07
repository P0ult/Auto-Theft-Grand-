// Procedural sound (port of src/game/audio.js): synthesised effects with 3D panning and a city reverb, the
// player's engine, tyres, wind and horn, aircraft engines, sirens, the police helicopter, ambience (traffic,
// birds, crickets, waves, rain, thunder) and three generative radio stations. It runs on Sim/WebAudio; the
// Unreal side (Game/ATGAudio) calls init() once it can play sound and then pulls samples with render().
#pragma once

#include "Systems.h"
#include "WebAudio.h"
#include <map>
#include <mutex>
#include <random>

namespace atg {

class Game;
class Vehicle;

class Audio : public System, public IAudio {
public:
	explicit Audio(Game& game);
	~Audio() override;
	Game& game;
	// (the audio thread renders while the game thread schedules: both hold this)
	std::mutex mutex;
	std::unique_ptr<wa::Context> ctx;
	bool enabled = false;
	double volume, musicVolume;
	bool radioOn = true;

	// start the context and its graph (audio.js init, on the first user gesture there)
	void init();
	// stereo samples for the device (interleaved L R)
	void render(float* interleaved, int frames);
	void setVolume(double v);
	void setMusic(double v);

	// IAudio
	void play(const std::string& name, double vol = 1) override;
	void playAt(const std::string& name, const V3& pos, double vol = 1, const SoundOpts& opts = SoundOpts()) override;
	bool playSample(const std::string& name, double vol = 1) override;
	void stopSample(const std::string& name, double fade = 0) override;
	void muffle(bool on) override;
	double clock() const override;
	int loop(const std::string& name, const V3& pos) override;
	void loopPos(int id, const V3& pos) override;
	void loopVol(int id, double v) override;
	void loopStop(int id) override;
	void radioNext() override;
	std::string radioLabel() const override;

	void update(double dt) override;

	struct Station { const char* name; const char* genre; double bpm; const char* style; };
	static const std::vector<Station>& Stations();

private:
	using Buf = std::shared_ptr<const wa::Buffer>;
	std::mt19937 rng{ 20261007 };
	double rand01();
	double rand(double a, double b) { return a + (b - a) * rand01(); }

	wa::GainNode *master = nullptr, *stinger = nullptr, *sfx = nullptr, *music = nullptr, *amb = nullptr, *revSend = nullptr;
	wa::BiquadFilterNode* worldFilter = nullptr;
	Buf noise, brown, pink;
	// the player's vehicle
	struct Veh {
		wa::GainNode *out, *tg, *wg, *hg; wa::OscillatorNode *o1, *o2, *o3; wa::BiquadFilterNode *f, *tf;
		double rpm = 800; int gear = 1;
	} veh{};
	// aircraft (built on first use)
	struct Air {
		wa::GainNode *out, *rg, *bg, *wg, *pg, *rtg, *tg; wa::BiquadFilterNode *rf, *pf, *rof;
		wa::OscillatorNode *whine, *prop, *prop2, *lfo;
	};
	std::unique_ptr<Air> air;
	// ambience
	wa::GainNode *cityGain = nullptr, *waveGain = nullptr, *rainGain = nullptr;
	double birdT = 1, cricketT = 1, thunderT = -1;
	// looping sounds (the helicopter, sirens, fire)
	struct Loop { wa::PannerNode* pan; wa::GainNode* g; std::vector<wa::Node*> nodes; };
	std::map<int, Loop> loops;
	int nextLoop = 1;
	std::vector<int> sirens;
	// the radio
	struct Song { int key; std::vector<int> minor, prog; std::vector<int> mel, bass, kick; double swing; };
	struct Radio { wa::GainNode* out = nullptr; int station = 0; double nextTime = 0; int step = 0, bar = 0, songBars = 0; Song song; } radio;

	Buf noiseBuffer(double sec, const std::string& type);
	wa::BufferSourceNode* src(const Buf& b, bool loop = false);
	void env(wa::GainNode* g, double t, double a, double peak, double d, double sustain = 0, double rel = 0);
	struct Hit { double dur = 0.2, vol = 1; wa::BiquadFilterNode::Type type = wa::BiquadFilterNode::Highpass; double freq = 1000, q = 0.7, attack = 0.001, sweepTo = 0; Buf buf; double offset = -1; };
	wa::GainNode* noiseHit(wa::Node* dest, double t, const Hit& h);
	struct Tone { double f0 = 100, f1 = 0, dur = 0.2, vol = 0.5; wa::OscillatorNode::Type type = wa::OscillatorNode::Sine; double attack = 0.002; };
	wa::OscillatorNode* tone(wa::Node* dest, double t, const Tone& o);
	wa::PannerNode* panner(const V3& pos, double ref = 6, double max = 400);
	void make(const std::string& name, wa::Node* dest, double vol, double dist = 0, const SoundOpts& opts = SoundOpts());
	void setupVehicleAudio();
	void setupAirAudio();
	void airUpdate(Vehicle* pv, double t);
	void setupAmbience();
	void chirp(bool night);
	void playLocked(const std::string& name, double vol);
	void loopStopLocked(int id);
	int loopLocked(const std::string& name, const V3& pos);
	void loopPosLocked(int id, const V3& pos);
	void radioNextLocked();
	// the radio
	void newSong();
	void schedule(double t, double stepDur);
	void radioUpdate(double dt, bool inVehicle);
};

} // namespace atg
