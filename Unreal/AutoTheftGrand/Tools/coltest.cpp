// Prints the road network's collision primitives (see dumpcol.mjs).
#include "WorldMeshes.h"
#include <cstdio>
using namespace atg;
static void P(const ColPrim& c) {
	if (c.kind == ColPrim::OBox) printf("O %s %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", c.type.c_str(), c.cx, c.cz, c.hx, c.hz, c.yaw, c.minY, c.maxY);
	else if (c.kind == ColPrim::Circle) printf("C %s %.3f %.3f %.3f %.3f\n", c.type.c_str(), c.x, c.z, c.r, c.h);
	else if (c.kind == ColPrim::Deck) printf("D %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %d\n", c.ax, c.az, c.ay, c.bx, c.bz, c.by, c.hl, c.hr, c.pavement ? 1 : 0);
	else printf("B %s\n", c.type.c_str());
}
int main() {
	CityMap m;
	RoadMeshes r = BuildRoadMeshes(m);
	for (const ColPrim& c : r.prims) P(c);
	for (const ColPrim& c : r.decks) P(c);
}
