// Prints the same sums over the humanoid mesh as dumphuman.mjs, for the same appearances.
//   ./native.sh humantest.exe humantest.cpp && ./humantest.exe > cpphuman.txt && diff jshuman.txt cpphuman.txt
#include "Sim/Humanoid.h"
#include <cstdio>
#include <functional>

using namespace atg;

int main() {
	auto base = [] {
		Appearance a;
		a.female = false; a.skin = 0x8a5536; a.hair = 0x111111; a.hairStyle = "short"; a.shirt = 0xffffff; a.shirtType = "tee";
		a.pants = 0x1f2a44; a.shorts = false; a.shoes = 0x111111; a.hat = -1; a.build = 1; a.height = 1; a.glasses = false;
		a.beard = false; a.jacketColor = 0x222222; a.bandana = -1; a.hasUniform = false;
		return a;
	};
	std::vector<std::function<void(Appearance&)>> looks = {
		[](Appearance&) {},
		[](Appearance& a) { a.female = true; a.skin = 0xf1c7a5; a.hair = 0x7a5230; a.hairStyle = "ponytail"; a.shirtType = "long"; a.shirt = 0x8b1e1e; a.pants = 0x6d5c43; a.shorts = true; a.glasses = true; a.build = 0.95; },
		[](Appearance& a) { a.hairStyle = "cap"; a.hat = 0x8b1e1e; a.shirtType = "jacket"; a.jacketColor = 0x455a64; a.beard = true; a.build = 1.12; a.pants = 0x2b2b2b; a.shoes = 0xeeeeee; },
		[](Appearance& a) { a.hairStyle = "afro"; a.shirtType = "tank"; a.shirt = 0xd4a017; a.pants = 0x4e4a45; a.bandana = 0x1e3f8b; a.skin = 0x5f3a24; },
		[](Appearance& a) { a.hairStyle = "buzz"; a.shirtType = "long"; a.hasUniform = true; a.uniformShirt = 0x1f2f55; a.uniformPants = 0x1a2440; a.uniformHat = 0x141d33; a.uniformBand = 0xd4af37; a.vest = "tactical"; a.mask = 0x111111; },
		[](Appearance& a) { a.female = true; a.hairStyle = "bun"; a.shirtType = "long"; a.vest = "hivis"; a.hardhat = 0xffcc00; a.build = 0.9; },
		[](Appearance& a) { a.female = true; a.hairStyle = "long"; a.shirt = 0x4db6ac; a.pants = 0x1d3b5c; },
		[](Appearance& a) { a.hairStyle = "bald"; a.vest = "hivis"; a.vestColor = 0xff6600; a.hasUniform = true; a.uniformShirt = 0x333333; a.uniformPants = 0x222222; a.noBadge = true; },
	};
	for (size_t k = 0; k < looks.size(); k++) {
		Appearance a = base();
		looks[k](a);
		const HumanoidMesh m = BuildHumanoidGeometry(a);
		double p[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 }, c[3] = { 0, 0, 0 }, mat = 0, bw[17] = {};
		for (size_t i = 0; i < m.Count(); i++) {
			for (int j = 0; j < 3; j++) { p[j] += m.P[i * 3 + j]; n[j] += m.N[i * 3 + j]; c[j] += m.C[i * 3 + j]; }
			mat += m.mat[i];
			for (int j = 0; j < 4; j++) bw[m.si[i * 4 + j]] += m.sw[i * 4 + j];
		}
		double isum = 0;
		for (uint32_t i : m.I) isum += i;
		printf("look %zu v %zu i %zu isum %.0f\n", k, m.Count(), m.I.size(), isum);
		printf("  pos %.3f %.3f %.3f nor %.3f %.3f %.3f\n", p[0] + 0.0, p[1] + 0.0, p[2] + 0.0, n[0] + 0.0, n[1] + 0.0, n[2] + 0.0);
		printf("  col %.3f %.3f %.3f mat %.0f\n", c[0], c[1], c[2], mat);
		printf("  bones");
		for (double v : bw) printf(" %.2f", v + 0.0);
		printf("\n");
	}
	return 0;
}
