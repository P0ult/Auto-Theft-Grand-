// Shared math for the world generator: clamp / lerp / smoothstep, the seeded Mulberry32 RNG and the hash
// and value-noise functions, written to give exactly the same numbers as the JavaScript original
// (src/core/utils.js) so the port builds the same world.
//
// Everything under Private/Gen is plain C++ (no Unreal headers) so it can also be compiled and tested on
// its own (see Tools/gentest.cpp). World units are metres with the JavaScript axes: x east, y up, z south.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace atg {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = kPi * 2.0;
constexpr double kInf = std::numeric_limits<double>::infinity();
inline double NaN() { return std::numeric_limits<double>::quiet_NaN(); }
inline bool IsSet(double v) { return !std::isnan(v); }

inline double Clamp(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
inline double Lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double InvLerp(double a, double b, double v) { return Clamp((v - a) / (b - a), 0.0, 1.0); }
inline double Smooth(double a, double b, double v) { const double t = InvLerp(a, b, v); return t * t * (3.0 - 2.0 * t); }
// Math.hypot exactly as V8 computes it (normalised by the largest value, Kahan summation): plain
// sqrt(a*a + b*b) differs in the last bit now and then, and the terrain is stored in floats, so a last-bit
// difference sometimes decides which side of a comparison a point falls on.
inline double HypotN(const double* v, int n) {
	double mx = 0;
	for (int i = 0; i < n; i++) { const double a = std::fabs(v[i]); if (std::isnan(a)) return std::numeric_limits<double>::quiet_NaN(); if (a > mx) mx = a; }
	if (std::isinf(mx)) return kInf;
	if (mx == 0) return 0;
	double sum = 0, comp = 0;
	for (int i = 0; i < n; i++) {
		const double q = std::fabs(v[i]) / mx;
		const double summand = q * q - comp;
		const double prelim = sum + summand;
		comp = (prelim - sum) - summand;
		sum = prelim;
	}
	return std::sqrt(sum) * mx;
}
inline double Hypot(double a, double b) { const double v[2] = { a, b }; return HypotN(v, 2); }
inline double Hypot3(double a, double b, double c) { const double v[3] = { a, b, c }; return HypotN(v, 3); }
inline double Sign(double v) { return v < 0 ? -1.0 : 1.0; }
inline double Max(double a, double b) { return a > b ? a : b; }
inline double Min(double a, double b) { return a < b ? a : b; }
inline double WrapAngle(double a) { while (a > kPi) a -= kTau; while (a < -kPi) a += kTau; return a; }
inline double Dist2(double ax, double az, double bx, double bz) { const double dx = ax - bx, dz = az - bz; return dx * dx + dz * dz; }

// JS Math.round (halves round up)
inline double JsRound(double v) { return std::floor(v + 0.5); }
// JS ToInt32 of a finite double
inline int32_t ToInt32(double v) {
	if (!std::isfinite(v)) return 0;
	const double t = std::trunc(v);
	const double m = std::fmod(t, 4294967296.0);
	uint32_t u = (uint32_t)(int64_t)(m < 0 ? m + 4294967296.0 : m);
	return (int32_t)u;
}

// Mulberry32, as in utils.js (the state is a double there; a 64-bit counter gives the same bits)
struct RNG {
	uint64_t s;
	explicit RNG(uint32_t seed = 1) : s(seed) {}
	double Next() {
		s += 0x6d2b79f5ULL;
		uint32_t t = (uint32_t)s;
		t = (t ^ (t >> 15)) * (t | 1u);
		t ^= t + (t ^ (t >> 7)) * (t | 61u);
		return (double)(t ^ (t >> 14)) / 4294967296.0;
	}
	double Range(double a, double b) { return a + (b - a) * Next(); }
	int Int(int a, int b) { return (int)std::floor(Range(a, b + 1)); }
	bool Chance(double p) { return Next() < p; }
	template <typename T> const T& Pick(const std::vector<T>& arr) { return arr[(size_t)std::floor(Next() * arr.size())]; }
	template <typename T> T Weighted(const std::vector<std::pair<T, double>>& entries) {
		double total = 0; for (auto& e : entries) total += e.second;
		double r = Next() * total;
		for (auto& e : entries) { r -= e.second; if (r <= 0) return e.first; }
		return entries.back().first;
	}
};

inline double Hash2(int32_t x, int32_t y) {
	uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return (double)(h ^ (h >> 16)) / 4294967296.0;
}

// value noise + fbm (utils.js)
inline double VNoise(double x, double y) {
	const double xi = std::floor(x), yi = std::floor(y);
	const double xf = x - xi, yf = y - yi;
	const double u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
	const int32_t ix = ToInt32(xi), iy = ToInt32(yi);
	const double a = Hash2(ix, iy), b = Hash2(ix + 1, iy), c = Hash2(ix, iy + 1), d = Hash2(ix + 1, iy + 1);
	return Lerp(Lerp(a, b, u), Lerp(c, d, u), v);
}
inline double Fbm(double x, double y, int oct = 4) {
	double s = 0, a = 0.5, f = 1, n = 0;
	for (int i = 0; i < oct; i++) { s += a * VNoise(x * f, y * f); n += a; a *= 0.5; f *= 2.03; }
	return s / n;
}

struct V2 { double x = 0, z = 0; };
// a polyline point: x, z and (optionally) height y
struct P3 { double x = 0, z = 0, y = 0; };
using Line = std::vector<P3>;

} // namespace atg
