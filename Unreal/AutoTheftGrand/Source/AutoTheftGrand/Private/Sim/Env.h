// The clock and the weather (the simulation half of src/world/environment.js): the time of day and the sun
// and moon directions, how dark it is, the street lights, and weather that drifts between clear, cloudy,
// rain, storms and fog. The renderer turns this into light, sky and fog.
#pragma once

#include "Core.h"

namespace atg {

class Environment {
public:
	Environment();
	double hours = 8.5;      // game clock
	double timeScale = 1;    // game minutes per real second
	V3 sunDir, moonDir, lightDir;
	double night = 0, dayF = 1, streetLights = 0, sunI = 1;
	bool moonMode = false;
	std::string weather = "clear";
	double cloudCover = 0.35, rain = 0, targetRain = 0, targetCloud = 0.35, fogBoost = 0, fogTarget = 0, weatherTimer = 240, lightning = 0;
	double wet = 0;          // how soaked the ground is (lags the rain)
	bool weatherLocked = false;

	void setTime(double h) { hours = std::fmod(std::fmod(h, 24) + 24, 24); update(0); }
	std::string timeString() const;
	void setWeather(const std::string& w, bool instant = false);
	void update(double dt);
};

} // namespace atg
