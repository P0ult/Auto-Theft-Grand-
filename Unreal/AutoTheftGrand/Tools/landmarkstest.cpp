// Prints the landmarks like dumplandmarks.mjs:
//   Tools/native.sh landmarkstest.exe landmarkstest.cpp && ./landmarkstest.exe > cpplandmarks.txt
//   node --import ./three-hook.mjs dumplandmarks.mjs > jslandmarks.txt && diff --strip-trailing-cr jslandmarks.txt cpplandmarks.txt
#include "Landmarks.h"
#include <cmath>
#include <cstdio>
#include <string>
using namespace atg;

static double Z3(double v) { return std::fabs(v) < 0.0005 ? 0 : v; }
static std::string Sums(const MeshBuf& g) {
	double s[9] = {};
	for (size_t i = 0; i < g.Count(); i++) {
		for (int a = 0; a < 3; a++) { s[a] += g.P[i * 3 + a]; s[3 + a] += g.N[i * 3 + a]; }
		if (!g.C[1].empty()) { s[6] += g.C[1][i * 2]; s[7] += g.C[1][i * 2 + 1]; s[8] += g.C[2][i * 2]; }
	}
	std::string r = "v " + std::to_string(g.Count()) + " i " + std::to_string(g.I.size());
	for (double v : s) { char b[32]; snprintf(b, sizeof b, " %.2f", std::fabs(v) < 0.005 ? 0.0 : v); r += b; }
	return r;
}

int main() {
	CityMap map;
	const Landmarks L = BuildLandmarks(map);
	// (landmarks.js adds the pier's and the wheel's collision first, then the cranes' boxes interleaved with their beacons)
	size_t ci = 0, bi = 0;
	auto col = [&](const LandmarkCollider& c) {
		if (c.kind == "box") printf("box %s %d %.3f %.3f %.3f %.3f %.3f %.3f\n", c.type.c_str(), c.soft ? 1 : 0, Z3(c.minX), Z3(c.maxX), Z3(c.minZ), Z3(c.maxZ), Z3(c.minY), Z3(c.maxY));
		else printf("circle %s %.3f %.3f %.3f %.3f\n", c.type.c_str(), Z3(c.x), Z3(c.z), Z3(c.r), Z3(c.h));
	};
	const size_t craneBoxes = 16;
	for (; ci < L.colliders.size() - craneBoxes; ci++) col(L.colliders[ci]);
	for (int k = 0; k < 4; k++) {
		for (int f = 0; f < 4; f++) col(L.colliders[ci++]);
		for (int b = 0; b < 2; b++, bi++) printf("beacon %.3f %.3f %.3f\n", Z3(L.beacons[bi].x), Z3(L.beacons[bi].y), Z3(L.beacons[bi].z));
	}
	printf("deck %s\n", Sums(L.pierDeck).c_str());
	printf("posts %s\n", Sums(L.pierPosts).c_str());
	printf("poles %s\n", Sums(L.lampPoles).c_str());
	for (const LmPoint& p : L.lamps) printf("lamp %.3f %.3f %.3f\n", Z3(p.x), Z3(p.y), Z3(p.z));
	printf("gondola %s\n", Sums(L.gondola).c_str());
	for (int k = 0; k < 16; k++) printf("gcolor %.4f %.4f %.4f\n", L.gondolaColor[k][0], L.gondolaColor[k][1], L.gondolaColor[k][2]);
	printf("support %s\n", Sums(L.support).c_str());
	printf("hub %.3f %.3f %.3f\n", Z3(L.hubX), Z3(L.hubY), Z3(L.hubZ));
	printf("wheel %s\n", Sums(L.wheel).c_str());
	printf("neon %s\n", Sums(L.neon).c_str());
	for (const auto& l : L.letters) printf("letter %.3f %.3f %.3f\n", Z3(l.x), Z3(l.y), Z3(l.z));
	printf("scaffold %s\n", Sums(L.scaffold).c_str());
	printf("cranes %s\n", Sums(L.cranes).c_str());
	return 0;
}
