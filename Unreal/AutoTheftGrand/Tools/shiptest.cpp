// Prints the cargo ship like dumpship.mjs:
//   Tools/native.sh shiptest.exe shiptest.cpp && ./shiptest.exe > cppship.txt
//   node --import ./three-hook.mjs dumpship.mjs > jsship.txt && diff --strip-trailing-cr jsship.txt cppship.txt
#include "CargoShip.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace atg;

static double Z3(double v) { return std::fabs(v) < 0.0005 ? 0 : v; }
static double Z2(double v) { return std::fabs(v) < 0.005 ? 0 : v; }
static std::string Sums(const MeshBuf& g) {
	double s[9] = {};
	for (size_t i = 0; i < g.Count(); i++) {
		for (int a = 0; a < 3; a++) { s[a] += g.P[i * 3 + a]; s[3 + a] += g.N[i * 3 + a]; }
		if (!g.C[1].empty()) { s[6] += g.C[1][i * 2]; s[7] += g.C[1][i * 2 + 1]; s[8] += g.C[2][i * 2]; }
	}
	char buf[256];
	snprintf(buf, sizeof buf, "v %zu i %zu pos %.2f %.2f %.2f nor %.2f %.2f %.2f col %.2f %.2f %.2f", g.Count(), g.I.size(), Z2(s[0]), Z2(s[1]), Z2(s[2]), Z2(s[3]), Z2(s[4]), Z2(s[5]), Z2(s[6]), Z2(s[7]), Z2(s[8]));
	return buf;
}

int main() {
	const CargoShip s = BuildCargoShip();
	for (const ShipCollider& c : s.colliders) {
		if (c.kind == "box") printf("box %s %.3f %.3f %.3f %.3f %.3f %.3f\n", c.type.c_str(), Z3(c.minX), Z3(c.maxX), Z3(c.minZ), Z3(c.maxZ), Z3(c.minY), Z3(c.maxY));
		else if (c.kind == "obox") printf("obox %s %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", c.type.c_str(), Z3(c.cx), Z3(c.cz), Z3(c.hx), Z3(c.hz), Z3(c.yaw), Z3(c.minY), Z3(c.maxY));
		else if (c.kind == "deck") printf("deck %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", Z3(c.ax), Z3(c.az), Z3(c.ay), Z3(c.bx), Z3(c.bz), Z3(c.by), Z3(c.hl), Z3(c.hr));
		else printf("circle %.3f %.3f %.3f %.3f %.3f\n", Z3(c.x), Z3(c.z), Z3(c.r), Z3(c.h), Z3(c.y0));
	}
	const std::pair<const char*, const MeshBuf*> parts[] = { { "hull", &s.hull }, { "deck", &s.deck }, { "steel", &s.steel }, { "white", &s.white }, { "cont", &s.cont }, { "glass", &s.glass }, { "lamp", &s.lamp } };
	for (const auto& [k, g] : parts) printf("part %s %s\n", k, Sums(*g).c_str());
	const std::pair<const char*, ShipPoint> pts[] = { { "safe", s.safe }, { "bridgeDoor", s.bridgeDoor }, { "gangwayFoot", s.gangwayFoot }, { "gangwayTop", s.gangwayTop } };
	for (const auto& [k, p] : pts) printf("%s %.3f %.3f %.3f\n", k, Z3(p.x), Z3(p.y), Z3(p.z));
	printf("route");
	for (const Pt3& r : s.route) printf(" %.3f,%.3f,%.3f", Z3(r[0]), Z3(r[1]), Z3(r[2]));
	printf("\n");
	for (const ShipStack& st : s.stacks) printf("stack %.3f %.3f %d %.3f %d\n", Z3(st.lx), Z3(st.lz), st.n, Z3(st.bay), st.row);
	for (int lz : { -93, -80, -60, -48, 0, 80, 85 }) printf("beam %d %.3f\n", lz, Z3(ShipHalfBeam(lz)));
	printf("ship %s %.3f %.3f %.3f\n", SHIP.name, Z3(SHIP.x), Z3(SHIP.z), Z3(SHIP.deckY));
	return 0;
}
