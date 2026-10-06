#include "Hud.h"
#include "Game.h"

namespace atg {

namespace {
// CSS's default transition timing ("ease" = cubic-bezier(0.25, 0.1, 0.25, 1))
double CssEase(double x) {
	if (x <= 0) return 0;
	if (x >= 1) return 1;
	const double x1 = 0.25, y1 = 0.1, x2 = 0.25, y2 = 1;
	auto bx = [&](double t) { const double u = 1 - t; return 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t; };
	auto by = [&](double t) { const double u = 1 - t; return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t; };
	double lo = 0, hi = 1, t = x;
	for (int i = 0; i < 30; i++) { t = (lo + hi) / 2; if (bx(t) < x) lo = t; else hi = t; }
	return by(t);
}
// utils.js formatMoney: eight digits, zero padded
std::string FormatMoney(double n) {
	char buf[32];
	std::snprintf(buf, sizeof buf, "%08lld", (long long)Max(0, std::floor(std::fabs(n))));
	return std::string(n < 0 ? "-$" : "$") + buf;
}
}

double HudModel::Fade::value() const {
	if (dur <= 0) return to;
	return from + (to - from) * CssEase(t / dur);
}

HudModel::HudModel(Game& g) : game(g) {}

void HudModel::help(const std::string& text, double seconds) { helpLine.text = text; helpLine.t = seconds; }

void HudModel::speech(Character* who, const std::string& text) {
	if (speeches.size() > 5) speeches.erase(speeches.begin());
	speeches.push_back({ Ref<Character>(who), text, 3 });
}

void HudModel::bigMessage(const std::string& text, const std::string& style, double dur, const std::string& sub) { big = { text, style, sub, dur }; }

void HudModel::subtitle(const std::string& text, const std::string& speaker, double dur) { subs.text = text; subsSpeaker = speaker; subs.t = dur; }

void HudModel::objective(const std::string& text, double dur) { subs.text = text; subsSpeaker.clear(); subs.t = dur; objectiveText = text; }

void HudModel::setBar(const std::string* label, double v, const std::string& color) {
	if (!label) { bar.on = false; return; }
	bar = { true, *label, Clamp(v, 0, 1), color };
}

void HudModel::moneyFlash(double amount) {
	std::string s = FormatMoney(std::fabs(amount));
	// (.replace('$0000', '$').replace(/^\$0+/, '$'))
	const size_t p = s.find("$0000");
	if (p != std::string::npos) s.replace(p, 5, "$");
	if (!s.empty() && s[0] == '$') { size_t k = 1; while (k < s.size() && s[k] == '0') k++; if (k > 1) s = "$" + s.substr(k); }
	money.text = std::string(amount >= 0 ? "+" : "-") + s;
	money.t = 2;
}

void HudModel::dispatch(const std::string& text, const std::string& where) {
	if (text.empty()) return;
	dispatchLine.text = text; dispatchWhere = where; dispatchLine.t = 5;
}

void HudModel::fade(double dur, std::function<void()> mid) {
	fadeState = { fadeState.value(), 1, dur, 0 };
	game.setTimeout(dur + 0.15, [this, dur, mid]() {
		if (mid) mid();
		fadeState = { fadeState.value(), 0, dur, 0 };
	});
}

void HudModel::fadeTo(double v, double dur) { fadeState = { fadeState.value(), v, dur, 0 }; }

void HudModel::update(double dt) {
	Player& p = *game.player;
	// zone and vehicle names
	const V3 zp = p.vehicle ? p.vehicle->pos : p.pos;
	const std::string z = game.map.ZoneName(zp.x, zp.z);
	if (z != lastZone) { lastZone = z; zone.text = z; zone.t = 3.5; }
	Vehicle* pv = p.vehicle;
	if (pv != lastVeh) { lastVeh = pv; if (pv) { veh.text = pv->def.name; veh.t = 3; } }
	// speech bubbles
	for (int i = (int)speeches.size() - 1; i >= 0; i--) {
		Speech& s = speeches[i];
		s.t -= dt;
		Character* c = s.who.get();
		if (s.t <= 0 || !c || c->removed) speeches.erase(speeches.begin() + i);
	}
	damageFlash = Max(0, damageFlash - dt * 1.5);
	const double hp = p.health / (p.maxHealth ? p.maxHealth : 100);
	vignette = Max(damageFlash, hp < 0.2 && !p.dead ? 0.35 + std::sin(game.time * 4) * 0.1 : 0);
	fadeState.t = Min(fadeState.dur, fadeState.t + dt);
	if (!shard.empty()) shardT += dt;
	for (Line* l : { &helpLine, &subs, &zone, &veh, &radio, &money, &dispatchLine }) if (l->t > 0) l->t -= dt;
	if (big.t > 0) big.t -= dt;
}

} // namespace atg
