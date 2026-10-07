// Prints each aircraft (and the tank) model like dumpaircraft.mjs:
//   Tools/native.sh aircrafttest.exe aircrafttest.cpp && ./aircrafttest.exe > cppair.txt
//   node --import ./three-hook.mjs dumpaircraft.mjs > jsair.txt && diff --strip-trailing-cr jsair.txt cppair.txt
#include "AircraftModels.h"
#include <cstdio>
using namespace atg;

static void PrintP3(const Pt3& p) { printf("%.4f,%.4f,%.4f", p[0] + 0.0, p[1] + 0.0, p[2] + 0.0); }
static void List(const char* name, const std::vector<Pt3>& v) {
	printf("  %s", name);
	for (const Pt3& p : v) { printf(" "); PrintP3(p); }
	printf("\n");
}
static void Sums(const char* k, const MeshBuf& g) {
	double sx = 0, sy = 0, sz = 0, cr = 0, cg = 0, cb = 0;
	const bool col = !g.C[1].empty();
	for (size_t i = 0; i < g.Count(); i++) {
		sx += g.P[i * 3]; sy += g.P[i * 3 + 1]; sz += g.P[i * 3 + 2];
		if (col) { cr += g.C[1][i * 2]; cg += g.C[1][i * 2 + 1]; cb += g.C[2][i * 2]; }
	}
	printf("  %s v %zu i %zu sum %.2f %.2f %.2f col %.2f %.2f %.2f\n", k, g.Count(), g.I.size(), sx + 0.0, sy + 0.0, sz + 0.0, cr + 0.0, cg + 0.0, cb + 0.0);
}

int main() {
	for (const char* id : { "skipper", "raptor", "hercules", "warhawk", "skylark", "mammoth" }) {
		const VehicleDef& def = *FindVehicle(id);
		const AircraftModel& m = BuildAircraftModel(def, def.colors[0], id);
		printf("model %s cgY %.4f visibleSeats %d\n", id, m.cgY, m.visibleSeats);
		List("seats", m.seats);
		printf("  doorPos "); PrintP3(m.doorPos); printf("\n");
		if (!m.muzzles.empty()) List("muzzles", m.muzzles);
		if (!m.pylons.empty()) List("pylons", m.pylons);
		if (!m.pods.empty()) List("pods", m.pods);
		for (size_t i = 0; i < m.parts.size(); i++) { char k[16]; snprintf(k, sizeof k, "part%zu", i); Sums(k, m.parts[i]); }
		if (m.hasGear) Sums("gear", m.gear);
		if (m.hasGlass) { Sums("glass", m.glass); printf("  glassAt "); PrintP3(m.glassAt); printf("\n"); }
		for (const auto& pr : m.props) { printf("  prop at "); PrintP3(pr.pos); printf(" axis %c rate %.4f disc %.4f\n", pr.axis, pr.rate, pr.disc); Sums("blade", pr.blade); }
		if (m.hasRotor) {
			printf("  rotor at "); PrintP3(m.rotor.pos); printf(" disc %.4f\n", m.rotor.disc); Sums("rotorBlade", m.rotor.blade);
			printf("  tail at "); PrintP3(m.tailRotor.pos); printf(" disc %.4f\n", m.tailRotor.disc); Sums("tailBlade", m.tailRotor.blade);
		}
		for (const auto& f : m.flames) { printf("  flame at "); PrintP3(f.pos); printf("\n"); Sums("flame", f.mesh); }
		for (const auto& l : m.lights) { printf("  light at "); PrintP3(l.pos); printf(" r %.4f%s\n", l.r, l.kind == 2 ? " strobe" : ""); }
		if (m.hasGun) { printf("  gun at "); PrintP3(m.gunAt); printf("\n"); Sums("gunMesh", m.gun); }
		if (m.tank) {
			printf("  turret at "); PrintP3(m.turretAt); printf(" gunPivot "); PrintP3(m.gunPivot); printf("\n");
			Sums("turretMesh", m.turret);
			Sums("gunMesh", m.tankGun);
			printf("  hatch pivot "); PrintP3(m.doorPivot); printf(" axis %c max %.4f\n", m.doorAxis, m.doorMax);
			Sums("hatch", m.hatch);
			for (const auto& set : m.wheelSets) List("wheels", set);
			Sums("roadWheel", m.roadWheel);
		} else if (m.doorAxis) { printf("  door pivot "); PrintP3(m.doorPivot); printf(" axis %c max %.4f\n", m.doorAxis, m.doorMax); }
	}
}
