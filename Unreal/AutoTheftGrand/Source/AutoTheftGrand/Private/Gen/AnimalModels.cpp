// Animal models (port of src/entities/animals.js lines 1-162). Line for line with the JS.
// The Animal class (from line 164) is NOT ported here.
#include "AnimalModels.h"
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <cmath>
#include <limits>

namespace atg {

namespace {

// ---------- local math helpers (mirror BikeModels.cpp but with Animal prefix) ----------

struct AnimalQuat { double x, y, z, w; };

static AnimalQuat AnimalQuatFromUnitVectors(double fx, double fy, double fz,
                                             double tx, double ty, double tz) {
    const double r = fx * tx + fy * ty + fz * tz + 1;
    AnimalQuat q;
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

static Mat4 AnimalCompose(double px, double py, double pz, const AnimalQuat& q, double sx, double sy, double sz) {
    const double x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    const double xx = q.x * x2, xy = q.x * y2, xz = q.x * z2, yy = q.y * y2, yz = q.y * z2, zz = q.z * z2, wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    Mat4 r;
    r.m[0] = (1 - (yy + zz)) * sx; r.m[1] = (xy + wz) * sx; r.m[2] = (xz - wy) * sx; r.m[3] = 0;
    r.m[4] = (xy - wz) * sy; r.m[5] = (1 - (xx + zz)) * sy; r.m[6] = (yz + wx) * sy; r.m[7] = 0;
    r.m[8] = (xz + wy) * sz; r.m[9] = (yz - wx) * sz; r.m[10] = (1 - (xx + yy)) * sz; r.m[11] = 0;
    r.m[12] = px; r.m[13] = py; r.m[14] = pz; r.m[15] = 1;
    return r;
}

static Mat4 AnimalRotationX(double a) {
    const double c = std::cos(a), s = std::sin(a);
    Mat4 r;
    r.m[0] = 1;  r.m[1] = 0;   r.m[2] = 0;   r.m[3] = 0;
    r.m[4] = 0;  r.m[5] = c;   r.m[6] = s;   r.m[7] = 0;
    r.m[8] = 0;  r.m[9] = -s;  r.m[10] = c;  r.m[11] = 0;
    r.m[12] = 0; r.m[13] = 0;  r.m[14] = 0;  r.m[15] = 1;
    return r;
}

static Mat4 AnimalMultiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++) for (int row = 0; row < 4; row++) {
        double s = 0;
        for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
        r.m[c * 4 + row] = s;
    }
    return r;
}

static Mat4 AnimalM4(double x, double y, double z, double rx, double ry, double rz, double sx = 1, double sy = 1, double sz = 1) {
    return Mat4::Compose(x, y, z, rx, ry, rz, sx, sy, sz);
}

// ---------- Expanded: convert indexed MeshBuf to non-indexed (one vertex per index) ----------
// Mirrors three.js BufferGeometry.toNonIndexed(): each index becomes a unique vertex.
static MeshBuf AnimalFlat(const MeshBuf& g) {
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

// ---------- rgb: three.js Color(hex) -> linear RGB ----------
static void AnimalRgb(uint32_t hex, double out[3]) {
    auto SrgbToLinear = [](double c) { return c < 0.04045 ? c * 0.0773993808 : std::pow(c * 0.9478672986 + 0.0521327014, 2.4); };
    out[0] = SrgbToLinear(((hex >> 16) & 255) / 255.0);
    out[1] = SrgbToLinear(((hex >> 8) & 255) / 255.0);
    out[2] = SrgbToLinear((hex & 255) / 255.0);
}

// ---------- ellipsoid (SphereGeometry) ----------
static void AnimalEllipsoid(MeshBuf& gb, double x, double y, double z, double rx, double ry, double rz,
                             const double col[3], int seg = 10) {
    gb.Color(col[0], col[1], col[2]);
    MeshBuf s = Geo::Sphere(1, seg, std::max(6, seg - 2));
    gb.Add(AnimalFlat(s), AnimalM4(x, y, z, 0, 0, 0, rx, ry, rz));
}

// ---------- cyl (CylinderGeometry with quaternion from unit vectors) ----------
static void AnimalCyl(MeshBuf& gb, const double a[3], const double b[3], double r0, double r1,
                       const double col[3], int seg = 7) {
    const double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-4) return;
    const double nx = dx / len, ny = dy / len, nz = dz / len;
    gb.Color(col[0], col[1], col[2]);
    MeshBuf c = Geo::Cylinder(r1, r0, len, seg);
    const AnimalQuat q = AnimalQuatFromUnitVectors(0, 1, 0, nx, ny, nz);
    const double mx = (a[0] + b[0]) * 0.5, my = (a[1] + b[1]) * 0.5, mz = (a[2] + b[2]) * 0.5;
    gb.Add(AnimalFlat(c), AnimalCompose(mx, my, mz, q, 1, 1, 1));
}

} // namespace

