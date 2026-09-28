// Standalone check of the world generator (no Unreal needed): builds the whole world and prints the same
// summary Tools/dumpworld.mjs prints for the JavaScript original, so the two can be diffed.
//
//   g++ -std=c++20 -O2 -I../Source/AutoTheftGrand/Private/Gen gentest.cpp ../Source/AutoTheftGrand/Private/Gen/*.cpp -o gentest
//   ./gentest > cpp.txt && node dumpworld.mjs > js.txt && diff js.txt cpp.txt
#include "CityMap.h"
#include <chrono>
#include <cstdio>

using namespace atg;

static const char* KindName(ENode k) {
	switch (k) { case ENode::X: return "x"; case ENode::RB: return "rb"; case ENode::End: return "end"; case ENode::Via: return "via"; case ENode::Split: return "split"; case ENode::Merge: return "merge"; }
	return "?";
}
// JavaScript's toFixed(3) (which prints -0 as 0)
static std::string F(double v) {
	char buf[64];
	snprintf(buf, sizeof buf, "%.3f", v);
	if (std::string(buf) == "-0.000") return "0.000";
	return buf;
}

int main() {
	const auto t0 = std::chrono::steady_clock::now();
	CityMap m;
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	fprintf(stderr, "cpp ms %.0f\n", ms);
	const RoadNet& net = m.roads;
	int live = 0; for (const REdge& e : net.edges) if (!e.removed) live++;
	printf("nodes %zu edges %zu live %d\n", net.nodes.size(), net.edges.size(), live);
	double hsum = 0; for (size_t k = 0; k < m.hf.h.size(); k += 7) hsum += m.hf.h[k];
	printf("hsum %.2f\n", hsum);
	printf("blocks %zu buildings %zu lots %zu pads %zu props %zu fences %zu parking %zu pools %zu containers %zu colliders %zu walk %zu towns %zu fixed %zu\n",
		m.blocks.size(), m.buildings.size(), m.lotSurfaces.size(), m.padSurfaces.size(), m.props.size(), m.fences.size(), m.parkingSpots.size(), m.pools.size(),
		m.containers.size(), m.colliders.size(), m.walkNodes.size(), m.townAreas.size(), m.fixedVehicles.size());
	const Vegetation& V = m.vegetation;
	printf("veg pine=%zu oak=%zu bush=%zu cactus=%zu rock=%zu deadtree=%zu palm=%zu\n", V.pine.size() / 5, V.oak.size() / 5, V.bush.size() / 5, V.cactus.size() / 5, V.rock.size() / 5, V.deadtree.size() / 5, V.palm.size() / 5);
	for (const RNode& n : net.nodes) {
		std::string es; for (size_t i = 0; i < n.e.size(); i++) { if (i) es += ","; es += std::to_string(n.e[i]); }
		printf("N %d %s %s %s %s %s %s %s\n", n.id, F(n.x).c_str(), F(n.z).c_str(), F(n.y).c_str(), KindName(n.kind), F(n.r).c_str(), es.c_str(), n.name.c_str());
	}
	for (const REdge& e : net.edges) {
		double ys = 0; for (int i = 0; i < e.n; i++) ys += e.p[i * 3 + 1];
		int dk = 0; for (int i = 0; i < e.n; i++) dk += e.deck[i];
		printf("E %d %d %d %s %d %s %s %d %d %s\n", e.id, e.a, e.b, RoadTypeName(e.type), e.n, F(e.len).c_str(), F(ys).c_str(), dk, e.removed ? 1 : 0, e.name.c_str());
	}
	for (const Building& b : m.buildings)
		printf("B %s %s %s %s %s %s %d %s %s %s %s %s\n", F(b.x0).c_str(), F(b.z0).c_str(), F(b.x1).c_str(), F(b.z1).c_str(), F(b.y0).c_str(), F(b.y1).c_str(), b.style, F(b.rot).c_str(), b.roof.c_str(), b.district.c_str(), b.kind.c_str(), b.name.c_str());
	for (const Prop& p : m.props) printf("P %s %s %s %s\n", p.type.c_str(), F(p.x).c_str(), F(p.z).c_str(), F(p.rot).c_str());
	for (const auto& kv : m.landmarks) printf("L %s %s %s\n", kv.first.c_str(), F(kv.second.x).c_str(), F(kv.second.z).c_str());
	return 0;
}
