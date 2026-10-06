// The simulation's shared basics: three.js-style vectors, quaternions and matrices (same conventions: right-
// handed, column-major matrices, Euler orders), the helpers from src/core/utils.js, Math.random, and Ref<T>,
// a weak reference to an entity (a JavaScript reference keeps an object alive; here an entity lives as long as
// its manager holds it, and a Ref to one that has gone reads as null, which the game treats like `removed`).
//
// Everything under Private/Sim is plain C++ (no Unreal headers), like Private/Gen: the whole game logic runs
// and is tested without the engine (Tools/simtest.cpp). World units are metres in the browser game's axes.
#pragma once

#include "GenMath.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace atg {

// ------------------------------------------------------------------ utils.js
inline double Damp(double a, double b, double lambda, double dt) { return Lerp(a, b, 1 - std::exp(-lambda * dt)); }
inline double AngleLerp(double a, double b, double t) { return a + WrapAngle(b - a) * t; }
inline double DampAngle(double a, double b, double lambda, double dt) { return a + WrapAngle(b - a) * (1 - std::exp(-lambda * dt)); }
inline double SmoothStep(double a, double b, double v) { return Smooth(a, b, v); }
inline double Dist(double ax, double az, double bx, double bz) { return std::sqrt(Dist2(ax, az, bx, bz)); }
inline bool Finite(double v) { return std::isfinite(v); }
inline double Sgn(double v) { return v > 0 ? 1.0 : v < 0 ? -1.0 : 0.0; } // Math.sign (0 stays 0)

// Math.random (seedable so test runs repeat)
double Rand();
inline double Rand(double a, double b) { return a + (b - a) * Rand(); }
inline int RandInt(int a, int b) { return (int)std::floor(Rand(a, b + 1)); }
void SeedRand(uint64_t seed);
template <typename T> const T& RandPick(const std::vector<T>& v) { return v[(size_t)std::floor(Rand() * v.size())]; }

// ------------------------------------------------------------------ V3
struct V3 {
	double x = 0, y = 0, z = 0;
	V3() = default;
	V3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
	V3& set(double X, double Y, double Z) { x = X; y = Y; z = Z; return *this; }
	V3 operator+(const V3& b) const { return { x + b.x, y + b.y, z + b.z }; }
	V3 operator-(const V3& b) const { return { x - b.x, y - b.y, z - b.z }; }
	V3 operator*(double s) const { return { x * s, y * s, z * s }; }
	V3 operator/(double s) const { return { x / s, y / s, z / s }; }
	V3 operator-() const { return { -x, -y, -z }; }
	V3& operator+=(const V3& b) { x += b.x; y += b.y; z += b.z; return *this; }
	V3& operator-=(const V3& b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
	V3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
	bool operator==(const V3& b) const { return x == b.x && y == b.y && z == b.z; }
	double dot(const V3& b) const { return x * b.x + y * b.y + z * b.z; }
	V3 cross(const V3& b) const { return { y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x }; }
	double lengthSq() const { return x * x + y * y + z * z; }
	double length() const { return std::sqrt(lengthSq()); }
	double distanceTo(const V3& b) const { return (*this - b).length(); }
	double distanceToSq(const V3& b) const { return (*this - b).lengthSq(); }
	// three.js normalize(): a zero vector stays zero
	V3 normalized() const { const double l = length(); return l > 0 ? *this / l : V3(); }
	V3& normalize() { *this = normalized(); return *this; }
	V3& addScaled(const V3& v, double s) { x += v.x * s; y += v.y * s; z += v.z * s; return *this; }
	V3 lerp(const V3& b, double t) const { return { x + (b.x - x) * t, y + (b.y - y) * t, z + (b.z - z) * t }; }
	bool finite() const { return Finite(x) && Finite(y) && Finite(z); }
};
inline V3 operator*(double s, const V3& v) { return v * s; }

// ------------------------------------------------------------------ Quat (three.js Quaternion)
struct Quat {
	double x = 0, y = 0, z = 0, w = 1;
	Quat() = default;
	Quat(double X, double Y, double Z, double W) : x(X), y(Y), z(Z), w(W) {}
	// Euler orders "XYZ" (three.js default) and "YXZ"
	static Quat FromEuler(double ex, double ey, double ez, const char* order = "XYZ");
	static Quat FromAxisAngle(const V3& axis, double angle);
	static Quat FromUnitVectors(const V3& from, const V3& to);
	// from the rotation part of a matrix (three.js setFromRotationMatrix)
	static Quat FromMatrix(const double m[16]);
	Quat operator*(const Quat& b) const; // this * b
	Quat inverse() const { return { -x, -y, -z, w }; } // (unit quaternions)
	Quat& normalize();
	double length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
	V3 rotate(const V3& v) const;
	static Quat Slerp(const Quat& a, const Quat& b, double t);
	// Euler angles in YXZ order (three.js Euler.setFromQuaternion)
	V3 toEulerYXZ() const;
};

// ------------------------------------------------------------------ M4 (three.js Matrix4, column-major)
struct M4 {
	double m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	static M4 Identity() { return M4(); }
	static M4 Compose(const V3& p, const Quat& q, const V3& s = V3(1, 1, 1));
	static M4 Basis(const V3& X, const V3& Y, const V3& Z);
	M4 operator*(const M4& b) const;
	V3 apply(const V3& v) const;      // point
	V3 applyDir(const V3& v) const;   // direction (no translation)
	V3 position() const { return { m[12], m[13], m[14] }; }
	Quat rotation() const;            // (assumes no shear; scale removed)
	M4 inverse() const;
};

// ------------------------------------------------------------------ references to entities
template <typename T>
class Ref {
public:
	Ref() = default;
	Ref(std::nullptr_t) {}
	Ref(const std::shared_ptr<T>& p) : w(p) {}
	template <typename U> Ref(const std::shared_ptr<U>& p) : w(std::static_pointer_cast<T>(p)) {}
	Ref(T* p) { if (p) w = std::static_pointer_cast<T>(p->shared_from_this()); }
	T* get() const { return w.lock().get(); }
	T* operator->() const { return get(); }
	explicit operator bool() const { return get() != nullptr; }
	bool operator==(const T* p) const { return get() == p; }
	bool operator!=(const T* p) const { return get() != p; }
	Ref& operator=(std::nullptr_t) { w.reset(); return *this; }
	void reset() { w.reset(); }
private:
	std::weak_ptr<T> w;
};

} // namespace atg
