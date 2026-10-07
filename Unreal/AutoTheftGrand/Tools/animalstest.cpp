// Prints each animal breed model like dumpanimals.mjs:
//   Tools/native.sh animalstest.exe animalstest.cpp && ./animalstest.exe > cppanimals.txt
//   node --import ./three-hook.mjs dumpanimals.mjs > jsanimals.txt && diff --strip-trailing-cr jsanimals.txt cppanimals.txt
#include "AnimalModels.h"
#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
using namespace atg;

// (a near-zero sum prints as 0, whatever its sign)
static double Z2(double v) { return std::fabs(v) < 0.005 ? 0.0 : v; }

static std::string Sums(const MeshBuf& g) {
    double s[9] = {};
    for (size_t i = 0; i < g.Count(); i++) {
        for (int a = 0; a < 3; a++) { s[a] += g.P[i * 3 + a]; s[3 + a] += g.N[i * 3 + a]; }
        if (!g.C[1].empty()) { s[6] += g.C[1][i * 2]; s[7] += g.C[1][i * 2 + 1]; s[8] += g.C[2][i * 2]; }
    }
    char buf[256];
    snprintf(buf, sizeof buf, "v %zu i %zu pos %.2f %.2f %.2f nor %.2f %.2f %.2f col %.2f %.2f %.2f", g.Count(), g.I.size(), Z2(s[0]), Z2(s[1]), Z2(s[2]), Z2(s[3]), Z2(s[4]), Z2(s[5]), Z2(s[6]), Z2(s[7]), Z2(s[8]));
    return buf;
}

static std::string Fmt4(double v) {
    if (std::isnan(v)) return "NaN";
    char buf[32];
    snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

static double Z(double v) { return v + 0.0; }

int main() {
    for (const std::string& key : AnimalBreedOrder()) {
        const AnimalParts& P = BuildAnimalParts(key);
        printf("breed %s\n", key.c_str());
        printf("bird %s k %s wingY %s wingX %s headPos %s,%s,%s hipY %s legX %s legZf %s legZh %s tailPos %s,%s,%s\n",
            Fmt4(P.bird ? 1.0 : 0.0).c_str(), Fmt4(P.k).c_str(), Fmt4(P.wingY).c_str(), Fmt4(P.wingX).c_str(),
            Fmt4(P.headPos[0]).c_str(), Fmt4(P.headPos[1]).c_str(), Fmt4(P.headPos[2]).c_str(),
            Fmt4(P.hipY).c_str(), Fmt4(P.legX).c_str(), Fmt4(P.legZf).c_str(), Fmt4(P.legZh).c_str(),
            Fmt4(P.tailPos[0]).c_str(), Fmt4(P.tailPos[1]).c_str(), Fmt4(P.tailPos[2]).c_str());
        const std::pair<const char*, const MeshBuf*> parts[] = {
            { "body", &P.body }, { "wing", &P.wing }, { "head", &P.head },
            { "frontLeg", &P.frontLeg }, { "hindLeg", &P.hindLeg }, { "tail", &P.tail }
        };
        for (const auto& [k, g] : parts) if (!g->Empty()) printf("part %s %s\n", k, Sums(*g).c_str());
    }
}