// ---------- SPECIES table (lines 13-23) ----------
const std::map<std::string, AnimalSpecies>& AnimalSpeciesTable() {
    static const std::map<std::string, AnimalSpecies> table = {
        { "dog",   { "Dog",   true, false, false, 0.72, 0.55, 0.26, 0.28, 0.2, 0.13, 0.16, "flop", "up",   1.5, 7.8, 0, 0, 60, false, false } },
        { "cat",   { "Cat",   true, false, false, 0.46, 0.27, 0.15, 0.16, 0.12, 0.03, 0.06, "up",   "long", 0.9, 5.6, 0, 6, 25, false, false } },
        { "deer",  { "Deer",  true, false, false, 1.25, 1.0,  0.38, 0.42, 0.2,  0.18, 0.46, "up",   "stub", 1.3, 11,  0, 24, 70, true,  false } },
        { "rabbit",{ "Rabbit",true, false, true,  0.32, 0.19, 0.16, 0.17, 0.1,  0.04, 0.04, "long", "puff", 0.8, 6.5, 0, 10, 12, false, false } },
        { "coyote",{ "Coyote",true, false, false, 0.82, 0.6,  0.24, 0.28, 0.19, 0.16, 0.18, "up",   "down", 1.4, 9.5, 0, 18, 50, false, false } },
        { "cow",   { "Cow",   true, false, false, 1.85, 1.35, 0.66, 0.72, 0.3,  0.16, 0.3,  "side", "thin", 0.9, 3.8, 0, 4,  220, false, true  } },
        { "pigeon",{ "Pigeon",false, true,  false, 0.3,  0.2,  0,    0,    0,    0,    0,    "",     "",     0.5, 0,   8, 5,  5,  false, false } },
        { "seagull",{ "Seagull",false, true, false, 0.42, 0.25, 0,    0,    0,    0,    0,    "",     "",     0.6, 0,   9, 6,  6,  false, false } },
        { "crow",  { "Crow",  false, true,  false, 0.36, 0.22, 0,    0,    0,    0,    0,    "",     "",     0.55,0,   9, 7,  5,  false, false } },
    };
    return table;
}

