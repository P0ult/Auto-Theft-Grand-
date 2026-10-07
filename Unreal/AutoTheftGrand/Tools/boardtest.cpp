// Prints the skateboard model like dumpboard.mjs:
//   Tools/native.sh boardtest.exe boardtest.cpp && ./boardtest.exe > cppboard.txt
//   node --import ./three-hook.mjs dumpboard.mjs > jsboard.txt && diff --strip-trailing-cr jsboard.txt cppboard.txt
#include "BoardModel.h"
#include <cstdio>
using namespace atg;

static double Z(double v) { return v + 0.0; }

int main() {
	for (const VehicleDef& d : VehicleDefs()) {
		if (!d.board) continue;
		const BoardModel& m = BuildBoardModel(d);
		printf("board %s deckY %.4f\n", d.id.c_str(), m.deckY);
		for (const auto& w : m.wheels) printf("wheel %.4f,%.4f,%.4f front %d\n", Z(w.x), Z(w.y), Z(w.z), w.front ? 1 : 0);
		const std::pair<const char*, const MeshBuf*> parts[] = { { "paint", &m.paint }, { "trim", &m.trim }, { "wheel", &m.wheel } };
		for (const auto& [k, g] : parts) {
			double s[9] = {};
			for (size_t i = 0; i < g->Count(); i++) {
				for (int a = 0; a < 3; a++) { s[a] += g->P[i * 3 + a]; s[3 + a] += g->N[i * 3 + a]; }
				s[6] += g->C[1][i * 2]; s[7] += g->C[1][i * 2 + 1]; s[8] += g->C[2][i * 2];
			}
			printf("part %s v %zu i %zu pos %.3f %.3f %.3f nor %.3f %.3f %.3f col %.3f %.3f %.3f\n", k, g->Count(), g->I.size(), Z(s[0]), Z(s[1]), Z(s[2]), Z(s[3]), Z(s[4]), Z(s[5]), Z(s[6]), Z(s[7]), Z(s[8]));
		}
	}
}
