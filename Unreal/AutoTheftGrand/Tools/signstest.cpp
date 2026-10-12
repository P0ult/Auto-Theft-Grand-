// Prints the billboards and signs like dumpsigns.mjs:
//   Tools/native.sh signstest.exe signstest.cpp && ./signstest.exe > cppsigns.txt
//   node --import ./three-hook.mjs dumpsigns.mjs > jssigns.txt && diff --strip-trailing-cr jssigns.txt cppsigns.txt
#include "CitySigns.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace atg;

static double Z3(double v) { return std::fabs(v) < 0.0005 ? 0 : v; }
static std::string Sums(const MeshBuf& g, bool uvs = true) {
	double s[11] = {};
	for (size_t i = 0; i < g.Count(); i++) {
		for (int a = 0; a < 3; a++) { s[a] += g.P[i * 3 + a]; s[3 + a] += g.N[i * 3 + a]; }
		if (!g.C[1].empty()) { s[6] += g.C[1][i * 2]; s[7] += g.C[1][i * 2 + 1]; s[8] += g.C[2][i * 2]; }
		if (uvs && !g.C[0].empty()) { s[9] += g.C[0][i * 2]; s[10] += g.C[0][i * 2 + 1]; }
	}
	std::string r = "v " + std::to_string(g.Count()) + " i " + std::to_string(g.I.size());
	for (double v : s) { char b[32]; snprintf(b, sizeof b, " %.2f", std::fabs(v) < 0.005 ? 0.0 : v); r += b; }
	return r;
}

int main() {
	CityMap map;
	const CityBillboards bb = BuildBillboards(map);
	for (const auto& b : bb.colliders) printf("box %.3f %.3f %.3f %.3f %.3f %.3f\n", Z3(b.minX), Z3(b.maxX), Z3(b.minZ), Z3(b.maxZ), Z3(b.minY), Z3(b.maxY));
	printf("frame %s\n", Sums(bb.frame, false).c_str());
	printf("faces %s\n", Sums(bb.faces).c_str());
	for (const CitySign& s : PlaceSigns(map)) printf("sign %s %.3f %.3f %.3f %.3f %.3f %.3f\n", s.text.c_str(), Z3(s.x), Z3(s.y), Z3(s.z), Z3(s.w), Z3(s.h), Z3(s.yaw));
	return 0;
}
