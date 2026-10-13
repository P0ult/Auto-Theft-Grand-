#include "Env.h"
#include <algorithm>
#include <cmath>
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
	// the light: the sky straight up (ambient), and the sun (its colour through the air) or the moon
	const double turbidity = 5 + cloudCover * 3;
	const V3 c = SunTransmittance(sunDir, turbidity);
	ambientColor = SkySample(V3(0, 1, 0), sunDir, turbidity) * 1.4;
	ambientColor = ambientColor.lerp(V3(0.085, 0.105, 0.19), night);
	if (cloudCover > 0.5) { const double g = (ambientColor.x + ambientColor.y + ambientColor.z) / 3; ambientColor = ambientColor.lerp(V3(g, g, g * 1.05), (cloudCover - 0.5) * 1.4); }
	if (moonMode) {
		sunColor = V3(0.55, 0.65, 1.0);
		sunIntensity = 0.8 * SmoothStep(-0.05, 0.2, moonDir.y) * (1 - cloudCover * 0.5);
	} else {
		const double m = std::max({ c.x, c.y, c.z, 0.001 });
		sunColor = c * (1 / m);
		sunIntensity = 3.4 * sunI * std::max({ c.x, c.y, c.z });
	}
	if (lightning > 0) { sunIntensity += lightning * 6; sunColor = V3(0.8, 0.85, 1); }
	const double gloom = cloudCover * 0.05 + rain * 0.08 + fogBoost * 0.04;
	streetLights = 1 - SmoothStep(0.02, 0.15, sunDir.y - gloom);
	uTime += dt;
}

namespace {
constexpr double kRayleigh = 1.6, kMieCoef = 0.006, kMieG = 0.82, kExposure = 0.55;
const double TR[3] = { 5.804542996261093e-6, 1.3562911419845635e-5, 3.0265902468824876e-5 };
const double MC[3] = { 1.8399918514433978e14, 2.7798023919660528e14, 4.0790479543861094e14 };
}

V3 SkySample(const V3& dir, const V3& sunDir, double turbidity) {
	V3 d = dir; d.y = std::max(d.y, 0.0005); d = d.normalized();
	auto sunIntensity = [](double zc) { zc = std::max(-1.0, std::min(1.0, zc)); return 1000 * std::max(0.0, 1 - std::exp(-((1.6110731556870734 - std::acos(zc)) / 1.5))); };
	const double vSunE = sunIntensity(sunDir.y);
	const double vSunfade = 1 - std::max(0.0, std::min(1.0, 1 - std::exp(sunDir.y)));
	const double rc = kRayleigh - (1 - vSunfade);
	const double cM = 0.2 * turbidity * 10e-18;
	const double zen = std::acos(std::max(0.0, d.y));
	const double inv = 1 / (std::cos(zen) + 0.15 * std::pow(93.885 - zen * 180 / kPi, -1.253));
	const double sR = 8.4e3 * inv, sM = 1.25e3 * inv;
	const double cosT = d.dot(sunDir);
	const double rPhase = 0.05968310365946075 * (1 + std::pow(cosT * 0.5 + 0.5, 2));
	const double g = kMieG, g2 = g * g;
	const double mPhase = 0.07957747154594767 * ((1 - g2) / std::pow(1 - 2 * g * cosT + g2, 1.5));
	const double mixK = std::max(0.0, std::min(1.0, std::pow(1 - sunDir.y, 5)));
	const double lift[3] = { 0, 0.0003, 0.00075 };
	double res[3];
	for (int i = 0; i < 3; i++) {
		const double bR = TR[i] * rc, bM = 0.434 * cM * MC[i] * kMieCoef;
		const double Fex = std::exp(-(bR * sR + bM * sM));
		const double ratio = (bR * rPhase + bM * mPhase) / (bR + bM);
		double Lin = std::pow(std::max(0.0, vSunE * ratio * (1 - Fex)), 1.5);
		Lin *= 1 + (std::pow(std::max(0.0, vSunE * ratio * Fex), 0.5) - 1) * mixK;
		const double L0 = 0.1 * Fex;
		const double tex = (Lin + L0) * 0.04 + lift[i];
		res[i] = std::pow(tex, 1 / (1.2 + 1.2 * vSunfade)) * kExposure;
	}
	return V3(res[0], res[1], res[2]);
}

V3 SunTransmittance(const V3& sunDir, double turbidity) {
	const double y = std::max(sunDir.y, 0.0);
	const double zen = std::acos(y);
	const double inv = 1 / (std::cos(zen) + 0.15 * std::pow(std::max(93.885 - zen * 180 / kPi, 0.01), -1.253));
	const double cM = 0.2 * turbidity * 10e-18;
	double res[3];
	for (int i = 0; i < 3; i++) {
		const double bR = TR[i] * kRayleigh, bM = 0.434 * cM * MC[i] * kMieCoef;
		res[i] = std::exp(-(bR * 8.4e3 * inv + bM * 1.25e3 * inv) * 0.55);
	}
	return V3(res[0], res[1], res[2]);
}

} // namespace atg
