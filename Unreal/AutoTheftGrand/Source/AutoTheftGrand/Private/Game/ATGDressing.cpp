// See ATGDressing.h.
#include "Game/ATGDressing.h"
#include "Gen/CityMap.h"
#include "Gen/GenMath.h"
#include "Gen/WorldMeshes.h"
#include <cmath>

namespace {
using namespace atg;

// would something of radius r at (x, z) sit in a road, a building or another prop?
struct FSpace {
	const CityMap& Map;
	const std::vector<PropInstance>& Props;
	const std::map<std::string, PropTemplate>& Defs;
	bool Free(double x, double z, double r) const {
		if (Map.IsOnRoad(x, z)) return false;
		for (const Building& B : Map.buildings) {
			// (into the building's own frame when it is turned)
			const double cx = (B.x0 + B.x1) / 2, cz = (B.z0 + B.z1) / 2;
			const double c = std::cos(B.rot), s = std::sin(B.rot);
			const double lx = c * (x - cx) - s * (z - cz) + cx, lz = s * (x - cx) + c * (z - cz) + cz;
			if (lx > B.x0 - r && lx < B.x1 + r && lz > B.z0 - r && lz < B.z1 + r) return false;
		}
		for (const PropInstance& P : Props) {
			auto D = Defs.find(P.type);
			const double pr = D == Defs.end() ? 0.3 : D->second.r * P.scale;
			if ((P.x - x) * (P.x - x) + (P.z - z) * (P.z - z) < (pr + r + 0.15) * (pr + r + 0.15)) return false;
		}
		return true;
	}
};
}

const std::vector<std::string>& ATGDressing::Kinds() {
	static const std::vector<std::string> K = { "utilitybox0", "utilitybox1", "planter0", "planter1", "barrel0", "barrel1", "barrel2", "crate0", "crate1", "cardboard", "trashbag" };
	return K;
}

int ATGDressing::Place(const CityMap& Map, std::vector<PropInstance>& Props, std::map<std::string, PropTemplate>& Defs,
	const std::map<std::string, std::pair<double, double>>& Have) {
	// the templates: no generated mesh (only the model draws them), the model's size as the collision circle;
	// the light things can be knocked over
	for (const auto& H : Have) {
		PropTemplate T;
		T.r = H.second.first; T.h = H.second.second;
		T.breakable = H.first.rfind("barrel", 0) == 0 || H.first.rfind("crate", 0) == 0 || H.first == "cardboard" || H.first == "trashbag";
		Defs[H.first] = std::move(T);
	}
	auto Has = [&](const std::string& K) { return Have.count(K) > 0; };
	RNG R(4242);
	const FSpace Space{ Map, Props, Defs };
	const size_t N = Props.size();
	int Placed = 0;
	auto Put = [&](const std::string& Type, double x, double y, double z, double rot) {
		if (!Has(Type) || !Space.Free(x, z, Defs[Type].r)) return;
		PropInstance P;
		P.type = Type; P.x = x; P.y = y; P.z = z; P.rot = rot; P.scale = 1;
		Props.push_back(P);
		Placed++;
	};
	for (size_t i = 0; i < N; i++) {
		const PropInstance P = Props[i]; // (a copy: the list grows)
		// the prop's own axes: forward is its +z (a street lamp's arm points over the road), side its +x
		const double fx = std::sin(P.rot), fz = std::cos(P.rot), sx = std::cos(P.rot), sz = -std::sin(P.rot);
		const double side = R.Chance(0.5) ? 1 : -1;
		if (P.type == "streetlight") {
			// a utility cabinet behind some of the lamps, facing the road
			if (!R.Chance(0.2)) continue;
			const char* K = R.Chance(0.5) ? "utilitybox0" : "utilitybox1";
			Put(K, P.x - fx * 0.9 + sx * side * 1.2, P.y, P.z - fz * 0.9 + sz * side * 1.2, P.rot);
		} else if (P.type == "bench") {
			// a planter at one end of most benches
			if (!R.Chance(0.6)) continue;
			const char* K = R.Chance(0.5) ? "planter0" : "planter1";
			Put(K, P.x + sx * side * 1.75, P.y, P.z + sz * side * 1.75, P.rot);
		} else if (P.type == "dumpster") {
			// barrels, crates, boxes and bin bags beside the dumpsters, along the wall
			static const char* Junk[] = { "barrel0", "barrel1", "barrel2", "crate0", "crate1", "cardboard", "trashbag", "trashbag" };
			const int Count = R.Int(1, 3);
			for (int k = 0; k < Count; k++) {
				const char* K = Junk[R.Int(0, 7)];
				const double d = 1.35 + k * 0.75;
				Put(K, P.x + sx * side * d + fx * R.Range(-0.2, 0.2), P.y, P.z + sz * side * d + fz * R.Range(-0.2, 0.2), R.Range(0, kTau));
			}
		} else if (P.type == "trashcan") {
			// now and then a bin bag left by the bin
			if (!R.Chance(0.25)) continue;
			Put("trashbag", P.x + sx * side * 0.7, P.y, P.z + sz * side * 0.7, R.Range(0, kTau));
		}
	}
	return Placed;
}