// ---------- BREEDS table (lines 26-45) ----------
const std::map<std::string, AnimalBreed>& AnimalBreeds() {
    static const std::map<std::string, AnimalBreed> table = {
        { "lab",         { "dog",   "Labrador",          { 0xd4a24c, 0xe2b868, 0xc49240, 0xb88a3c }, 1.0,   1200, "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "shepherd",    { "dog",   "German Shepherd",   { 0x2a211a, 0xa66a2c, 0x2c2218, 0x1d1712 }, 1.08,  1800, "up",   "",     NAN,    NAN,    true,  false, 0xffffffff, 0xffffffff } },
        { "husky",       { "dog",   "Husky",             { 0x7f858c, 0xeeeeee, 0xf2f2f2, 0x5e646a }, 1.0,   2000, "up",   "curl", NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "rottweiler",  { "dog",   "Rottweiler",        { 0x141212, 0x8a4b1f, 0x1a1616, 0x121010 }, 1.12,  1600, "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "pug",         { "dog",   "Pug",               { 0xcfa874, 0xd9b888, 0x2a2420, 0x2a2420 }, 0.52,  900,  "",     "curl", 0.75,   0.35,   false, false, 0xffffffff, 0xffffffff } },
        { "poodle",      { "dog",   "Poodle",            { 0xf2efe8, 0xf2efe8, 0xece8e0, 0xf2efe8 }, 0.78,  1400, "",     "",     NAN,    NAN,    false, true,  0xffffffff, 0xffffffff } },
        { "tabby",       { "cat",   "Tabby Cat",         { 0xd98a3c, 0xf0c890, 0xd98a3c, 0xc0783a }, 1.0,   500,  "",     "",     NAN,    NAN,    false, false, 0xa8602a,   0xffffffff } },
        { "blackcat",    { "cat",   "Black Cat",         { 0x1b1a1c, 0x222124, 0x1b1a1c, 0x19181a }, 1.0,   500,  "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "siamese",     { "cat",   "Siamese",           { 0xeadcc6, 0xf1e6d4, 0x5a4638, 0x4a3a2e }, 1.0,   700,  "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        // wildlife
        { "deer",        { "deer",  "",                  { 0x8a5a33, 0xd8c3a0, 0x6e4526, 0x7a4d2c }, 1.0,   -1,   "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "rabbit",      { "rabbit","",                  { 0x8c7a66, 0xd8ccbc, 0x7a6a58, 0x9a8672 }, 1.0,   -1,   "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "coyote",      { "coyote","",                  { 0x9c8260, 0xd8c8aa, 0x8a7050, 0x6e5a40 }, 1.0,   -1,   "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "cow",         { "cow",   "",                  { 0xf0ece4, 0xf0ece4, 0xf0ece4, 0x1b1a18 }, 1.0,   -1,   "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0x1b1a18 } },
        { "brown_cow",   { "cow",   "",                  { 0x7a4a2a, 0xe8e0d0, 0xe8e0d0, 0x5a3a20 }, 1.0,   -1,   "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "pigeon",      { "pigeon","",                  { 0x8e929c, 0x6a6f7a, 0x4d6a64, 0xd0d2d8 }, 1.0,   -1,   "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "seagull",     { "seagull","",                 { 0xf4f4f2, 0xa8adb4, 0xf4f4f2, 0xe8b020 }, 1.0,   -1,   "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
        { "crow",        { "crow",  "",                  { 0x18181c, 0x101014, 0x18181c, 0x303036 }, 1.0,   -1,   "",     "",     NAN,    NAN,    false, false, 0xffffffff, 0xffffffff } },
    };
    return table;
}

// ---------- PET_BREEDS (line 46) ----------
const std::vector<std::string>& PetBreeds() {
    static const std::vector<std::string> v = { "lab", "shepherd", "husky", "rottweiler", "pug", "poodle", "tabby", "blackcat", "siamese" };
    return v;
}

// ---------- AnimalBreedOrder: keys in JS insertion order ----------
const std::vector<std::string>& AnimalBreedOrder() {
    static const std::vector<std::string> v = {
        "lab", "shepherd", "husky", "rottweiler", "pug", "poodle", "tabby", "blackcat", "siamese",
        "deer", "rabbit", "coyote", "cow", "brown_cow", "pigeon", "seagull", "crow"
    };
    return v;
}

// ---------- BuildAnimalParts (port of animals.js buildParts, lines 68-162) ----------
const AnimalParts& BuildAnimalParts(const std::string& breedKey) {
    static std::map<std::string, std::unique_ptr<AnimalParts>> cache;
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    auto it = cache.find(breedKey);
    if (it != cache.end()) return *it->second;

    auto parts = std::make_unique<AnimalParts>();

    const auto& breeds = AnimalBreeds();
    const auto& species = AnimalSpeciesTable();
    auto brIt = breeds.find(breedKey);
    if (brIt == breeds.end()) {
        cache[breedKey] = std::move(parts);
        return *cache[breedKey];
    }
    const AnimalBreed& br = brIt->second;
    auto spIt = species.find(br.species);
    if (spIt == species.end()) {
        cache[breedKey] = std::move(parts);
        return *cache[breedKey];
    }
    const AnimalSpecies& sp = spIt->second;

    const double k = br.scale > 0 ? br.scale : 1.0;

    double cBody[3], cBelly[3], cFace[3], cEar[3];
    AnimalRgb(br.coat[0], cBody);
    AnimalRgb(br.coat[1], cBelly);
    AnimalRgb(br.coat[2], cFace);
    AnimalRgb(br.coat[3], cEar);

    parts->bird = sp.bird;
    parts->k = k;

    if (sp.bird) {
        parts->wingY = 0;
        parts->wingX = 0;
    } else {
        parts->wingY = NaN();
        parts->wingX = NaN();
    }

    if (sp.bird) {
        const double L = sp.len * k;
        const double H = sp.h * k;

        MeshBuf body;
        AnimalEllipsoid(body, 0, H * 0.55, 0, L * 0.22, L * 0.2, L * 0.38, cBody);
        AnimalEllipsoid(body, 0, H * 0.45, -0.02, L * 0.18, L * 0.14, L * 0.3, cBelly);
        AnimalEllipsoid(body, 0, H * 0.9, L * 0.3, L * 0.13, L * 0.13, L * 0.14, cFace);

        double beakCol[3];
        uint32_t beakHex = (br.species == "seagull") ? 0xe8b020 : (br.species == "crow") ? 0x202024 : 0x3a3a3a;
        AnimalRgb(beakHex, beakCol);
        body.Color(beakCol[0], beakCol[1], beakCol[2]);
        MeshBuf cone = Geo::Cone(L * 0.04, L * 0.16, 6);
        body.Add(AnimalFlat(cone), AnimalM4(0, H * 0.88, L * 0.48, kPi / 2, 0, 0));

        body.Color(0.05, 0.05, 0.05);
        MeshBuf eyeSphere = Geo::Sphere(L * 0.025, 5, 4);
        for (int s : { -1, 1 }) {
            body.Add(AnimalFlat(eyeSphere), AnimalM4(s * L * 0.1, H * 0.95, L * 0.38, 0, 0, 0));
        }

        body.Color(cEar[0], cEar[1], cEar[2]);
        MeshBuf tailBox = Geo::Box(L * 0.22, L * 0.03, L * 0.32);
        body.Add(AnimalFlat(tailBox), AnimalM4(0, H * 0.6, -L * 0.45, -0.25, 0, 0));

        double legCol[3];
        uint32_t legHex = (br.species == "pigeon") ? 0xc0504a : (br.species == "seagull") ? 0xd8a060 : 0x202020;
        AnimalRgb(legHex, legCol);
        for (int s : { -1, 1 }) {
            double a[3] = { s * L * 0.07, H * 0.4, 0 };
            double b[3] = { s * L * 0.07, 0, L * 0.02 };
            AnimalCyl(body, a, b, L * 0.015, L * 0.015, legCol, 4);
        }

        MeshBuf wing;
        wing.Color(cBelly[0], cBelly[1], cBelly[2]);
        wing.Box(0, -L * 0.02, -L * 0.28, L * 0.62, L * 0.02, L * 0.22);
        wing.Color(cEar[0], cEar[1], cEar[2]);
        wing.Box(L * 0.35, -L * 0.021, -L * 0.3, L * 0.64, L * 0.021, L * 0.05);

        parts->body = std::move(body);
        parts->wing = std::move(wing);
        parts->wingY = H * 0.68;
        parts->wingX = L * 0.16;

        cache[breedKey] = std::move(parts);
        return *cache[breedKey];
    }

    const double L = sp.len * k;
    const double H = sp.h * k;
    const double W = sp.w * k;
    const double BH = sp.bh * k;

    const double legK = IsSet(br.legK) ? br.legK : 1.0;
    const double hipY = (H - BH * 0.45) * legK + (1 - legK) * BH * 0.3;
    const double bodyY = hipY + BH * 0.3;

    MeshBuf body;
    AnimalEllipsoid(body, 0, bodyY, 0, W * 0.5, BH * 0.5, L * 0.5, cBody, 12);
    AnimalEllipsoid(body, 0, bodyY - BH * 0.12, 0.02 * k, W * 0.44, BH * 0.4, L * 0.42, cBelly, 10);

    if (br.saddle) {
        double saddleCol[3];
        AnimalRgb(0x1a1612, saddleCol);
        AnimalEllipsoid(body, 0, bodyY + BH * 0.18, -L * 0.05, W * 0.46, BH * 0.3, L * 0.36, saddleCol, 10);
    }

    if (br.patches != 0xffffffff) {
        double patchesCol[3];
        AnimalRgb(br.patches, patchesCol);
        const double patchData[4][4] = {
            {0.8, 0.2, 0.2, 0.3},
            {-0.8, 0.1, -0.25, 0.28},
            {0.7, 0.3, -0.3, 0.22},
            {-0.7, 0.35, 0.3, 0.2}
        };
        for (int i = 0; i < 4; i++) {
            AnimalEllipsoid(body,
                patchData[i][0] * W * 0.42,
                bodyY + patchData[i][1] * BH * 0.4,
                patchData[i][2] * L,
                W * 0.22, BH * patchData[i][3], L * 0.18,
                patchesCol, 8);
        }
    }

    if (br.stripes != 0xffffffff) {
        double stripesCol[3];
        AnimalRgb(br.stripes, stripesCol);
        for (int s = -2; s <= 2; s++) {
            AnimalEllipsoid(body,
                0,
                bodyY + BH * 0.35,
                s * L * 0.14,
                W * 0.52, BH * 0.12, L * 0.04,
                stripesCol, 8);
        }
    }

    if (br.fluffy) {
        for (double z : { -0.35, 0.35 }) {
            AnimalEllipsoid(body,
                0,
                bodyY + BH * 0.05,
                z * L,
                W * 0.62, BH * 0.62, L * 0.2,
                cBody, 8);
        }
    }

    if (sp.name == "Cow") {
        double udderCol[3];
        AnimalRgb(0xe8a8a0, udderCol);
        AnimalEllipsoid(body, 0, bodyY - BH * 0.5, -L * 0.28, W * 0.18, BH * 0.14, L * 0.1, udderCol, 8);
    }

    double neckTop[3] = {
        0,
        bodyY + BH * 0.25 + sp.neck * k * (sp.name == "Deer" ? 1.0 : 0.6),
        L * 0.5 + sp.neck * k * 0.35
    };
    double cylStart[3] = { 0, bodyY + BH * 0.1, L * 0.38 };
    double neckR0 = W * 0.3;
    double neckR1 = W * (sp.name == "Deer" ? 0.2 : 0.26);
    AnimalCyl(body, cylStart, neckTop, neckR0, neckR1, cBody, 8);

    parts->body = std::move(body);
    parts->headPos[0] = neckTop[0];
    parts->headPos[1] = neckTop[1];
    parts->headPos[2] = neckTop[2];

    MeshBuf head;
    const double HS = sp.head * k;
    AnimalEllipsoid(head, 0, HS * 0.2, HS * 0.25, HS * 0.62, HS * 0.58, HS * 0.7, cFace, 10);

    const double sn = sp.snout * k * (IsSet(br.snoutK) ? br.snoutK : 1.0);
    if (sn > 0.02) {
        AnimalEllipsoid(head, 0, HS * 0.02, HS * 0.7 + sn * 0.45, HS * 0.34, HS * 0.3, sn * 0.62 + 0.01, cFace, 8);
    }

    head.Color(0.06, 0.05, 0.05);
    MeshBuf noseSphere = Geo::Sphere(HS * 0.12, 6, 4);
    head.Add(AnimalFlat(noseSphere), AnimalM4(0, HS * 0.1, HS * 0.72 + sn * 1.05, 0, 0, 0));

    MeshBuf eyeSphere = Geo::Sphere(HS * 0.09, 6, 4);
    for (int s : { -1, 1 }) {
        head.Add(AnimalFlat(eyeSphere), AnimalM4(s * HS * 0.3, HS * 0.35, HS * 0.72, 0, 0, 0));
    }

    const std::string ears = (!br.ears.empty() ? br.ears : sp.ears);
    for (int s : { -1, 1 }) {
        head.Color(cEar[0], cEar[1], cEar[2]);
        if (ears == "up") {
            MeshBuf earCone = Geo::Cone(HS * 0.2, HS * 0.55, 4);
            head.Add(AnimalFlat(earCone), AnimalM4(s * HS * 0.34, HS * 0.8, HS * 0.1, 0, 0, -s * 0.2));
        } else if (ears == "long") {
            MeshBuf earBox = Geo::Box(HS * 0.22, HS * 1.3, HS * 0.1);
            head.Add(AnimalFlat(earBox), AnimalM4(s * HS * 0.2, HS * 1.1, 0, -0.3, 0, -s * 0.12));
        } else if (ears == "side") {
            MeshBuf earBox = Geo::Box(HS * 0.5, HS * 0.12, HS * 0.28);
            head.Add(AnimalFlat(earBox), AnimalM4(s * HS * 0.72, HS * 0.45, HS * 0.05, 0, 0, s * 0.3));
        } else {
            MeshBuf earBox = Geo::Box(HS * 0.18, HS * 0.6, HS * 0.4);
            head.Add(AnimalFlat(earBox), AnimalM4(s * HS * 0.6, HS * 0.15, HS * 0.15, 0, 0, s * 0.35));
        }
    }

    if (sp.antlers && breedKey == "deer") {
        double ac[3];
        AnimalRgb(0xd8c8a8, ac);
        for (int s : { -1, 1 }) {
            double a1[3] = { s * HS * 0.2, HS * 0.7, HS * 0.1 };
            double b1[3] = { s * HS * 0.9, HS * 1.9, -HS * 0.1 };
            AnimalCyl(head, a1, b1, HS * 0.06, HS * 0.04, ac, 5);
            double a2[3] = { s * HS * 0.55, HS * 1.25, 0 };
            double b2[3] = { s * HS * 0.6, HS * 1.9, HS * 0.4 };
            AnimalCyl(head, a2, b2, HS * 0.04, HS * 0.03, ac, 5);
        }
    }

    if (sp.horns) {
        double hornCol[3];
        AnimalRgb(0xe8dcc0, hornCol);
        for (int s : { -1, 1 }) {
            double a[3] = { s * HS * 0.4, HS * 0.7, HS * 0.1 };
            double b[3] = { s * HS * 0.85, HS * 0.95, HS * 0.25 };
            AnimalCyl(head, a, b, HS * 0.08, HS * 0.04, hornCol, 5);
        }
    }

    parts->head = std::move(head);

    const double legR = W * (sp.name == "Cow" ? 0.16 : 0.14);
    auto mkLeg = [&](double len, const double col[3], const double paw[3]) -> MeshBuf {
        MeshBuf g;
        double a[3] = { 0, 0, 0 };
        double b[3] = { 0, -len + legR, 0 };
        AnimalCyl(g, a, b, legR * 1.25, legR * 0.8, col, 6);
        AnimalEllipsoid(g, 0, -len + legR * 0.8, legR * 0.3, legR * 1.05, legR * 0.8, legR * 1.35, paw, 6);
        return g;
    };

    double frontPaw[3];
    if (sp.name == "Cow" || sp.name == "Deer") {
        AnimalRgb(0x2a2420, frontPaw);
    } else {
        frontPaw[0] = cBelly[0]; frontPaw[1] = cBelly[1]; frontPaw[2] = cBelly[2];
    }
    parts->frontLeg = mkLeg(hipY, cBelly, frontPaw);

    if (sp.hop) {
        MeshBuf g;
        AnimalEllipsoid(g, 0, -hipY * 0.35, -0.02 * k, W * 0.28, hipY * 0.45, L * 0.2, cBody, 8);
        AnimalEllipsoid(g, 0, -hipY + 0.02, 0.04 * k, W * 0.14, 0.025 * k, L * 0.22, cBelly, 6);
        parts->hindLeg = std::move(g);
    } else {
        parts->hindLeg = parts->frontLeg;
    }

    parts->hipY = hipY;
    parts->legX = W * 0.36;
    parts->legZf = L * 0.36;
    parts->legZh = -L * 0.36;

    MeshBuf tail;
    const std::string tt = (!br.tail.empty() ? br.tail : sp.tail);
    if (tt == "puff") {
        AnimalEllipsoid(tail, 0, 0, -0.03 * k, 0.05 * k, 0.05 * k, 0.05 * k, cBelly, 6);
    } else if (tt == "stub") {
        AnimalEllipsoid(tail, 0, 0.02 * k, -0.05 * k, W * 0.14, 0.08 * k, 0.05 * k, cBelly, 6);
    } else if (tt == "curl") {
        AnimalEllipsoid(tail, 0, 0.05 * k, -0.03 * k, W * 0.18, W * 0.18, W * 0.18, cBody, 6);
    } else {
        double tl = 0;
        if (tt == "long") tl = L * 0.7;
        else if (tt == "thin") tl = L * 0.45;
        else tl = L * 0.5;

        double up = 0;
        if (tt == "down") up = -0.9;
        else if (tt == "thin") up = -1.3;
        else if (tt == "long") up = 0.6;
        else up = 0.35;

        double a[3] = { 0, 0, 0 };
        double b[3] = { 0, std::sin(up) * tl, -std::cos(up) * tl };
        double r0 = W * (tt == "down" ? 0.16 : 0.08);
        double r1 = W * (tt == "down" ? 0.1 : 0.05);
        AnimalCyl(tail, a, b, r0, r1, cBody, 6);
    }

    parts->tail = std::move(tail);
    parts->tailPos[0] = 0;
    parts->tailPos[1] = bodyY + BH * 0.2;
    parts->tailPos[2] = -L * 0.48;

    cache[breedKey] = std::move(parts);
    return *cache[breedKey];
}

} // namespace atg