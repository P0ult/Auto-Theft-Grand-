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
	double uTime = 0;       // the shaders' clock (the traffic lights run on it)
	// the light (environment.js): the sky's colour straight up (ambientColor), the sun's or the moon's colour and
	// strength; from sky.js's scattering model, run on the CPU as the browser does
	V3 ambientColor, sunColor;
	double sunIntensity = 0;
	double wet = 0;          // how soaked the ground is (lags the rain)
	bool weatherLocked = false;

	void setTime(double h) { hours = std::fmod(std::fmod(h, 24) + 24, 24); update(0); }
	std::string timeString() const;
	void setWeather(const std::string& w, bool instant = false);
	void update(double dt);
};

// sky.js Sky.sample / sunTransmittance: the sky's colour in a direction (linear, before exposure; the browser's
// uniforms: rayleigh 1.6, mie 0.006, g 0.82, exposure 0.55) and the colour of sunlight reaching the ground
V3 SkySample(const V3& dir, const V3& sunDir, double turbidity);
V3 SunTransmittance(const V3& sunDir, double turbidity);

} // namespace atg
