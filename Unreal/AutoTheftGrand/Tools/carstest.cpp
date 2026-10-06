// Prints each car model's seats, door, hull and per-part vertex / index counts, like dumpcars.mjs.
//   Tools/native.sh carstest.exe carstest.cpp && ./carstest.exe > cppcars.txt && diff jscars.txt cppcars.txt
#include "VehicleModels.h"
#include <cstdio>
using namespace atg;
int main() {
	for (const VehicleDef& d : VehicleDefs()) {
		if (!d.kind.empty() || !d.bike.empty()) continue;
		const VehicleModel& m = BuildVehicleModel(d);
		printf("car %s\nseats", d.id.c_str());
		for (const Pt3& s : m.seats) printf(" %.4f,%.4f,%.4f", s[0], s[1], s[2]);
		printf("\ndoor %.4f,%.4f hinge %.4f,%.4f,%.4f\n", m.doorPos[0], m.doorPos[2], m.doorHinge[0], m.doorHinge[1], m.doorHinge[2]);
		printf("hull %.4f %.4f %.4f %.4f %.4f %.4f\n", m.hull.roofX, m.hull.roofY, m.hull.roofZ0, m.hull.roofZ1, m.hull.beltY, m.hull.cgH);
		for (const char* k : { "body", "hood", "trunk", "door2", "bumperF", "bumperR", "glass", "trim", "head", "tail", "door" }) {
			for (const VPart& p : m.parts) {
				if (p.name != k) continue;
				double sx = 0, sy = 0, sz = 0;
				for (size_t i = 0; i < p.mesh.Count(); i++) { sx += p.mesh.P[i * 3]; sy += p.mesh.P[i * 3 + 1]; sz += p.mesh.P[i * 3 + 2]; }
				// (the hinged panels are stored relative to their hinges here; three.js keeps the hood / trunk in the car frame)
				if (p.pivot == "hood") { sy += m.hoodHinge[1] * p.mesh.Count(); sz += m.hoodHinge[2] * p.mesh.Count(); }
				if (p.pivot == "trunk") { sy += m.trunkHinge[1] * p.mesh.Count(); sz += m.trunkHinge[2] * p.mesh.Count(); }
				printf("part %s v %zu i %zu sum %.2f %.2f %.2f\n", k, p.mesh.Count(), p.mesh.I.size(), sx + 0.0, sy + 0.0, sz + 0.0);
			}
		}
		printf("wheel v %zu\n", m.wheel.Count());
	}
}
