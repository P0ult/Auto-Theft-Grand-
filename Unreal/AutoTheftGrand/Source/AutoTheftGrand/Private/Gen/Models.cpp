// See Models.h.
#include "Models.h"

namespace atg {

// ------------------------------------------------------------------ people
std::vector<HumanPart> BuildHuman(const HumanLook& k) {
	std::vector<HumanPart> parts;
	auto part = [&](const char* name, const char* parent, double jx, double jy, double jz) -> MeshBuf& {
		HumanPart p; p.name = name; p.parent = parent; p.joint[0] = jx; p.joint[1] = jy; p.joint[2] = jz;
		p.mesh.Rough(0.75);
		parts.push_back(p);
		return parts.back().mesh;
	};
	{ // hips: pelvis block in the trousers' colour
		MeshBuf& g = part("hips", "", 0, 0.98, 0);
		g.ColorHex(k.pants); g.Add(Geo::Box(0.34, 0.2, 0.21), Mat4::Compose(0, -0.02, 0));
		g.ColorHex(0x2a2a2a); g.Add(Geo::Box(0.35, 0.05, 0.22), Mat4::Compose(0, 0.06, 0)); // belt
	}
	{ // torso from the waist up
		MeshBuf& g = part("torso", "hips", 0, 0.06, 0);
		g.ColorHex(k.shirt);
		g.Add(Geo::Cylinder(0.2, 0.17, 0.48, 10), Mat4::Compose(0, 0.24, 0, 0, 0, 0, 1, 1, 0.62));
		g.Add(Geo::Sphere(0.2, 10, 6, 0, kTau, 0, kPi / 2), Mat4::Compose(0, 0.46, 0, 0, 0, 0, 1.05, 0.45, 0.62));
	}
	{ // neck + head
		MeshBuf& g = part("head", "torso", 0, 0.52, 0);
		g.ColorHex(k.skin);
		g.Add(Geo::Cylinder(0.05, 0.055, 0.1, 8), Mat4::Compose(0, 0.04, 0));
		g.Add(Geo::Sphere(0.105, 12, 10), Mat4::Compose(0, 0.19, 0.01, 0, 0, 0, 0.92, 1.1, 1.0));
		g.Add(Geo::Box(0.04, 0.05, 0.04), Mat4::Compose(0, 0.18, 0.105)); // nose
		g.ColorHex(k.hair);
		g.Add(Geo::Sphere(0.11, 12, 6, 0, kTau, 0, kPi * 0.55), Mat4::Compose(0, 0.215, -0.005, -0.25, 0, 0, 0.95, 1.0, 1.05));
		g.ColorHex(0x111111);
		for (int s : { -1, 1 }) g.Add(Geo::Sphere(0.012, 6, 4), Mat4::Compose(s * 0.036, 0.205, 0.093));
	}
	for (int s : { -1, 1 }) {
		const bool L = s > 0;
		{ // upper arm: short sleeve then skin
			MeshBuf& g = part(L ? "armL" : "armR", "torso", s * 0.235, 0.44, 0);
			g.ColorHex(k.shirt); g.Add(Geo::Capsule(0.062, 0.1, 3, 8), Mat4::Compose(0, -0.07, 0));
			g.ColorHex(k.skin); g.Add(Geo::Capsule(0.05, 0.16, 3, 8), Mat4::Compose(0, -0.18, 0));
		}
		{ // forearm + hand
			MeshBuf& g = part(L ? "foreL" : "foreR", L ? "armL" : "armR", 0, -0.29, 0);
			g.ColorHex(k.skin);
			g.Add(Geo::Capsule(0.045, 0.2, 3, 8), Mat4::Compose(0, -0.13, 0));
			g.Add(Geo::Sphere(0.052, 8, 6), Mat4::Compose(0, -0.29, 0.01, 0, 0, 0, 0.8, 1.15, 0.7));
		}
		{ // thigh
			MeshBuf& g = part(L ? "thighL" : "thighR", "hips", s * 0.095, -0.06, 0);
			g.ColorHex(k.pants); g.Add(Geo::Capsule(0.078, 0.3, 3, 8), Mat4::Compose(0, -0.21, 0));
		}
		{ // shin + shoe
			MeshBuf& g = part(L ? "shinL" : "shinR", L ? "thighL" : "thighR", 0, -0.43, 0);
			g.ColorHex(k.pants); g.Add(Geo::Capsule(0.062, 0.3, 3, 8), Mat4::Compose(0, -0.2, 0));
			g.ColorHex(k.shoes); g.Add(Geo::Box(0.11, 0.09, 0.27), Mat4::Compose(0, -0.42, 0.05));
		}
	}
	return parts;
}

} // namespace atg
