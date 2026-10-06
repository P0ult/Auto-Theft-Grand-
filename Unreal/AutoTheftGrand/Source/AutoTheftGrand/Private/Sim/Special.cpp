#include "Special.h"
#include "Game.h"
#include "Gameplay.h"

namespace atg {

namespace {
const double DRAIN = 0.11;   // meter per real second while active (about nine seconds from full)
const double FOOT = 0.38, DRIVE = 0.5;
}

Special::Special(Game& g) : game(g) {
	g.events.kill.on([this](Character* killer, Character*, const std::string&, const std::string& part) {
		if (!killer || !killer->isPlayer || active) return;
		add(part == "head" ? 0.1 : 0.04);
	});
	g.events.playerDied.on([this]() { stop(); });
	g.events.busted.on([this]() { stop(); });
}

void Special::add(double x) { meter = Clamp(meter + x, 0, 1); }

bool Special::pressed() const {
	const Input& input = game.input;
	return input.keyHit("CapsLock") || input.keyHit("KeyZ") || (input.gpDown(GP::LS) && input.gpDown(GP::RS) && (input.gpHit(GP::LS) || input.gpHit(GP::RS)));
}

void Special::start() {
	if (active) return;
	if (meter < 0.12) { if (game.time - lastBlocked > 1) { lastBlocked = game.time; game.sound("dryfire"); } return; }
	active = true;
	game.sound("swoosh", 1);
	game.events.specialOn.emit();
}

void Special::stop() {
	if (!active) return;
	active = false;
	game.slowmo.erase("special");
	game.sound("swoosh", 0.6);
}

void Special::update(double dt) {
	Player& p = *game.player;
	const Input& input = game.input;
	const double real = input.frameDt ? input.frameDt : dt;
	const bool playing = game.gameplay && game.gameplay->state == "playing" && !game.cutscene && !p.dead;
	if (playing && input.enabled && !game.menuOpen && !game.phoneOpen && pressed()) { if (active) stop(); else start(); }
	if (!playing) stop();
	if (active) {
		meter -= DRAIN * real;
		if (meter <= 0) { meter = 0; stop(); }
		else game.slowmo["special"] = p.vehicle ? DRIVE : FOOT;
	} else if (playing) {
		// filling up: a trickle all the time, faster at speed and while drifting
		double gain = 0.004;
		const Vehicle* v = p.vehicle;
		if (v && v->def.kind.empty()) {
			const double spd = v->speedAbs();
			if (spd > 22) gain += 0.012 * Min(1, (spd - 22) / 20);
			if (game.gameplay && game.gameplay->drift > 10) gain += 0.03;
			if (v->airborne) gain += 0.02;
		}
		add(gain * real);
	}
	// the look: drained colour and a little extra fringing
	k += ((active ? 1 : 0) - k) * Min(1, real * 6);
	if (!p.dead) {
		game.post.desat = 0.42 * k;
		chroma = 0.0022 + 0.004 * k;
	}
}

double Special::grip() const { return active && game.player->vehicle ? 1.25 : 1; }

} // namespace atg
