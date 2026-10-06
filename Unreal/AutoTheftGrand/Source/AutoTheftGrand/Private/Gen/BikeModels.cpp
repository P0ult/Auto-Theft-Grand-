// Procedural two-wheeler models (port of src/entities/bikes.js lines 1-185). Line for line with the JS:
// tube, wheelGeometry, buildBikeModel. The Bike class (physics, from line 188) is NOT ported here.
#include "BikeModels.h"
#include <map>
#include <memory>
#include <mutex>

namespace atg {

namespace {

// ---------- local math helpers copied from Sim/Core.cpp (Gen must not include Sim headers) ----------

struct Quat { double x, y, z, w; };

static Quat QuatFromUnitVectors(const double fx, const double fy, const double fz,
                                const double tx, const double ty, const double tz) {
    const double r = fx * tx + fy * ty + fz * tz + 1;
    Quat q;
    if (r < 1e-8) {
        if (std::fabs(fx) > std::fabs(fz)) { q.x = -fy; q.y = fx; q.z = 0; q.w = r; }
        else { q.x = 0; q.y = -fz; q.z = fy; q.w = r; }
    } else {
        q.x = fy * tz - fz * ty;
        q.y = fz * tx - fx * tz;
        q.z = fx * ty - fy * tx;
        q.w = r;
    }
    const double l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l > 0) { q.x /= l; q.y /= l; q.z /= l; q.w /= l; }
    return q;
}

static Mat4 M4Compose(double px, double py, double pz, const Quat& q, double sx, double sy, double sz) {
    const double x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    const double xx = q.x * x2, xy = q.x * y2, xz = q.x * z2, yy = q.y * y2, yz = q.y * z2, zz = q.z * z2, wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    Mat4 r;
    r.m[0] = (1 - (yy + zz)) * sx; r.m[1] = (xy + wz) * sx; r.m[2] = (xz - wy) * sx; r.m[3] = 0;
    r.m[4] = (xy - wz) * sy; r.m[5] = (1 - (xx + zz)) * sy; r.m[6] = (yz + wx) * sy; r.m[7] = 0;
    r.m[8] = (xz + wy) * sz; r.m[9] = (yz - wx) * sz; r.m[10] = (1 - (xx + yy)) * sz; r.m[11] = 0;
    r.m[12] = px; r.m[13] = py; r.m[14] = pz; r.m[15] = 1;
    return r;
}

static Mat4 MakeRotationX(double a) {
    const double c = std::cos(a), s = std::sin(a);
    Mat4 r;
    r.m[0] = 1;  r.m[1] = 0;   r.m[2] = 0;   r.m[3] = 0;
    r.m[4] = 0;  r.m[5] = c;   r.m[6] = s;   r.m[7] = 0;
    r.m[8] = 0;  r.m[9] = -s;  r.m[10] = c;  r.m[11] = 0;
    r.m[12] = 0; r.m[13] = 0;  r.m[14] = 0;  r.m[15] = 1;
    return r;
}

static Mat4 M4Multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++) for (int row = 0; row < 4; row++) {
        double s = 0;
        for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
        r.m[c * 4 + row] = s;
    }
    return r;
}

static Mat4 M4(double x, double y, double z, double rx, double ry, double rz, double sx = 1, double sy = 1, double sz = 1) {
    return Mat4::Compose(x, y, z, rx, ry, rz, sx, sy, sz);
}

// ---------- Expanded: convert indexed MeshBuf to non-indexed (one vertex per index) ----------
// Mirrors three.js BufferGeometry.toNonIndexed(): each index becomes a unique vertex.
// Copies P, N, C[0] (uv); does NOT copy C[1..3] (colour channels come from builder at Add time).
static MeshBuf Expanded(const MeshBuf& g) {
    if (g.I.empty()) return g;
    MeshBuf r;
    r.P.reserve(g.I.size() * 3);
    r.N.reserve(g.I.size() * 3);
    r.C[0].reserve(g.I.size() * 2);
    for (uint32_t idx : g.I) {
        r.P.push_back(g.P[idx * 3]);
        r.P.push_back(g.P[idx * 3 + 1]);
        r.P.push_back(g.P[idx * 3 + 2]);
        r.N.push_back(g.N[idx * 3]);
        r.N.push_back(g.N[idx * 3 + 1]);
        r.N.push_back(g.N[idx * 3 + 2]);
        r.C[0].push_back(g.C[0][idx * 2]);
        r.C[0].push_back(g.C[0][idx * 2 + 1]);
    }
    r.I.resize(g.I.size());
    for (size_t i = 0; i < g.I.size(); i++) r.I[i] = (uint32_t)i;
    return r;
}

// ------------------------------------------------------------------ tube (line 14-22)

