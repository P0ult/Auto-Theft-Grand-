// Prints each boat model like dumpboats.mjs:
//   Tools/native.sh boatstest.exe boatstest.cpp && ./boatstest.exe > cppboats.txt
//   node --import ./three-hook.mjs dumpboats.mjs > jsboats.txt && diff --strip-trailing-cr jsboats.txt cppboats.txt
#include "BoatModels.h"
#include <cstdio>
#include <string>
using namespace atg;

static std::string Sums(const MeshBuf& g) {
	double s[9] = {};
	for (size_t i = 0; i < g.Count(); i++) {
		for (int a = 0; a < 3; a++) { s[a] += g.P[i * 3 + a]; s[3 + a] += g.N[i * 3 + a]; }
		if (!g.C[1].empty()) { s[6] += g.C[1][i * 2]; s[7] += g.C[1][i * 2 + 1]; s[8] += g.C[2][i * 2]; }
	}
	char buf[256];
	snprintf(buf, sizeof buf, "v %zu i %zu pos %.2f %.2f %.2f nor %.2f %.2f %.2f col %.2f %.2f %.2f", g.Count(), g.I.size(), s[0] + 0.0, s[1] + 0.0, s[2] + 0.0, s[3] + 0.0, s[4] + 0.0, s[5] + 0.0, s[6] + 0.0, s[7] + 0.0, s[8] + 0.0);
	return buf;
}
static double Z(double v) { return v + 0.0; }

int main() {
	for (const VehicleDef& d : VehicleDefs()) {
		if (d.boat.empty()) continue;
		const BoatModel& m = BuildBoatModel(d);
		printf("boat %s\nseats", d.id.c_str());
		for (const Pt3& s : m.seats) printf(" %.4f,%.4f,%.4f", Z(s[0]), Z(s[1]), Z(s[2]));
		printf("\nseatHip %.4f\n", m.seatHip);
		printf("doorPos %.4f,%.4f,%.4f\n", Z(m.doorPos[0]), Z(m.doorPos[1]), Z(m.doorPos[2]));
		printf("hull draft %.4f free %.4f L %.4f z0 %.4f\n", m.hullData.draft, m.hullData.free, m.hullData.L, m.hullData.z0);
		printf("propPos %.4f,%.4f,%.4f\n", Z(m.propPos[0]), Z(m.propPos[1]), Z(m.propPos[2]));
		const std::pair<const char*, const MeshBuf*> parts[] = { { "hull", &m.hull }, { "trim", &m.trim }, { "glass", &m.glass }, { "head", &m.head }, { "tail", &m.tail }, { "deck", &m.deck }, { "chrome", &m.chrome } };
		for (const auto& [k, g] : parts) if (!g->Empty()) printf("part %s %s\n", k, Sums(*g).c_str());
		if (m.hasLightbar) {
			printf("light red at %.4f,%.4f,%.4f %s\n", Z(m.lightRedPos[0]), Z(m.lightRedPos[1]), Z(m.lightRedPos[2]), Sums(m.lightRed).c_str());
			printf("light blue at %.4f,%.4f,%.4f %s\n", Z(m.lightBluePos[0]), Z(m.lightBluePos[1]), Z(m.lightBluePos[2]), Sums(m.lightBlue).c_str());
		}
		if (m.hasGun) printf("gun at %.4f,%.4f,%.4f %s\n", Z(m.gunPos[0]), Z(m.gunPos[1]), Z(m.gunPos[2]), Sums(m.gun).c_str());
	}
}
