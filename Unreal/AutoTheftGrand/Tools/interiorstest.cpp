// Prints each walk-in shop like dumpinteriors.mjs:
//   Tools/native.sh interiorstest.exe interiorstest.cpp && ./interiorstest.exe > cppinteriors.txt
//   node --import ./three-hook.mjs dumpinteriors.mjs > jsinteriors.txt && diff --strip-trailing-cr jsinteriors.txt cppinteriors.txt
#include "CityMap.h"
#include "InteriorMesh.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace atg;

static double Z(double v) { return v + 0.0; }
static double Z1(double v) { return std::fabs(v) < 0.05 ? 0.0 : v; } // (a near-zero sum prints as 0, whatever its sign)
static std::string Sums(const MeshBuf& g) {
	double s[9] = {};
	for (size_t i = 0; i < g.Count(); i++) {
		for (int a = 0; a < 3; a++) { s[a] += g.P[i * 3 + a]; s[3 + a] += g.N[i * 3 + a]; }
		if (!g.C[1].empty()) { s[6] += g.C[1][i * 2]; s[7] += g.C[1][i * 2 + 1]; s[8] += g.C[2][i * 2]; }
	}
	char b[256];
	snprintf(b, sizeof b, "v %zu i %zu pos %.1f %.1f %.1f nor %.1f %.1f %.1f col %.1f %.1f %.1f", g.Count(), g.I.size(), Z1(s[0]), Z1(s[1]), Z1(s[2]), Z1(s[3]), Z1(s[4]), Z1(s[5]), Z1(s[6]), Z1(s[7]), Z1(s[8]));
	return b;
}
int main() {
	CityMap m;
	for (const InteriorShell& it : m.interiors) {
		printf("shop %s %s colliders %zu furniture %zu\n", it.key.c_str(), it.name.c_str(), it.colliders.size(), it.furniture.size());
		printf("  clerk %.3f %.3f yaw %.3f service %.3f %.3f till %.3f %.3f %.3f\n", Z(it.clerk.x), Z(it.clerk.z), Z(it.clerkYaw), Z(it.service.x), Z(it.service.z), Z(it.till.x), Z(it.till.y), Z(it.till.z));
		printf("  door %.3f %.3f center %.3f %.3f light %.3f %.3f %.3f\n", Z(it.door.x), Z(it.door.z), Z(it.center.x), Z(it.center.z), Z(it.light.x), Z(it.light.y), Z(it.light.z));
		for (const auto& q : it.furniture) printf("  %s u=%.3f w=%.3f u0=%.3f w0=%.3f u1=%.3f w1=%.3f h=%.3f face=%.3f i=%.3f\n", q.kind.c_str(), Z(q.u), Z(q.w), Z(q.u0), Z(q.w0), Z(q.u1), Z(q.w1), Z(q.h), Z(q.face), Z(q.i));
		const InteriorMesh im = BuildInteriorMesh(it);
		printf("  solid %s\n", Sums(im.solid).c_str());
		if (!im.glow.Empty()) printf("  glow %s\n", Sums(im.glow).c_str());
		// (in the order the browser adds them to the group: weapons and glass as built, then the panels)
		for (const auto& w : im.weapons) printf("  weapon %.3f %.3f %.3f rot %.3f %.3f\n", Z(w.x), Z(w.y), Z(w.z), Z(w.yaw), Z(w.roll));
		for (const auto& g : im.glass) printf("  glass %.3f %.3f %.3f size %.3f %.3f %.3f\n", Z(g.x), Z(g.y), Z(g.z), Z(g.sx), Z(g.sy), Z(g.sz));
		for (const auto& p : im.panels) printf("  panel %.3f %.3f %.3f size %.3f %.3f yaw %.3f lit %d\n", Z(p.x), Z(p.y), Z(p.z), Z(p.width), Z(p.height), Z(p.yaw), p.lit ? 1 : 0);
	}
}
