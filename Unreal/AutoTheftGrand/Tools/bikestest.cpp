// Prints each bike model's seats, door, grips, feet, head/tail and per-part vertex / index counts, like dumpbikes.mjs.
//   Tools/native.sh bikestest.exe bikestest.cpp && ./bikestest.exe > cppbikes.txt && diff jsbikes.txt cppbikes.txt
#include "BikeModels.h"
#include <cstdio>
using namespace atg;
int main() {
    for (const VehicleDef& d : VehicleDefs()) {
        if (d.bike.empty() || d.board) continue;
        const BikeModel& m = BuildBikeModel(d);
        printf("bike %s\nseats", d.id.c_str());
        for (const Pt3& s : m.seats) printf(" %.4f,%.4f,%.4f", s[0], s[1], s[2]);
        printf("\ndoor %.4f,%.4f\n", m.doorPos[0], m.doorPos[2]);
        printf("grips %.4f,%.4f\n", m.grips[0], m.grips[1]);
        if (m.hasFeet) {
            printf("feet %.4f,%.4f,%.4f %.4f,%.4f,%.4f\n", m.feet[0][0], m.feet[0][1], m.feet[0][2], m.feet[1][0], m.feet[1][1], m.feet[1][2]);
        } else {
            printf("feet none\n");
        }
        printf("headY %.4f headZ %.4f headX %.4f headZSize %.4f\n", m.headY, m.headZ, m.headX, m.headZSize);
        printf("tailY %.4f tailZ %.4f tailX %.4f tailZSize %.4f\n", m.tailY, m.tailZ, m.tailX, m.tailZSize);
        for (const char* k : { "body", "trim", "fork", "wheel", "crank" }) {
            const MeshBuf* mesh = nullptr;
            if (strcmp(k, "body") == 0) mesh = &m.body;
            else if (strcmp(k, "trim") == 0) mesh = &m.trim;
            else if (strcmp(k, "fork") == 0) mesh = &m.fork;
            else if (strcmp(k, "wheel") == 0) mesh = &m.wheel;
            else if (strcmp(k, "crank") == 0) mesh = &m.crank;
            if (!mesh || mesh->Empty()) continue;
            double sx = 0, sy = 0, sz = 0;
            for (size_t i = 0; i < mesh->Count(); i++) { sx += mesh->P[i * 3]; sy += mesh->P[i * 3 + 1]; sz += mesh->P[i * 3 + 2]; }
            printf("part %s v %zu i %zu sum %.2f %.2f %.2f\n", k, mesh->Count(), mesh->I.size(), sx + 0.0, sy + 0.0, sz + 0.0);
        }
        // glass: 8 corners of the glass box after the glass matrix (task spec)
        double sx = 0, sy = 0, sz = 0;
        double hw, hh, hd;
        Mat4 mat;
        if (m.hasGlass) {
            // street motorbike: 0.3 x 0.2 x 0.012 box at glassMatrix
            hw = 0.15; hh = 0.1; hd = 0.006;
            mat = m.glassMatrix;
        } else {
            // dirt motorbike: tiny 0.01 box at origin (identity)
            hw = 0.005; hh = 0.005; hd = 0.005;
            mat = Mat4::Identity();
        }
        for (int ix = -1; ix <= 1; ix += 2) for (int iy = -1; iy <= 1; iy += 2) for (int iz = -1; iz <= 1; iz += 2) {
            double x = ix * hw, y = iy * hh, z = iz * hd;
            mat.Apply(x, y, z);
            sx += x; sy += y; sz += z;
        }
        printf("part glass v 8 i 0 sum %.2f %.2f %.2f\n", sx + 0.0, sy + 0.0, sz + 0.0);
    }
}