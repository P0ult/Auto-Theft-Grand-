#include "Env.h"
#include <cstdio>

namespace atg {

Environment::Environment() { update(0); }

std::string Environment::timeString() const {
	const int h = (int)std::floor(hours), m = (int)std::floor((hours - h) * 60);
	char buf[16];
	std::snprintf(buf, sizeof buf, "%02d:%02d", h, m);
	return buf;
}

void Environment::setWeather(const std::string& w, bool instant) {
	weather = w;
	double c = 0.3, r = 0;
	if (w == "clear") { c = 0.25; r = 0; }
	else if (w == "cloudy") { c = 0.7; r = 0; }
	else if (w == "rain") { c = 0.9; r = 1; }
	else if (w == "storm") { c = 1; r = 1; }
	else if (w == "fog") { c = 0.5; r = 0; }
	targetCloud = c; targetRain = r;
	fogTarget = w == "fog" ? 1 : (w == "rain" || w == "storm") ? 0.4 : 0;
	if (instant) { cloudCover = targetCloud; rain = targetRain; fogBoost = fogTarget; }
}

void Environment::update(double dt) {
	hours = std::fmod(hours + dt * timeScale / 60, 24.0);
	const double theta = (hours - 6) / 24 * kTau;
	const double tilt = 0.45;
	sunDir = V3(std::cos(theta), std::sin(theta) * std::cos(tilt) + 0.2, std::sin(theta) * std::sin(tilt) + 0.08).normalized();
	moonDir = V3(-std::cos(theta) * 0.9, -std::sin(theta) * std::cos(tilt) * 0.95 + 0.12, -std::sin(theta) * std::sin(tilt) - 0.2).normalized();
	weatherTimer -= dt * timeScale;
	if (weatherTimer <= 0 && !weatherLocked) {
		weatherTimer = 300 + Rand() * 600;
		const double r = Rand();
		setWeather(r < 0.55 ? "clear" : r < 0.78 ? "cloudy" : r < 0.92 ? "rain" : r < 0.96 ? "storm" : "fog");
	}
	cloudCover = Lerp(cloudCover, targetCloud, Clamp(dt * 0.05, 0, 1));
	rain = Lerp(rain, targetRain, Clamp(dt * 0.08, 0, 1));
	fogBoost = Lerp(fogBoost, fogTarget, Clamp(dt * 0.05, 0, 1));
	wet = Clamp(Lerp(wet, rain > 0.2 ? 1 : 0, dt * (rain > 0.2 ? 0.05 : 0.01)), 0, 1);
	if (weather == "storm" && Rand() < dt * 0.08) lightning = 1;
	lightning = Max(0, lightning - dt * 3.5);
	const double sunY = sunDir.y;
	night = 1 - SmoothStep(-0.18, 0.06, sunY);
	dayF = SmoothStep(-0.04, 0.12, sunY);
	sunI = SmoothStep(-0.02, 0.1, sunY) * (1 - cloudCover * 0.55) * (1 - rain * 0.4);
	moonMode = sunY < -0.04;
	lightDir = moonMode ? moonDir : sunDir;
	if (lightDir.y < 0.12) { lightDir.y = 0.12; lightDir.normalize(); }
	const double gloom = cloudCover * 0.05 + rain * 0.08 + fogBoost * 0.04;
	streetLights = 1 - SmoothStep(0.02, 0.15, sunDir.y - gloom);
}

} // namespace atg