static void Tube(MeshBuf& gb, const double a[3], const double b[3], double r, int seg = 8) {
    const double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-4) return;
    const double nx = dx / len, ny = dy / len, nz = dz / len;
    MeshBuf g = Geo::Cylinder(r, r, len, seg, 1, false);
    const Quat q = QuatFromUnitVectors(0, 1, 0, nx, ny, nz);
    const double mx = (a[0] + b[0]) * 0.5, my = (a[1] + b[1]) * 0.5, mz = (a[2] + b[2]) * 0.5;
    gb.Add(Expanded(g), M4Compose(mx, my, mz, q, 1, 1, 1));
}

// ------------------------------------------------------------------ wheelGeometry (line 25-45)

static MeshBuf WheelGeometry(double R, double tw, bool moto, bool knobbly) {
    MeshBuf gb;
    gb.Color(0.05, 0.05, 0.055);
    gb.Add(Expanded(Geo::Torus(R - tw / 2, tw / 2, 8, 30)), M4(0, 0, 0, 0, kPi / 2, 0, 1, 1, 1));
    if (knobbly) {
        for (int k = 0; k < 22; k++) {
            const double a = k / 22.0 * kTau;
            gb.Add(Expanded(Geo::Box(tw * 1.05, 0.03, 0.04)), M4Multiply(MakeRotationX(a), M4(0, R - 0.012, 0, 0, 0, 0, 1, 1, 1)));
        }
    }
    const double rimR = R - tw * (moto ? 0.95 : 0.9);
    gb.Color(moto ? 0.2 : 0.75, moto ? 0.2 : 0.75, moto ? 0.22 : 0.78);
    gb.Add(Expanded(Geo::Torus(rimR, moto ? 0.018 : 0.01, 5, 28)), M4(0, 0, 0, 0, kPi / 2, 0, 1, 1, 1));
    gb.Color(0.55, 0.55, 0.58);
    gb.Add(Expanded(Geo::Cylinder(moto ? 0.06 : 0.025, moto ? 0.06 : 0.025, moto ? 0.16 : 0.1, 10)), M4(0, 0, 0, 0, 0, kPi / 2, 1, 1, 1));
    if (moto) {
        gb.Color(0.18, 0.18, 0.2);
        for (int k = 0; k < 5; k++) {
            gb.Add(Expanded(Geo::Box(0.03, rimR - 0.05, 0.035)), M4Multiply(MakeRotationX(k / 5.0 * kTau), M4(0, (rimR + 0.05) / 2, 0, 0, 0, 0, 1, 1, 1)));
        }
        gb.Color(0.62, 0.62, 0.64);
        gb.Add(Expanded(Geo::Cylinder(R * 0.5, R * 0.5, 0.008, 20)), M4(0.07, 0, 0, 0, 0, kPi / 2, 1, 1, 1)); // brake disc
    } else {
        gb.Color(0.8, 0.8, 0.82);
        for (int k = 0; k < 18; k++) {
            const double a = k / 18.0 * kTau;
            const int s = k % 2 ? 1 : -1;
            double a1[3] = { s * 0.025, std::cos(a) * 0.03, std::sin(a) * 0.03 };
            double b1[3] = { 0, std::cos(a + 0.2) * rimR, std::sin(a + 0.2) * rimR };
            Tube(gb, a1, b1, 0.0035, 3);
        }
    }
    return std::move(gb);
}

} // namespace

// ------------------------------------------------------------------ BuildBikeModel (line 47-185)

