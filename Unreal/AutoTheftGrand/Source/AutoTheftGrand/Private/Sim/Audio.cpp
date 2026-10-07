#include "Audio.h"
#include "Aircraft.h"
#include "Game.h"
#include "Hud.h"
#include "Player.h"
#include "Vehicles.h"

namespace atg {

using namespace wa;

namespace {
const BiquadFilterNode::Type LP = BiquadFilterNode::Lowpass, HP = BiquadFilterNode::Highpass, BP = BiquadFilterNode::Bandpass;
const OscillatorNode::Type SINE = OscillatorNode::Sine, SQUARE = OscillatorNode::Square, SAW = OscillatorNode::Sawtooth, TRI = OscillatorNode::Triangle;
const int NONE = -999; // (a rest in the radio's patterns)
}

const std::vector<Audio::Station>& Audio::Stations() {
	static const std::vector<Station> S = {
		{ "Radio Los Soles", "West Coast Classics", 92, "gfunk" },
		{ "Neon 88.8", "Synthwave", 108, "synth" },
		{ "Low Rider Soul", "Slow Jams & Funk", 78, "soul" },
		{ "Off", "", 0, "off" },
	};
	return S;
}

Audio::Audio(Game& g) : game(g), volume(g.settings.volume), musicVolume(g.settings.music) {}
Audio::~Audio() = default;

double Audio::rand01() { return std::uniform_real_distribution<double>(0, 1)(rng); }

// ------------------------------------------------------------------ setup
void Audio::init() {
	std::lock_guard<std::mutex> lock(mutex);
	if (ctx) return;
	ctx = std::make_unique<Context>();
	master = ctx->createGain();
	master->gain.setValue(volume);
	DynamicsCompressorNode* comp = ctx->createDynamicsCompressor();
	comp->threshold = -14; comp->ratio = 4; comp->attack = 0.003; comp->release = 0.2;
	// the world filter: muffles everything (except stingers) on death / busted
	worldFilter = ctx->createBiquadFilter();
	worldFilter->type = LP; worldFilter->frequency.setValue(20000); worldFilter->Q.setValue(0.5);
	master->connect(worldFilter)->connect(comp)->connect(ctx->destination);
	// stingers bypass the world filter
	stinger = ctx->createGain(); stinger->gain.setValue(volume); stinger->connect(ctx->destination);
	sfx = ctx->createGain(); sfx->connect(master);
	music = ctx->createGain(); music->gain.setValue(musicVolume * 0.55); music->connect(master);
	amb = ctx->createGain(); amb->gain.setValue(0.5); amb->connect(master);
	// the reverb bus
	ReverbNode* reverb = ctx->createReverb();
	revSend = ctx->createGain(); revSend->gain.setValue(0.35);
	revSend->connect(reverb)->connect(sfx);
	// noise buffers
	noise = noiseBuffer(2, "white");
	brown = noiseBuffer(4, "brown");
	pink = noiseBuffer(3, "pink");
	enabled = true;
	setupVehicleAudio();
	setupAmbience();
	radio.out = ctx->createGain();
	radio.out->gain.setValue(0);
	radio.out->connect(music);
	radio.station = 0;
	newSong();
}

void Audio::render(float* interleaved, int frames) {
	std::lock_guard<std::mutex> lock(mutex);
	if (ctx) ctx->render(interleaved, frames);
	else for (int i = 0; i < frames * 2; i++) interleaved[i] = 0;
}

void Audio::setVolume(double v) { std::lock_guard<std::mutex> lock(mutex); volume = v; if (master) master->gain.setValue(v); if (stinger) stinger->gain.setValue(v); }
void Audio::setMusic(double v) { std::lock_guard<std::mutex> lock(mutex); musicVolume = v; if (music) music->gain.setValue(v * 0.55); }

Audio::Buf Audio::noiseBuffer(double sec, const std::string& type) {
	auto buf = std::make_shared<Buffer>();
	const size_t len = (size_t)std::floor(SR * sec);
	buf->data.resize(len);
	double last = 0, b0 = 0, b1 = 0, b2 = 0;
	for (size_t i = 0; i < len; i++) {
		const double w = rand01() * 2 - 1;
		if (type == "brown") { last = (last + 0.02 * w) / 1.02; buf->data[i] = (float)(last * 3.5); }
		else if (type == "pink") { b0 = 0.99765 * b0 + w * 0.099046; b1 = 0.963 * b1 + w * 0.2965164; b2 = 0.57 * b2 + w * 1.0526913; buf->data[i] = (float)((b0 + b1 + b2 + w * 0.1848) * 0.2); }
		else buf->data[i] = (float)w;
	}
	return buf;
}

// ------------------------------------------------------------------ primitives
BufferSourceNode* Audio::src(const Buf& b, bool loop) { BufferSourceNode* s = ctx->createBufferSource(); s->buffer = b; s->loop = loop; return s; }

void Audio::env(GainNode* g, double t, double a, double peak, double d, double sustain, double rel) {
	g->gain.setValueAtTime(0.0001, t);
	g->gain.exponentialRampToValueAtTime(Max(peak, 0.0002), t + a);
	g->gain.exponentialRampToValueAtTime(Max(sustain, 0.0001), t + a + d);
	if (rel) g->gain.exponentialRampToValueAtTime(0.0001, t + a + d + rel);
}

GainNode* Audio::noiseHit(Node* dest, double t, const Hit& h) {
	BufferSourceNode* s = src(h.buf ? h.buf : noise);
	BiquadFilterNode* f = ctx->createBiquadFilter(); f->type = h.type; f->frequency.setValueAtTime(h.freq, t); f->Q.setValue(h.q);
	if (h.sweepTo) f->frequency.exponentialRampToValueAtTime(h.sweepTo, t + h.dur);
	GainNode* g = ctx->createGain();
	env(g, t, h.attack, h.vol, h.dur);
	s->connect(f)->connect(g)->connect(dest);
	s->start(t, h.offset >= 0 ? h.offset : rand01() * 1.5);
	const double stop = t + h.dur + h.attack + 0.05;
	s->stop(stop);
	s->expires = f->expires = g->expires = stop + 0.05;
	ctx->noteEnd(stop);
	return g;
}

OscillatorNode* Audio::tone(Node* dest, double t, const Tone& o) {
	OscillatorNode* osc = ctx->createOscillator(); osc->type = o.type; osc->frequency.setValueAtTime(o.f0, t);
	if (o.f1) osc->frequency.exponentialRampToValueAtTime(Max(1, o.f1), t + o.dur);
	GainNode* g = ctx->createGain();
	env(g, t, o.attack, o.vol, o.dur);
	osc->connect(g)->connect(dest);
	osc->start(t);
	const double stop = t + o.dur + o.attack + 0.05;
	osc->stop(stop);
	osc->expires = g->expires = stop + 0.05;
	ctx->noteEnd(stop);
	return osc;
}

PannerNode* Audio::panner(const V3& pos, double ref, double max) {
	PannerNode* p = ctx->createPanner();
	p->refDistance = ref; p->maxDistance = max; p->rolloffFactor = 1.1;
	p->x = pos.x; p->y = pos.y; p->z = pos.z;
	return p;
}

void Audio::play(const std::string& name, double vol) {
	if (!enabled) return;
	std::lock_guard<std::mutex> lock(mutex);
	playLocked(name, vol);
}
void Audio::playLocked(const std::string& name, double vol) {
	ctx->scheduledEnd = ctx->currentTime();
	make(name, sfx, vol);
}

void Audio::playAt(const std::string& name, const V3& pos, double vol, const SoundOpts& opts) {
	if (!enabled || !pos.finite()) return;
	std::lock_guard<std::mutex> lock(mutex);
	const V3 cam = game.rig.camPos;
	const double d = cam.distanceTo(pos);
	const double size = IsSet(opts.size) && opts.size ? opts.size : 1;
	if (d > (opts.gun ? 500 : name == "explosion" ? 260 * Max(1, std::sqrt(size)) : 180)) return;
	PannerNode* pan = panner(pos, opts.gun ? 12 : 5);
	pan->connect(sfx);
	GainNode* send = nullptr;
	if (opts.gun || name == "explosion") {
		send = ctx->createGain(); send->gain.setValue(Clamp(0.3 + d / 150, 0.3, 1.4));
		pan->connect(send)->connect(revSend);
	}
	ctx->scheduledEnd = ctx->currentTime();
	make(name, pan, vol, d, opts);
	pan->expires = ctx->scheduledEnd + 0.1;
	if (send) send->expires = pan->expires;
}

void Audio::make(const std::string& name, Node* dest, double vol, double dist, const SoundOpts& opts) {
	const double t = ctx->currentTime() + 0.005;
	const double far = Clamp(dist / 200, 0, 1);
	auto H = [&](double dur, double v, BiquadFilterNode::Type type, double freq) { Hit h; h.dur = dur; h.vol = v; h.type = type; h.freq = freq; return h; };
	auto T = [&](double f0, double f1, double dur, double v, OscillatorNode::Type type = SINE, double attack = 0.002) { Tone o; o.f0 = f0; o.f1 = f1; o.dur = dur; o.vol = v; o.type = type; o.attack = attack; return o; };
	auto Q = [](Hit h, double q) { h.q = q; return h; };
	auto Sw = [](Hit h, double to) { h.sweepTo = to; return h; };
	auto At = [](Hit h, double a) { h.attack = a; return h; };
	auto Bf = [](Hit h, const Buf& b) { h.buf = b; return h; };
	if (name == "pistol") {
		noiseHit(dest, t, H(0.18, 1.2 * vol, HP, 900 - far * 600));
		noiseHit(dest, t, Q(H(0.06, 0.9 * vol, BP, 3000), 0.8));
		tone(dest, t, T(160, 45, 0.12, 1.1 * vol));
	} else if (name == "smg") {
		noiseHit(dest, t, H(0.09, 0.9 * vol, HP, 1300 - far * 800));
		tone(dest, t, T(190, 60, 0.07, 0.7 * vol));
	} else if (name == "rifle") {
		noiseHit(dest, t, H(0.14, 1.2 * vol, HP, 700 - far * 400));
		noiseHit(dest, t, Q(H(0.04, 1.0 * vol, BP, 4500), 1));
		tone(dest, t, T(140, 40, 0.11, 1.2 * vol));
	} else if (name == "sniper") {
		// a heavy crack, a long rolling tail and the bolt working
		noiseHit(dest, t, H(0.22, 1.5 * vol, HP, 520 - far * 300));
		noiseHit(dest, t, Q(H(0.05, 1.2 * vol, BP, 5200), 1));
		tone(dest, t, T(110, 30, 0.3, 1.5 * vol));
		noiseHit(dest, t + 0.05, Sw(H(0.9, 0.35 * vol, LP, 300), 90));
		noiseHit(dest, t + 0.62, Q(H(0.05, 0.3 * vol, BP, 2600), 3));
		noiseHit(dest, t + 0.8, Q(H(0.05, 0.32 * vol, BP, 2100), 3));
	} else if (name == "shotgun") {
		noiseHit(dest, t, Sw(H(0.35, 1.5 * vol, LP, 400), 150));
		noiseHit(dest, t, H(0.12, 1.1 * vol, HP, 1500));
		tone(dest, t, T(110, 35, 0.25, 1.4 * vol));
		noiseHit(dest, t + 0.45, Q(H(0.05, 0.3 * vol, BP, 2500), 3));
		noiseHit(dest, t + 0.58, Q(H(0.05, 0.35 * vol, BP, 2000), 3));
	} else if (name == "rpg") {
		noiseHit(dest, t, At(Q(Sw(H(0.9, 1.0 * vol, BP, 300), 3000), 1.5), 0.02));
		tone(dest, t, T(80, 40, 0.3, 0.9 * vol));
	} else if (name == "explosion") {
		// bigger blasts: deeper, longer rumble and more debris rattling down afterwards
		const double sz = Clamp(IsSet(opts.size) ? opts.size : 1, 0.3, 4.6), lo = 1 / std::sqrt(sz);
		noiseHit(dest, t, At(Bf(Sw(H(2.5 * std::sqrt(sz), 2.0 * vol, LP, 3000 * lo), 80 * lo), brown), 0.005));
		noiseHit(dest, t, At(Sw(H(0.6, 1.5 * vol, LP, 1200 * Min(1.4, lo)), 200), 0.002));
		tone(dest, t, T(70 * Min(1.3, lo), 25 * Min(1.2, lo), 1.2 * std::sqrt(sz), 2.0 * vol));
		if (sz > 1.8) tone(dest, t + 0.05, T(38, 18, 2.2 * std::sqrt(sz), 1.4 * vol));
		const int nd = (int)std::round(6 * Min(3, sz));
		for (int i = 0; i < nd; i++) { const double at = t + 0.2 + rand01() * 1.2 * std::sqrt(sz); noiseHit(dest, at, Q(H(0.05, 0.3 * vol, BP, 2000 + rand01() * 3000), 2)); }
	} else if (name == "crash") {
		noiseHit(dest, t, Sw(H(0.35, 1.3 * vol, LP, 600), 150));
		noiseHit(dest, t, Q(H(0.2, 0.8 * vol, BP, 2500), 1.5));
		for (double f : { 233.0, 347.0, 519.0, 787.0, 1123.0 }) { const double f0 = f * rand(0.9, 1.1), dur = rand(0.2, 0.6); tone(dest, t, T(f0, 0, dur, 0.12 * vol, TRI)); }
		for (int i = 0; i < 4; i++) { const double at = t + 0.05 + rand01() * 0.3; noiseHit(dest, at, Q(H(0.04, 0.25 * vol, BP, 4000), 4)); }
	} else if (name == "glass") {
		noiseHit(dest, t, H(0.25, 0.9 * vol, HP, 5000));
		for (int i = 0; i < 8; i++) { const double at = t + rand01() * 0.35, f0 = rand(2500, 6000), dur = rand(0.04, 0.12); tone(dest, at, T(f0, 0, dur, 0.07 * vol, TRI)); }
	} else if (name == "metalhit") {
		for (double f : { 412.0, 689.0, 1033.0 }) tone(dest, t, T(f, 0, 0.3, 0.12 * vol, TRI));
		noiseHit(dest, t, H(0.08, 0.5 * vol, BP, 1800));
	} else if (name == "punch") {
		tone(dest, t, T(120, 50, 0.1, 1.0 * vol));
		noiseHit(dest, t, H(0.06, 0.7 * vol, LP, 800));
	} else if (name == "bat") {
		tone(dest, t, T(180, 60, 0.12, 1.0 * vol));
		noiseHit(dest, t, H(0.08, 0.8 * vol, BP, 1200));
	} else if (name == "stab") {
		noiseHit(dest, t, Q(H(0.12, 0.6 * vol, BP, 900), 2));
		tone(dest, t, T(90, 60, 0.08, 0.5 * vol));
	} else if (name == "swoosh") {
		noiseHit(dest, t, At(Q(Sw(H(0.22, 0.35 * vol, BP, 500), 2500), 2), 0.05));
	} else if (name == "bark") {
		const double f = vol > 0.9 ? 420 : 560; // (big dogs bark lower)
		for (int i = 0; i < 2; i++) {
			tone(dest, t + i * 0.18, T(f * 1.25, f * 0.7, 0.1, 0.45 * vol, SAW, 0.004));
			noiseHit(dest, t + i * 0.18, Q(H(0.08, 0.35 * vol, BP, f * 2.2), 1.5));
		}
	} else if (name == "whistle") {
		tone(dest, t, T(1500, 2300, 0.16, 0.18 * vol, SINE, 0.02));
		tone(dest, t + 0.2, T(2300, 1700, 0.22, 0.18 * vol, SINE, 0.02));
	} else if (name == "yelp") {
		tone(dest, t, T(1300, 600, 0.22, 0.4 * vol, TRI));
	} else if (name == "meow") {
		tone(dest, t, T(620, 900, 0.18, 0.25 * vol, TRI));
		tone(dest, t + 0.17, T(900, 520, 0.3, 0.22 * vol, TRI));
	} else if (name == "moo") {
		tone(dest, t, T(120, 95, 1.1, 0.35 * vol, SAW, 0.15));
		tone(dest, t, T(240, 190, 1.1, 0.12 * vol, TRI, 0.15));
	} else if (name == "flap") {
		for (int i = 0; i < 5; i++) noiseHit(dest, t + i * 0.07, Q(H(0.05, 0.25 * vol, BP, 700), 1));
	} else if (name == "bodyhit") {
		tone(dest, t, T(80, 40, 0.2, 1.2 * vol));
		noiseHit(dest, t, H(0.15, 0.6 * vol, LP, 500));
	} else if (name == "bulletflesh") {
		noiseHit(dest, t, H(0.05, 0.4 * vol, LP, 700));
	} else if (name == "bulletmetal") {
		tone(dest, t, T(rand(1500, 2500), 0, 0.08, 0.2 * vol, TRI));
		noiseHit(dest, t, H(0.03, 0.4 * vol, HP, 3000));
	} else if (name == "ricochet") {
		const double f0 = rand(2200, 3200), f1 = rand(600, 900);
		tone(dest, t, T(f0, f1, 0.35, 0.12 * vol, SINE));
	} else if (name == "clink") {
		tone(dest, t, T(2400, 0, 0.06, 0.2 * vol, TRI));
	} else if (name == "dryfire") {
		noiseHit(dest, t, Q(H(0.03, 0.4 * vol, BP, 3000), 5));
	} else if (name == "reload") {
		noiseHit(dest, t, Q(H(0.04, 0.5 * vol, BP, 2500), 4));
		noiseHit(dest, t + 0.35, Q(H(0.05, 0.6 * vol, BP, 1800), 4));
	} else if (name == "switch") {
		noiseHit(dest, t, Q(H(0.04, 0.35 * vol, BP, 2200), 3));
	} else if (name == "doorOpen") {
		noiseHit(dest, t, H(0.08, 0.4 * vol, LP, 600));
	} else if (name == "doorClose") {
		tone(dest, t, T(110, 70, 0.12, 0.8 * vol));
		noiseHit(dest, t, H(0.08, 0.5 * vol, LP, 900));
	} else if (name == "locked") {
		tone(dest, t, T(300, 0, 0.08, 0.2 * vol, SQUARE));
	} else if (name == "trainhorn") {
		// a three-chime air horn, a little detuned
		BiquadFilterNode* f = ctx->createBiquadFilter(); f->type = LP; f->frequency.setValue(1500); f->connect(dest);
		for (double fr : { 311.0, 370.0, 466.0 }) for (double d : { -2.0, 2.0 }) tone(f, t, T(fr + d, 0, 1.1, 0.07 * vol, SAW, 0.06));
		f->expires = ctx->scheduledEnd + 0.1;
	} else if (name == "horn") {
		for (double f : { 392.0, 494.0 }) tone(dest, t, T(f, 0, 0.5, 0.18 * vol, SQUARE, 0.01));
	} else if (name == "pickup") {
		const int ns[4] = { 0, 4, 7, 12 };
		for (int i = 0; i < 4; i++) tone(dest, t + i * 0.06, T(523.25 * std::pow(2.0, ns[i] / 12.0), 0, 0.18, 0.25 * vol, TRI));
	} else if (name == "cash") {
		const int ns[3] = { 12, 16, 19 };
		for (int i = 0; i < 3; i++) tone(dest, t + i * 0.05, T(523.25 * std::pow(2.0, ns[i] / 12.0), 0, 0.12, 0.2 * vol, SQUARE));
	} else if (name == "ui") {
		tone(dest, t, T(880, 0, 0.05, 0.15 * vol, TRI));
	} else if (name == "checkpoint") {
		const int ns[2] = { 0, 7 };
		for (int i = 0; i < 2; i++) tone(dest, t + i * 0.08, T(659 * std::pow(2.0, ns[i] / 12.0), 0, 0.2, 0.25 * vol, TRI));
	} else if (name == "passed") {
		const double seq[8][2] = { { 0, 0.0 }, { 4, 0.15 }, { 7, 0.3 }, { 12, 0.45 }, { 7, 0.75 }, { 12, 0.9 }, { 16, 1.05 }, { 19, 1.2 } };
		for (const auto& s : seq) {
			tone(dest, t + s[1], T(392 * std::pow(2.0, s[0] / 12), 0, 0.35, 0.22 * vol, SAW, 0.01));
			tone(dest, t + s[1], T(196 * std::pow(2.0, s[0] / 12), 0, 0.35, 0.18 * vol, TRI, 0.01));
		}
		tone(dest, t + 1.4, T(392 * 2, 0, 1.2, 0.25 * vol, SAW, 0.02));
	} else if (name == "failed") {
		const double seq[3][2] = { { 0, 0 }, { -3, 0.3 }, { -7, 0.6 } };
		for (const auto& s : seq) tone(dest, t + s[1], T(220 * std::pow(2.0, s[0] / 12), 0, 0.5, 0.25 * vol, SAW));
	} else if (name == "wasted") {
		tone(dest, t, T(110, 55, 2.5, 0.5 * vol, SAW));
		tone(dest, t, T(116, 58, 2.5, 0.4 * vol, SAW));
	} else if (name == "pop") {
		noiseHit(dest, t, Sw(H(0.12, 1.1 * vol, LP, 900), 200));
		noiseHit(dest, t + 0.05, H(0.9, 0.25 * vol, HP, 3000));
	} else if (name == "alarm") {
		for (int k = 0; k < 3; k++) tone(dest, t + k * 0.5, T(520, 760, 0.42, 0.18 * vol, SAW));
	} else if (name == "wanted") {
		tone(dest, t, T(740, 0, 0.1, 0.2 * vol, SQUARE));
		tone(dest, t + 0.12, T(988, 0, 0.12, 0.2 * vol, SQUARE));
	} else if (name == "footstep") {
		noiseHit(dest, t, Q(H(0.05, 0.12 * vol, BP, 1000), 1.5));
	} else if (name == "splash") {
		noiseHit(dest, t, Bf(Sw(H(0.8, 0.8 * vol, LP, 800), 200), pink));
	} else if (name == "thunder") {
		noiseHit(dest, t, At(Bf(Sw(H(4, 1.2 * vol, LP, 400), 60), brown), 0.1));
	}
}

// ------------------------------------------------------------------ looping sounds
int Audio::loop(const std::string& name, const V3& pos) {
	if (!enabled) return 0;
	std::lock_guard<std::mutex> lock(mutex);
	return loopLocked(name, pos);
}
int Audio::loopLocked(const std::string& name, const V3& pos) {
	Loop L;
	L.pan = panner(pos, 15, 600);
	L.pan->connect(sfx);
	L.g = ctx->createGain(); L.g->gain.setValue(0.8); L.g->connect(L.pan);
	L.nodes = { L.pan, L.g };
	if (name == "heli") {
		BufferSourceNode* s = src(brown, true);
		BiquadFilterNode* f = ctx->createBiquadFilter(); f->type = LP; f->frequency.setValue(500);
		GainNode* am = ctx->createGain(); am->gain.setValue(0.5);
		OscillatorNode* lfo = ctx->createOscillator(); lfo->frequency.setValue(14);
		GainNode* lg = ctx->createGain(); lg->gain.setValue(0.5);
		lfo->connect(lg); lg->connect(am->gain);
		s->connect(f)->connect(am)->connect(L.g);
		s->start(); lfo->start();
		L.nodes.insert(L.nodes.end(), { s, f, am, lfo, lg });
	} else if (name == "siren") {
		OscillatorNode* o = ctx->createOscillator(); o->type = SAW;
		OscillatorNode* lfo = ctx->createOscillator(); lfo->frequency.setValue(0.35);
		GainNode* lg = ctx->createGain(); lg->gain.setValue(350);
		o->frequency.setValue(950);
		lfo->connect(lg); lg->connect(o->frequency);
		BiquadFilterNode* f = ctx->createBiquadFilter(); f->type = LP; f->frequency.setValue(2200);
		GainNode* gg = ctx->createGain(); gg->gain.setValue(0.15);
		o->connect(f)->connect(gg)->connect(L.g);
		o->start(); lfo->start();
		L.nodes.insert(L.nodes.end(), { o, lfo, lg, f, gg });
	} else if (name == "fire") {
		BufferSourceNode* s = src(pink, true);
		BiquadFilterNode* f = ctx->createBiquadFilter(); f->type = LP; f->frequency.setValue(900);
		s->connect(f)->connect(L.g); s->start();
		L.nodes.insert(L.nodes.end(), { s, f });
	}
	const int id = nextLoop++;
	loops[id] = L;
	return id;
}

void Audio::loopPos(int id, const V3& p) { std::lock_guard<std::mutex> lock(mutex); loopPosLocked(id, p); }
void Audio::loopPosLocked(int id, const V3& p) {
	auto it = loops.find(id);
	if (it != loops.end() && p.finite()) { it->second.pan->x = p.x; it->second.pan->y = p.y; it->second.pan->z = p.z; }
}
void Audio::loopVol(int id, double v) {
	std::lock_guard<std::mutex> lock(mutex);
	auto it = loops.find(id);
	if (it != loops.end()) it->second.g->gain.setTargetAtTime(v, ctx->currentTime(), 0.1);
}
void Audio::loopStop(int id) { std::lock_guard<std::mutex> lock(mutex); loopStopLocked(id); }
void Audio::loopStopLocked(int id) {
	auto it = loops.find(id);
	if (it == loops.end()) return;
	for (Node* n : it->second.nodes) n->expires = ctx->currentTime() - 1;
	it->second.g->disconnect(); it->second.pan->disconnect();
	loops.erase(it);
}

// ------------------------------------------------------------------ recorded samples and the muffle
// (the browser game plays the user's own recording of the WASTED stinger; this build has no recordings, so
// the callers fall back to the synthesised one)
bool Audio::playSample(const std::string&, double) { return false; }
void Audio::stopSample(const std::string&, double) {}

void Audio::muffle(bool on) {
	if (!enabled) return;
	std::lock_guard<std::mutex> lock(mutex);
	const double t = ctx->currentTime();
	Param& f = worldFilter->frequency;
	f.cancelScheduledValues(t); f.setValueAtTime(f.value(), t);
	f.exponentialRampToValueAtTime(on ? 420 : 20000, t + (on ? 0.6 : 1.2));
	Param& m = music->gain;
	m.cancelScheduledValues(t); m.setValueAtTime(m.value(), t);
	m.linearRampToValueAtTime(on ? musicVolume * 0.12 : musicVolume * 0.55, t + (on ? 0.5 : 1.5));
}

double Audio::clock() const { return enabled && ctx ? ctx->currentTime() : -1; }

// ------------------------------------------------------------------ the player's vehicle
void Audio::setupVehicleAudio() {
	GainNode* out = ctx->createGain(); out->gain.setValue(0); out->connect(sfx);
	OscillatorNode* o1 = ctx->createOscillator(); o1->type = SAW;
	OscillatorNode* o2 = ctx->createOscillator(); o2->type = SQUARE;
	OscillatorNode* o3 = ctx->createOscillator(); o3->type = SINE;
	BiquadFilterNode* f = ctx->createBiquadFilter(); f->type = LP; f->frequency.setValue(400); f->Q.setValue(3);
	GainNode* g1 = ctx->createGain(); g1->gain.setValue(0.35);
	GainNode* g2 = ctx->createGain(); g2->gain.setValue(0.2);
	GainNode* g3 = ctx->createGain(); g3->gain.setValue(0.5);
	o1->connect(g1)->connect(f); o2->connect(g2)->connect(f); o3->connect(g3)->connect(f);
	BufferSourceNode* rumble = src(brown, true);
	BiquadFilterNode* rf = ctx->createBiquadFilter(); rf->type = LP; rf->frequency.setValue(180);
	GainNode* rg = ctx->createGain(); rg->gain.setValue(0.4);
	rumble->connect(rf)->connect(rg)->connect(f);
	f->connect(out);
	o1->start(); o2->start(); o3->start(); rumble->start();
	// tyres
	BufferSourceNode* tire = src(noise, true);
	BiquadFilterNode* tf = ctx->createBiquadFilter(); tf->type = BP; tf->frequency.setValue(1800); tf->Q.setValue(6);
	GainNode* tg = ctx->createGain(); tg->gain.setValue(0);
	tire->connect(tf)->connect(tg)->connect(sfx); tire->start();
	// wind
	BufferSourceNode* wind = src(pink, true);
	BiquadFilterNode* wf = ctx->createBiquadFilter(); wf->type = LP; wf->frequency.setValue(600);
	GainNode* wg = ctx->createGain(); wg->gain.setValue(0);
	wind->connect(wf)->connect(wg)->connect(sfx); wind->start();
	// horn
	OscillatorNode* h1 = ctx->createOscillator(); h1->type = SQUARE; h1->frequency.setValue(392);
	OscillatorNode* h2 = ctx->createOscillator(); h2->type = SQUARE; h2->frequency.setValue(494);
	BiquadFilterNode* hf = ctx->createBiquadFilter(); hf->type = LP; hf->frequency.setValue(1500);
	GainNode* hg = ctx->createGain(); hg->gain.setValue(0);
	h1->connect(hf); h2->connect(hf); hf->connect(hg)->connect(sfx); h1->start(); h2->start();
	veh.out = out; veh.o1 = o1; veh.o2 = o2; veh.o3 = o3; veh.f = f; veh.tg = tg; veh.tf = tf; veh.wg = wg; veh.hg = hg;
	veh.rpm = 800; veh.gear = 1;
}

// aircraft engines for the player's plane, jet or helicopter (built on first use)
void Audio::setupAirAudio() {
	air = std::make_unique<Air>();
	Air& A = *air;
	A.out = ctx->createGain(); A.out->gain.setValue(0); A.out->connect(sfx);
	// turbine: a broadband roar and a whine
	BufferSourceNode* roar = src(noise, true);
	A.rf = ctx->createBiquadFilter(); A.rf->type = BP; A.rf->frequency.setValue(600); A.rf->Q.setValue(0.6);
	A.rg = ctx->createGain(); A.rg->gain.setValue(0);
	roar->connect(A.rf)->connect(A.rg)->connect(A.out); roar->start();
	BufferSourceNode* rumble = src(brown, true);
	BiquadFilterNode* bf = ctx->createBiquadFilter(); bf->type = LP; bf->frequency.setValue(160);
	A.bg = ctx->createGain(); A.bg->gain.setValue(0);
	rumble->connect(bf)->connect(A.bg)->connect(A.out); rumble->start();
	A.whine = ctx->createOscillator(); A.whine->type = SINE; A.whine->frequency.setValue(2500);
	A.wg = ctx->createGain(); A.wg->gain.setValue(0);
	A.whine->connect(A.wg)->connect(A.out); A.whine->start();
	// piston and propeller buzz
	A.prop = ctx->createOscillator(); A.prop->type = SAW; A.prop->frequency.setValue(40);
	A.prop2 = ctx->createOscillator(); A.prop2->type = SQUARE; A.prop2->frequency.setValue(20);
	A.pf = ctx->createBiquadFilter(); A.pf->type = LP; A.pf->frequency.setValue(500); A.pf->Q.setValue(2);
	A.pg = ctx->createGain(); A.pg->gain.setValue(0);
	GainNode* pg2 = ctx->createGain(); pg2->gain.setValue(0.5);
	A.prop->connect(A.pf); A.prop2->connect(pg2)->connect(A.pf); A.pf->connect(A.pg)->connect(A.out); A.prop->start(); A.prop2->start();
	// rotor chop: low noise amplitude-modulated at the blade-pass rate
	BufferSourceNode* rot = src(brown, true);
	A.rof = ctx->createBiquadFilter(); A.rof->type = LP; A.rof->frequency.setValue(420);
	GainNode* am = ctx->createGain(); am->gain.setValue(0.45);
	A.lfo = ctx->createOscillator(); A.lfo->type = SINE; A.lfo->frequency.setValue(12);
	GainNode* lg = ctx->createGain(); lg->gain.setValue(0.55);
	A.lfo->connect(lg); lg->connect(am->gain);
	A.rtg = ctx->createGain(); A.rtg->gain.setValue(0);
	rot->connect(A.rof)->connect(am)->connect(A.rtg)->connect(A.out); rot->start(); A.lfo->start();
	BufferSourceNode* turb = src(pink, true);
	BiquadFilterNode* tf = ctx->createBiquadFilter(); tf->type = BP; tf->frequency.setValue(1400); tf->Q.setValue(1.2);
	A.tg = ctx->createGain(); A.tg->gain.setValue(0);
	turb->connect(tf)->connect(A.tg)->connect(A.out); turb->start();
}

void Audio::airUpdate(Vehicle* pv, double t) {
	if (!air) setupAirAudio();
	Air& A = *air;
	const std::string& k = pv->def.kind;
	double s = 0;
	if (const AirVehicle* a = dynamic_cast<const AirVehicle*>(pv)) s = a->spool;
	auto set = [&](Param& p, double v, double tc = 0.08) { p.setTargetAtTime(v, t, tc); };
	set(A.out->gain, pv->isWrecked() ? 0 : 1, 0.2);
	const bool jet = k == "jet", plane = k == "plane", heli = k == "heli";
	set(A.rg->gain, jet ? 0.04 + s * 0.3 : plane ? 0.015 : 0);
	set(A.rf->frequency, 350 + s * 1500);
	set(A.bg->gain, jet ? s * 0.5 : heli ? s * 0.25 : plane ? s * 0.15 : 0);
	set(A.wg->gain, jet ? 0.004 + s * 0.012 : heli ? s * 0.004 : 0);
	set(A.whine->frequency, (jet ? 1800 : 3200) + s * (jet ? 4200 : 1800));
	const bool big = pv->def.L > 20;
	set(A.prop->frequency, (big ? 25 : 38) + s * (big ? 45 : 80));
	set(A.prop2->frequency, ((big ? 25 : 38) + s * (big ? 45 : 80)) * 0.5);
	set(A.pf->frequency, 200 + s * 900);
	set(A.pg->gain, plane ? 0.03 + s * 0.13 : 0);
	set(A.rtg->gain, heli ? s * 0.55 : 0);
	set(A.lfo->frequency, 2 + s * (pv->type == "warhawk" ? 17 : 12));
	set(A.rof->frequency, 250 + s * 300);
	set(A.tg->gain, heli ? s * 0.035 : 0);
}

// ------------------------------------------------------------------ ambience
void Audio::setupAmbience() {
	BufferSourceNode* city = src(brown, true);
	BiquadFilterNode* cf = ctx->createBiquadFilter(); cf->type = LP; cf->frequency.setValue(350);
	cityGain = ctx->createGain(); cityGain->gain.setValue(0.25);
	city->connect(cf)->connect(cityGain)->connect(amb); city->start();
	BufferSourceNode* waves = src(pink, true);
	BiquadFilterNode* wf = ctx->createBiquadFilter(); wf->type = LP; wf->frequency.setValue(700);
	waveGain = ctx->createGain(); waveGain->gain.setValue(0);
	OscillatorNode* lfo = ctx->createOscillator(); lfo->frequency.setValue(0.12);
	GainNode* lg = ctx->createGain(); lg->gain.setValue(0.4);
	GainNode* am = ctx->createGain(); am->gain.setValue(0.6);
	lfo->connect(lg); lg->connect(am->gain); lfo->start();
	waves->connect(wf)->connect(am)->connect(waveGain)->connect(amb); waves->start();
	BufferSourceNode* rain = src(noise, true);
	BiquadFilterNode* rf = ctx->createBiquadFilter(); rf->type = HP; rf->frequency.setValue(2500);
	rainGain = ctx->createGain(); rainGain->gain.setValue(0);
	rain->connect(rf)->connect(rainGain)->connect(amb); rain->start();
	birdT = 1; cricketT = 1;
}

void Audio::chirp(bool night) {
	const double t = ctx->currentTime() + 0.01;
	const V3 cam = game.rig.camPos;
	const double px = cam.x + rand(-30, 30), py = cam.y + rand(3, 12), pz = cam.z + rand(-30, 30);
	PannerNode* pan = panner(V3(px, py, pz), 8);
	pan->connect(amb);
	ctx->scheduledEnd = ctx->currentTime();
	if (night) {
		for (int i = 0; i < 6; i++) { Tone o; o.f0 = 4200 + rand(-100, 100); o.dur = 0.04; o.vol = 0.04; tone(pan, t + i * 0.07, o); }
	} else {
		const double base = rand(2200, 3800);
		// (the browser game draws the count again on every pass of the loop)
		for (int i = 0; i < 2 + (int)std::floor(rand01() * 3); i++) { Tone o; o.f0 = base * rand(0.9, 1.2); o.f1 = base * rand(1.2, 1.6); o.dur = 0.08; o.vol = 0.05; tone(pan, t + i * 0.12, o); }
	}
	pan->expires = ctx->scheduledEnd + 0.1;
}

// ------------------------------------------------------------------ every frame
void Audio::update(double dt) {
	if (!enabled) return;
	std::lock_guard<std::mutex> lock(mutex);
	const double t = ctx->currentTime();
	// the listener
	const V3 cam = game.rig.camPos;
	const V3 f = game.rig.camQuat.rotate(V3(0, 0, -1)), u = game.rig.camQuat.rotate(V3(0, 1, 0));
	ctx->lx = cam.x; ctx->ly = cam.y; ctx->lz = cam.z;
	ctx->fx = f.x; ctx->fy = f.y; ctx->fz = f.z; ctx->ux = u.x; ctx->uy = u.y; ctx->uz = u.z;

	// the player's vehicle
	Vehicle* pv = game.player->vehicle;
	Veh& V = veh;
	const bool flying = pv && pv->def.aircraft;
	if (flying) airUpdate(pv, t);
	else if (air) air->out->gain.setTargetAtTime(0, t, 0.3);
	if (pv && !pv->isWrecked() && !flying && !pv->def.pedal && !pv->def.board) {
		const double sp = std::fabs(pv->speed());
		const double top = pv->def.top;
		// a fake gearbox
		const double ratio = sp / top;
		const double gears[6] = { 0, 0.18, 0.34, 0.52, 0.72, 1.01 };
		int gear = 1; while (gear < 5 && ratio > gears[gear]) gear++;
		const double lo = gears[gear - 1], hi = gears[gear];
		double rpm = 900 + ((ratio - lo) / (hi - lo)) * 5200;
		if (pv->wheelspin > 0.1 || (pv->input.throttle > 0.5 && sp < 3)) rpm = Max(rpm, 3500 + pv->input.throttle * 2500);
		if (pv->airborne) rpm = Max(rpm, 5000 * pv->input.throttle + 1500);
		V.rpm += (rpm - V.rpm) * Min(1, dt * 8);
		const double heavy = pv->def.mass > 2000 ? 0.7 : 1;
		const double kind = pv->def.kind == "boat" ? (pv->def.boat == "jetski" ? 1.4 : 0.6) : !pv->def.bike.empty() ? 1.75 : pv->def.body == "super" ? 1.3 : pv->def.body == "muscle" ? 0.8 : 1;
		const double base = (V.rpm / 60) * 0.5 * heavy * kind;
		V.o1->frequency.setTargetAtTime(base, t, 0.03);
		V.o2->frequency.setTargetAtTime(base * 0.5, t, 0.03);
		V.o3->frequency.setTargetAtTime(base * 0.25, t, 0.03);
		V.f->frequency.setTargetAtTime(250 + pv->input.throttle * 900 + V.rpm * 0.1, t, 0.05);
		V.out->gain.setTargetAtTime(0.13 + pv->input.throttle * 0.12, t, 0.05);
		const double skid = pv->skid ? Clamp(pv->slipRear / 8 + pv->wheelspin, 0.2, 1) : 0;
		V.tg->gain.setTargetAtTime(skid * 0.18, t, 0.04);
		V.tf->frequency.setTargetAtTime(1400 + sp * 20, t, 0.1);
		V.wg->gain.setTargetAtTime(Clamp(sp / 50, 0, 1) * 0.12, t, 0.2);
		V.hg->gain.setTargetAtTime(pv->horn ? 0.08 : 0, t, 0.01);
	} else if (pv && pv->def.board && !pv->airborne) {
		// urethane on concrete: a low rolling rumble
		V.out->gain.setTargetAtTime(0, t, 0.1);
		V.tg->gain.setTargetAtTime(Clamp(pv->speedAbs() / pv->def.top, 0, 1) * 0.1, t, 0.05);
		V.tf->frequency.setTargetAtTime(300 + pv->speedAbs() * 25, t, 0.1);
		V.wg->gain.setTargetAtTime(Clamp(pv->speedAbs() / 30, 0, 1) * 0.08, t, 0.2);
		V.hg->gain.setTargetAtTime(0, t, 0.01);
	} else {
		V.out->gain.setTargetAtTime(0, t, 0.1);
		V.tg->gain.setTargetAtTime(0, t, 0.05);
		V.wg->gain.setTargetAtTime(flying && !pv->isWrecked() ? Clamp(pv->vel.length() / 120, 0, 1) * 0.16 : 0, t, 0.2);
		V.hg->gain.setTargetAtTime(0, t, 0.01);
	}
	// sirens: loops on the nearest two cars with their sirens going
	std::vector<Vehicle*> sirenCars;
	for (const auto& v : game.vehicles.list) if (v->sirenOn && !v->sirenMute && !v->isWrecked()) sirenCars.push_back(v.get());
	std::stable_sort(sirenCars.begin(), sirenCars.end(), [&](Vehicle* a, Vehicle* b) { return a->pos.distanceToSq(cam) < b->pos.distanceToSq(cam); });
	if (sirenCars.size() > 2) sirenCars.resize(2);
	while (sirens.size() < sirenCars.size()) sirens.push_back(loopLocked("siren", cam));
	while (sirens.size() > sirenCars.size()) { loopStopLocked(sirens.back()); sirens.pop_back(); }
	for (size_t i = 0; i < sirenCars.size(); i++) loopPosLocked(sirens[i], sirenCars[i]->pos);
	// ambience
	const auto& e = game.env;
	const double night = e.night;
	const std::string d = game.map.DistrictAt(cam.x, cam.z);
	cityGain->gain.setTargetAtTime((d == "hills" ? 0.08 : 0.22) * (1 - night * 0.4), t, 0.5);
	const double beachDist = Max(0, cam.z - 560);
	waveGain->gain.setTargetAtTime(Clamp(beachDist / 120, 0, 1) * 0.35, t, 0.5);
	rainGain->gain.setTargetAtTime(e.rain * 0.18, t, 0.5);
	birdT -= dt; cricketT -= dt;
	if (!pv && night < 0.3 && birdT <= 0) { birdT = rand(1.5, 5); if (d == "hills" || d == "hood" || d == "westside" || rand01() < 0.3) chirp(false); }
	if (!pv && night > 0.6 && cricketT <= 0) { cricketT = rand(0.8, 2.5); chirp(true); }
	// thunder a moment after the flash
	if (e.lightning > 0.95 && thunderT < 0) thunderT = rand(0.3, 2.5);
	if (thunderT >= 0) { thunderT -= dt; if (thunderT < 0) playLocked("thunder", 0.8); }
	// the radio, only while in a vehicle (not on a bike); N / D-pad right changes the station (hud.js)
	if (game.input.hit("radio") && pv && pv->def.bike.empty()) radioNextLocked();
	radioUpdate(dt, pv && pv->def.bike.empty() && radioOn);
}

// ------------------------------------------------------------------ the radio
void Audio::radioNext() {
	if (!enabled) return;
	std::lock_guard<std::mutex> lock(mutex);
	radioNextLocked();
}
void Audio::radioNextLocked() {
	radio.station = (radio.station + 1) % (int)Stations().size();
	newSong();
	radio.nextTime = ctx->currentTime() + 0.1;
	const Station& st = Stations()[radio.station];
	if (game.hud) game.hud->showRadio(std::string(st.style) == "off" ? "Radio Off" : st.name, std::string(st.style) == "off" ? "" : st.genre);
}

std::string Audio::radioLabel() const {
	const Station& st = Stations()[radio.station];
	return std::string(st.style) == "off" ? "Radio Off" : std::string(st.name) + "\n" + st.genre;
}

void Audio::newSong() {
	const std::string style = Stations()[radio.station].style;
	auto pick = [&](const std::vector<int>& v) { return v[(size_t)std::floor(rand01() * v.size())]; };
	auto pickP = [&](const std::vector<std::vector<int>>& v) { return v[(size_t)std::floor(rand01() * v.size())]; };
	Song S;
	S.key = 36 + (int)std::floor(rand01() * 7);
	S.minor = { 0, 2, 3, 5, 7, 8, 10 };
	S.prog = style == "synth" ? pickP({ { 0, 5, 3, 4 }, { 0, 3, 5, 4 }, { 5, 3, 0, 4 } }) : style == "soul" ? pickP({ { 0, 3, 4, 3 }, { 0, 5, 3, 4 } }) : pickP({ { 0, 0, 3, 4 }, { 0, 3, 0, 4 }, { 0, 5, 3, 4 } });
	for (int i = 0; i < 32; i++) S.mel.push_back(rand01() < (style == "gfunk" ? 0.35 : 0.5) ? pick({ 0, 2, 4, 7, 9, 11, 12 }) : NONE);
	for (int i = 0; i < 16; i++) { const bool on = rand01() < 0.45 || i % 8 == 0; S.bass.push_back(on ? pick({ 0, 0, 7, 12, 10, 5 }) : NONE); }
	const int K[16] = { 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0 };
	for (int i = 0; i < 16; i++) S.kick.push_back(K[i] || (rand01() < 0.1 && i % 2 == 0) ? 1 : 0);
	S.swing = style == "gfunk" ? 0.12 : style == "soul" ? 0.16 : 0;
	radio.song = S;
	radio.songBars = 0;
	radio.bar = 0; radio.step = 0;
}

void Audio::schedule(double t, double stepDur) {
	const Station& st = Stations()[radio.station];
	const std::string style = st.style;
	const Song& S = radio.song;
	const int s = radio.step;
	Node* out = radio.out;
	auto note = [](double midi) { return 440 * std::pow(2.0, (midi - 69) / 12); };
	const int chordDeg = S.prog[radio.bar % 4];
	const int root = S.key + S.minor[chordDeg % 7];
	const double swingT = s % 2 == 1 ? stepDur * S.swing : 0;
	const double tt = t + swingT;
	auto T = [](double f0, double f1, double dur, double v, OscillatorNode::Type type = SINE, double attack = 0.002) { Tone o; o.f0 = f0; o.f1 = f1; o.dur = dur; o.vol = v; o.type = type; o.attack = attack; return o; };
	auto H = [](double dur, double v, BiquadFilterNode::Type type, double freq, double q = 0.7) { Hit h; h.dur = dur; h.vol = v; h.type = type; h.freq = freq; h.q = q; return h; };
	// drums
	if (S.kick[s]) tone(out, tt, T(130, 42, 0.22, 0.9));
	if (s == 4 || s == 12) {
		noiseHit(out, tt, H(style == "synth" ? 0.25 : 0.16, 0.45, BP, 1800, 0.8));
		tone(out, tt, T(220, 150, 0.08, 0.25, TRI));
		if (style != "gfunk") noiseHit(out, tt, H(0.4, 0.15, HP, 5000));
	}
	if (style == "synth" ? true : s % 2 == 0) noiseHit(out, tt, H(0.03, s % 4 == 2 ? 0.12 : 0.07, HP, 8000));
	if (style == "gfunk" && s == 14 && rand01() < 0.5) noiseHit(out, tt, H(0.15, 0.1, HP, 7000));
	// bass
	const int b = S.bass[s];
	if (b != NONE) {
		const double f = note(root + b - 12);
		tone(out, tt, T(f * (style == "gfunk" ? 1.03 : 1), f, stepDur * (style == "soul" ? 3 : 1.8), style == "synth" ? 0.18 : 0.5, style == "synth" ? SAW : SINE));
	}
	// chords and pads
	if (s == 0 || (style == "soul" && s == 8)) {
		for (int iv : { 0, 3, 7, 10 }) tone(out, tt, T(note(root + 12 + iv), 0, stepDur * 14, style == "synth" ? 0.045 : 0.035, style == "synth" ? SAW : TRI, 0.08));
	}
	if (style == "synth" && s % 2 == 0) {
		const int arp[8] = { 0, 7, 12, 15, 19, 15, 12, 7 };
		tone(out, tt, T(note(root + 24 + arp[(s / 2) % 8]), 0, stepDur * 0.9, 0.05, SQUARE));
	}
	// the lead melody (in bars 2 to 7 of every 8)
	const int mi = (radio.bar % 2) * 16 + s;
	const int m = S.mel[mi];
	if (m != NONE && (radio.bar % 8) >= 2) {
		const double f = note(root + 24 + m);
		if (style == "gfunk") tone(out, tt, T(f * 0.94, f, stepDur * 1.8, 0.06, SINE, 0.03)); // (a portamento "whistle" lead)
		else if (style == "soul") tone(out, tt, T(f, 0, stepDur * 2.5, 0.06, TRI, 0.02));
		else tone(out, tt, T(f, 0, stepDur * 1.5, 0.05, SAW, 0.01));
	}
}

void Audio::radioUpdate(double, bool inVehicle) {
	const Station& st = Stations()[radio.station];
	const bool on = inVehicle && std::string(st.style) != "off";
	const double now = ctx->currentTime();
	radio.out->gain.setTargetAtTime(on ? 0.6 : 0, now, 0.3);
	if (!on) { radio.nextTime = now + 0.05; return; }
	const double stepDur = 60 / st.bpm / 4;
	if (radio.nextTime < now - 0.2) radio.nextTime = now + 0.05;
	while (radio.nextTime < now + 0.15) {
		schedule(radio.nextTime, stepDur);
		radio.nextTime += stepDur;
		radio.step++;
		if (radio.step >= 16) {
			radio.step = 0; radio.bar++; radio.songBars++;
			if (radio.songBars >= 48) newSong();
		}
	}
}

} // namespace atg
