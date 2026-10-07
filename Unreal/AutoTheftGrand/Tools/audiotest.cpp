// Renders the game's sounds (Sim/Audio on Sim/WebAudio) without Unreal and prints, for each, its peak and its
// loudness every 50 ms; with an argument, also writes them as WAV files into that folder.
//   Tools/native.sh audiotest.exe audiotest.cpp && ./audiotest.exe [wavdir]
// tools/browser-test/tests/audiocmp.mjs prints the same lines from the browser's Web Audio rendering audio.js.
#include "Sim/Audio.h"
#include "Sim/Game.h"
#include "Sim/Setup.h"
#include "Sim/Vehicles.h"
#include "WorldGen.h"
#include "WorldMeshes.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace atg;

static void WriteWav(const std::string& path, const std::vector<float>& st) {
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return;
	const uint32_t frames = (uint32_t)(st.size() / 2), bytes = frames * 4;
	auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
	fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(2); u32(wa::SR); u32(wa::SR * 4); u16(4); u16(16);
	fwrite("data", 1, 4, f); u32(bytes);
	for (float s : st) { const int16_t v = (int16_t)std::lround(std::max(-1.f, std::min(1.f, s)) * 32767); fwrite(&v, 2, 1, f); }
	fclose(f);
}

// the peak, and the loudness (RMS over both channels, dBFS) in 50 ms windows
static void Report(const char* name, const std::vector<float>& st) {
	double peak = 0; bool bad = false;
	for (float s : st) { if (!std::isfinite(s)) bad = true; peak = std::max(peak, (double)std::abs(s)); }
	printf("%s peak %.2f%s |", name, peak, bad ? " NAN" : "");
	const size_t win = wa::SR / 20 * 2;
	for (size_t i = 0; i + win <= st.size(); i += win) {
		double e = 0;
		for (size_t k = i; k < i + win; k++) e += (double)st[k] * st[k];
		const double db = 10 * std::log10(e / win + 1e-12);
		printf(" %.0f", std::max(-90.0, db));
	}
	printf("\n");
}

int main(int argc, char** argv) {
	const std::string dir = argc > 1 ? argv[1] : "";
	CityMap map;
	RoadMeshes roads = BuildRoadMeshes(map);
	std::vector<PropInstance> props = PlaceProps(map);
	std::map<std::string, PropTemplate> defs = BuildPropTemplates();
	WorldData w; w.map = &map; w.roadPrims = roads.prims; w.roadDecks = roads.decks; w.props = props; w.propDefs = &defs;
	Game g(w);
	InstallSystems(g);
	StartGame(g);
	Audio& a = *g.audioSys;
	a.init();
	auto render = [&](double sec) { std::vector<float> st((size_t)(sec * wa::SR) * 2); a.render(st.data(), (int)(st.size() / 2)); return st; };
	render(0.1);
	auto one = [&](const char* name, double sec, double vol = 1) {
		a.play(name, vol);
		const auto st = render(sec);
		Report(name, st);
		if (!dir.empty()) WriteWav(dir + "/" + name + ".wav", st);
		render(1.0); // (let tails and the reverb die away)
	};
	for (const char* n : { "pistol", "smg", "rifle", "sniper", "shotgun", "rpg", "explosion", "crash", "glass", "metalhit", "punch", "bark", "moo", "pickup", "cash", "passed", "failed", "wasted", "alarm", "trainhorn", "thunder", "splash" })
		one(n, std::string(n) == "thunder" || std::string(n) == "wasted" || std::string(n) == "explosion" ? 4.0 : 1.5);
	printf("nodes after the one-shots: %zu\n", a.ctx->nodeCount());
	// a siren and the helicopter, 20 m away
	const V3 cam = g.rig.camPos;
	int siren = a.loop("siren", cam + V3(20, 0, 0)), heli = a.loop("heli", cam + V3(0, 30, 20));
	{ const auto st = render(3); Report("siren+heli", st); if (!dir.empty()) WriteWav(dir + "/siren_heli.wav", st); }
	a.loopStop(siren); a.loopStop(heli);
	render(0.5);
	// the radio in a car, each station, and the engine revving
	Vehicle* car = g.vehicles.spawn("zenith", g.player->pos.x + 3, g.player->pos.z, 0);
	g.vehicles.seatNow(&*g.player, car, 0);
	for (int st = 0; st < 3; st++) {
		std::vector<float> all;
		for (int i = 0; i < 90; i++) { g.frame(1.0 / 30); const auto s = render(1.0 / 30); all.insert(all.end(), s.begin(), s.end()); }
		char nm[32]; snprintf(nm, sizeof nm, "radio%d", st);
		Report(nm, all);
		if (!dir.empty()) WriteWav(dir + "/" + nm + ".wav", all);
		a.radioNext();
	}
	g.input.KeyDown("KeyW");
	{
		std::vector<float> all;
		for (int i = 0; i < 120; i++) { g.frame(1.0 / 30); const auto s = render(1.0 / 30); all.insert(all.end(), s.begin(), s.end()); }
		Report("engine", all);
		if (!dir.empty()) WriteWav(dir + "/engine.wav", all);
	}
	printf("nodes at the end: %zu\n", a.ctx->nodeCount());
}