const BikeModel& BuildBikeModel(const VehicleDef& def) {
    static std::map<std::string, std::unique_ptr<BikeModel>> cache;
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(def.id);
    if (it != cache.end()) return *it->second;

    auto model = std::make_unique<BikeModel>();
    const double R = def.wheelR;
    const double zf = def.wheelbase / 2;
    const double zr = -def.wheelbase / 2;
    const bool moto = def.bike == "moto";
    const bool dirt = def.offroad;
    auto dark = [](MeshBuf& gb, double k = 0.12) { gb.Color(k, k, k + 0.01); };
    auto chrome = [](MeshBuf& gb) { gb.Color(0.72, 0.72, 0.75); };

    // steering head (top of the fork) and where the fork meets the axle, relative to the front axle
    const double head[3] = { 0, (moto ? (dirt ? 1.02 : 0.95) : 0.93) - R, moto ? -0.2 : -0.1 };
    const double hy = R + head[1];
    const double hz = zf + head[2];

    // head and tail meshes (small boxes)
    // We'll store their positions and sizes in the model struct
    model->headY = moto ? (dirt ? 0.95 : 0.84) : 0.86;
    model->headZ = moto ? (dirt ? zf - 0.12 : 0.795) : zf + 0.02;
    model->headX = moto ? 0.08 : 0.025; // half-width of the head box
    model->headZSize = moto ? 0.015 : 0.015; // half-depth of the head box

    model->tailY = moto ? (dirt ? 0.93 : 0.88) : 0.9;
    model->tailZ = moto ? (dirt ? -0.96 : -0.87) : -0.42;
    model->tailX = moto ? 0.07 : 0.025;
    model->tailZSize = moto ? 0.01 : 0.01;

    MeshBuf paint, trim, forkBuf;
    paint.Color(1, 1, 1);

    if (moto) {
        const double pivY = dirt ? 0.5 : 0.45;
        const double pivZ = -0.16;
        dark(trim, 0.16);
        for (const double x : { -0.09, 0.09 }) {
            double a1[3] = { x, hy - 0.05, hz - 0.02 };
            double b1[3] = { x, pivY + 0.08, pivZ };
            Tube(trim, a1, b1, 0.035); // frame spars
            double a2[3] = { x, pivY, pivZ };
            double b2[3] = { x * 1.1, R, zr };
            Tube(trim, a2, b2, 0.03); // swingarm
        }
        double a3[3] = { 0, hy, hz };
        double b3[3] = { 0, dirt ? 0.95 : 0.88, -0.45 };
        Tube(trim, a3, b3, 0.028); // subframe

        // engine & exhaust
        trim.Color(0.3, 0.3, 0.32);
        trim.Box(-0.15, dirt ? 0.3 : 0.22, -0.12, 0.15, dirt ? 0.62 : 0.6, 0.32);
        trim.Color(0.22, 0.22, 0.24);
        trim.Box(-0.11, 0.6, 0.12, 0.11, 0.74, 0.34);
        chrome(trim);
        if (dirt) {
            double a4[3] = { 0.12, 0.45, 0.25 };
            double b4[3] = { 0.16, 0.72, -0.2 };
            Tube(trim, a4, b4, 0.035);
            double a5[3] = { 0.16, 0.72, -0.2 };
            double b5[3] = { 0.17, 0.8, -0.7 };
            Tube(trim, a5, b5, 0.05);
        } else {
            double a4[3] = { 0.1, 0.25, 0.25 };
            double b4[3] = { 0.14, 0.3, -0.2 };
            Tube(trim, a4, b4, 0.035);
            double a5[3] = { 0.14, 0.3, -0.2 };
            double b5[3] = { 0.17, 0.52, -0.72 };
            Tube(trim, a5, b5, 0.06);
        }

        if (dirt) {
            paint.Box(-0.15, 0.78, 0.02, 0.15, 0.98, 0.38, true);
            paint.Box(-0.2, 0.62, 0.2, 0.2, 0.9, 0.36); // radiator shrouds
            dark(trim, 0.08);
            trim.Box(-0.12, 0.92, -0.62, 0.12, 1.0, 0.18, true); // long flat seat
            paint.Box(-0.1, 0.9, -0.95, 0.1, 0.96, -0.55); // rear fender
            model->seats.push_back({ 0, 1.0, -0.22 });
        } else {
            paint.Box(-0.17, 0.78, 0.02, 0.17, 1.02, 0.46, true);
            paint.Box(-0.19, 0.5, 0.18, 0.19, 0.78, 0.5); // side fairings
            dark(trim, 0.07);
            trim.Box(-0.14, 0.86, -0.48, 0.14, 0.94, 0.04, true);
            dark(trim, 0.09);
            trim.Box(-0.12, 0.93, -0.72, 0.12, 0.99, -0.46, true);
            paint.Box(-0.12, 0.82, -0.86, 0.12, 0.93, -0.46); // tail cowl
            paint.Box(-0.2, 0.55, 0.46, 0.2, 1.06, 0.78, true); // front fairing
            model->seats.push_back({ 0, 0.94, -0.2 });
            model->seats.push_back({ 0, 1.0, -0.58 });
            model->hasGlass = true;
            // glass: BoxGeometry(0.3, 0.2, 0.012) at position (0, 1.12, 0.7) rotation.x = -0.75
            // three.js: matrix = T * R (compose applies scale, rotation, translation)
            model->glassMatrix = M4Multiply(M4(0, 1.12, 0.7, 0, 0, 0, 1, 1, 1), MakeRotationX(-0.75));
        }
        model->feet[0][0] = 0.2; model->feet[0][1] = -0.5; model->feet[0][2] = -0.02;
        model->feet[1][0] = -0.2; model->feet[1][1] = -0.5; model->feet[1][2] = -0.02;
        model->hasFeet = true;
    } else {
        // diamond frame bicycle
        const double bbY = 0.3, bbZ = -0.02, stY = 0.86, stZ = -0.2;
        double a1[3] = { 0, hy, hz };
        double b1[3] = { 0, bbY, bbZ };
        Tube(paint, a1, b1, 0.02); // down tube
        double a2[3] = { 0, bbY, bbZ };
        double b2[3] = { 0, stY, stZ };
        Tube(paint, a2, b2, 0.018); // seat tube
        double a3[3] = { 0, stY - 0.03, stZ + 0.02 };
        double b3[3] = { 0, hy + 0.02, hz };
        Tube(paint, a3, b3, 0.017); // top tube
        for (const double x : { -0.05, 0.05 }) {
            double a4[3] = { 0, bbY, bbZ };
            double b4[3] = { x, R, zr };
            Tube(paint, a4, b4, 0.012); // chain stays
            double a5[3] = { 0, stY - 0.05, stZ };
            double b5[3] = { x, R, zr };
            Tube(paint, a5, b5, 0.011); // seat stays
        }
        chrome(trim);
        double a6[3] = { 0, stY, stZ };
        double b6[3] = { 0, 0.95, -0.24 };
        Tube(trim, a6, b6, 0.012); // seat post
        dark(trim, 0.06);
        trim.Box(-0.07, 0.95, -0.38, 0.07, 0.99, -0.12, true); // saddle

        // cranks & pedals
        model->hasFeet = false;
        model->crankY = bbY;
        model->crankZ = bbZ;
        MeshBuf cg;
        chrome(cg);
        cg.Add(Expanded(Geo::Cylinder(0.09, 0.09, 0.01, 18)), M4(0.06, 0, 0, 0, 0, kPi / 2, 1, 1, 1)); // chainring
        for (const int s : { 1, -1 }) {
            cg.Color(0.3, 0.3, 0.32);
            cg.Box(s * 0.08 - 0.012, -0.015, 0, s * 0.08 + 0.012, 0.015, s * 0.17);
            dark(cg, 0.1);
            const double x1 = s * 0.08 + (s > 0 ? 0 : -0.1);
            const double x2 = s * 0.08 + (s > 0 ? 0.1 : 0);
            cg.Box(x1, -0.012, s * 0.17 - 0.035, x2, 0.012, s * 0.17 + 0.035);
        }
        model->crank = std::move(cg);
        model->seats.push_back({ 0, 0.98, -0.26 });
    }

    // glass placeholder if none (invisible)
    if (!model->hasGlass) {
        model->hasGlass = false;
    }

    model->body = std::move(paint);
    model->trim = std::move(trim);

    // wheels
    const double tw = moto ? (dirt ? 0.12 : 0.15) : 0.035;
    model->wheel = WheelGeometry(R, tw, moto, dirt);
    model->wheels.push_back({ zf, true, R });
    model->wheels.push_back({ zr, false, R });

    // fork (front only)
    if (moto) {
        chrome(forkBuf);
        for (const double x : { -0.1, 0.1 }) {
            double a1[3] = { x, 0, 0 };
            double b1[3] = { x, head[1], head[2] };
            Tube(forkBuf, a1, b1, 0.028);
        }
        dark(forkBuf, 0.1);
        forkBuf.Box(-0.12, head[1] - 0.04, head[2] - 0.05, 0.12, head[1] + 0.02, head[2] + 0.05); // triple clamp
        double a7[3] = { -0.34, head[1] + 0.08, head[2] - 0.08 };
        double b7[3] = { 0.34, head[1] + 0.08, head[2] - 0.08 };
        Tube(forkBuf, a7, b7, 0.016); // bars
        if (dirt) {
            forkBuf.Color(0.9, 0.9, 0.9);
            forkBuf.Box(-0.08, R + 0.05, -0.25, 0.08, R + 0.09, 0.2); // high fender
        } else {
            forkBuf.Color(0.15, 0.15, 0.16);
            forkBuf.Box(-0.08, R * 0.7, -0.25, 0.08, R + 0.06, 0.2);
        }
    } else {
        chrome(forkBuf);
        for (const double x : { -0.035, 0.035 }) {
            double a1[3] = { x, 0, 0 };
            double b1[3] = { x * 0.3, head[1], head[2] };
            Tube(forkBuf, a1, b1, 0.011);
        }
        double a2[3] = { 0, head[1], head[2] };
        double b2[3] = { 0, head[1] + 0.12, head[2] - 0.04 };
        Tube(forkBuf, a2, b2, 0.013); // stem
        dark(forkBuf, 0.15);
        double a3[3] = { -0.28, head[1] + 0.12, head[2] - 0.06 };
        double b3[3] = { 0.28, head[1] + 0.12, head[2] - 0.06 };
        Tube(forkBuf, a3, b3, 0.012); // bars
    }
    model->fork = std::move(forkBuf);

    model->doorPos = { 0.8, 0, -0.1 };
    model->grips[0] = head[1] + R + (moto ? 0.08 : 0.12);
    model->grips[1] = zf + head[2] - (moto ? 0.08 : 0.06);

    cache[def.id] = std::move(model);
    return *cache[def.id];
}

} // namespace atg