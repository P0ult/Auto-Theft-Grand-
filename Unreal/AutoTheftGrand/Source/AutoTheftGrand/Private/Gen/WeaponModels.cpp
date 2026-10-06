#include "WeaponModels.h"
#include <map>
#include <mutex>

namespace atg {

namespace {
MeshBuf Build(const std::string& id) {
	MeshBuf g;
	auto c = [&](uint32_t hex) { g.ColorHex(hex); };
	auto box = [&](double w, double h, double d, double x, double y, double z, double rx = 0) { g.Add(Geo::Box(w, h, d), Mat4::Compose(x, y, z, rx)); };
	auto cyl = [&](double r, double l, double x, double y, double z, int s = 8) { g.Add(Geo::Cylinder(r, r, l, s), Mat4::Compose(x, y, z, kPi / 2)); };
	const uint32_t black = 0x1a1a1a, gun = 0x2a2c30, wood = 0x6b3e1f;
	if (id == "knife") {
		c(0x222222); box(0.025, 0.03, 0.11, 0, 0, 0);
		c(0xd8d8d8); box(0.006, 0.028, 0.18, 0, 0.005, 0.14);
	} else if (id == "bat") {
		c(0xb5874e); g.Add(Geo::Cylinder(0.035, 0.018, 0.85, 10), Mat4::Compose(0, 0, 0.36, kPi / 2));
		c(0x222222); cyl(0.02, 0.12, 0, 0, -0.02);
	} else if (id == "pistol") {
		c(gun); box(0.03, 0.035, 0.19, 0, 0.055, 0.05);
		c(black); box(0.028, 0.11, 0.045, 0, 0, -0.005, -0.25);
		c(black); box(0.01, 0.03, 0.04, 0, 0.005, 0.03);
	} else if (id == "smg") {
		c(gun); box(0.04, 0.06, 0.26, 0, 0.05, 0.06);
		c(black); box(0.03, 0.1, 0.04, 0, -0.02, 0.0, -0.1);
		c(black); box(0.025, 0.14, 0.03, 0, -0.03, 0.09);
		c(gun); cyl(0.012, 0.08, 0, 0.06, 0.22);
	} else if (id == "shotgun") {
		c(gun); cyl(0.016, 0.62, 0, 0.07, 0.34);
		c(gun); cyl(0.014, 0.5, 0, 0.045, 0.3);
		c(wood); box(0.04, 0.05, 0.14, 0, 0.045, 0.34);
		c(gun); box(0.045, 0.06, 0.16, 0, 0.055, 0.05);
		c(wood); box(0.035, 0.09, 0.05, 0, -0.01, 0.0, -0.3);
		c(wood); box(0.04, 0.08, 0.3, 0, 0.03, -0.18, 0.12);
	} else if (id == "rifle") {
		c(gun); box(0.045, 0.07, 0.34, 0, 0.06, 0.1);
		c(gun); cyl(0.013, 0.34, 0, 0.075, 0.43);
		c(wood); box(0.05, 0.055, 0.2, 0, 0.055, 0.32);
		c(black); box(0.03, 0.17, 0.06, 0, -0.04, 0.16, 0.35);
		c(black); box(0.035, 0.1, 0.04, 0, -0.01, 0.0, -0.2);
		c(wood); box(0.04, 0.09, 0.28, 0, 0.04, -0.2, 0.1);
		c(black); box(0.01, 0.04, 0.01, 0, 0.12, 0.5);
	} else if (id == "rpg") {
		c(0x3e4a2f); cyl(0.055, 1.05, 0, 0.1, 0.15, 12);
		c(black); box(0.03, 0.12, 0.05, 0, 0.0, 0.0, -0.2);
		c(black); box(0.03, 0.1, 0.04, 0, 0.02, 0.3);
		c(0x5a6b45); g.Add(Geo::Cone(0.07, 0.22, 10), Mat4::Compose(0, 0.1, 0.78, kPi / 2));
	} else if (id == "grenade") {
		c(0x3b4a2a); g.Add(Geo::Sphere(0.045, 10, 8), Mat4::Identity());
		c(0x888888); box(0.015, 0.04, 0.02, 0, 0.05, 0);
	} else if (id == "sniper") {
		c(0x3a3d33); box(0.045, 0.065, 0.36, 0, 0.055, 0.1);
		c(gun); cyl(0.012, 0.62, 0, 0.07, 0.58);
		c(gun); cyl(0.02, 0.06, 0, 0.07, 0.9);
		c(black); cyl(0.024, 0.3, 0, 0.13, 0.12, 12);
		c(black); cyl(0.03, 0.05, 0, 0.13, 0.28, 12); cyl(0.028, 0.05, 0, 0.13, -0.03, 12);
		c(black); box(0.012, 0.04, 0.02, 0, 0.1, 0.05);
		c(0x3a3d33); box(0.035, 0.1, 0.045, 0, -0.01, 0.0, -0.25);
		c(0x3a3d33); box(0.045, 0.11, 0.3, 0, 0.03, -0.22, 0.14);
		c(black); box(0.01, 0.12, 0.01, 0.03, 0.0, 0.66, 0.4); box(0.01, 0.12, 0.01, -0.03, 0.0, 0.66, 0.4);
	} else if (id == "minigun") {
		c(gun); cyl(0.07, 0.34, 0, 0.04, 0.08, 12);
		c(0x8a8d90); for (int k = 0; k < 6; k++) { const double a = k / 6.0 * kTau; cyl(0.011, 0.62, std::cos(a) * 0.035, 0.04 + std::sin(a) * 0.035, 0.52); }
		c(gun); cyl(0.052, 0.03, 0, 0.04, 0.62, 12); cyl(0.052, 0.03, 0, 0.04, 0.78, 12);
		c(black); box(0.03, 0.13, 0.04, 0, -0.06, -0.02, -0.2);
		c(black); box(0.1, 0.03, 0.03, 0, 0.14, 0.05);
		c(0x5a5a3a); box(0.1, 0.12, 0.14, 0.1, -0.02, 0.02);
	} else if (id == "molotov") {
		c(0x2f6b3a); g.Add(Geo::Cylinder(0.035, 0.035, 0.14, 10), Mat4::Identity());
		c(0x2f6b3a); g.Add(Geo::Cylinder(0.013, 0.033, 0.06, 10), Mat4::Compose(0, 0.1, 0));
		c(0xd9c9a3); g.Add(Geo::Cylinder(0.016, 0.01, 0.07, 6), Mat4::Compose(0, 0.16, 0));
	}
	return g;
}
}

const MeshBuf& WeaponGeometry(const std::string& id) {
	static std::mutex Lock;
	static std::map<std::string, MeshBuf> Cache;
	std::lock_guard<std::mutex> G(Lock);
	auto it = Cache.find(id);
	if (it == Cache.end()) it = Cache.emplace(id, Build(id)).first;
	return it->second;
}

} // namespace atg
