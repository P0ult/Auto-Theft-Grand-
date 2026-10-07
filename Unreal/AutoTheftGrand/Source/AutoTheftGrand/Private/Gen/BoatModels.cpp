#include "BoatModels.h"
#include <map>
#include <memory>
#include <mutex>

namespace atg {

namespace {

Mat4 BoatM4(double x, double y, double z, double rx = 0, double ry = 0, double rz = 0, double sx = 1, double sy = 1, double sz = 1) {
	return Mat4::Compose(x, y, z, rx, ry, rz, sx, sy, sz);
}

// hullDesign (line 17-27)
void BoatHullDesign(const VehicleDef& def, double& L, double& B, double& draft, double& freeboard,
                           double& dead, double& sheerRise, double& bowStart,
                           double& cockpit0, double& cockpit1, double& cockDepth, bool& tubes) {
    L = def.L;
    B = def.W;
    draft = def.draft;
    freeboard = def.freeboard;
    dead = 0.38;
    sheerRise = 0.28;
    bowStart = 0.5;
    cockpit0 = 0.03;
    cockpit1 = 0.62;
    cockDepth = 0.42;
    tubes = false;
    if (def.boat == "rib") {
        dead = 0.3;
        sheerRise = 0.12;
        bowStart = 0.55;
        cockpit0 = 0.02;
        cockpit1 = 0.8;
        cockDepth = 0.3;
        tubes = true;
    } else if (def.boat == "jetski") {
        dead = 0.3;
        sheerRise = 0.1;
        bowStart = 0.45;
        cockpit0 = 0;
        cockpit1 = 0;
        cockDepth = 0;
    } else if (def.boat == "cruiser") {
        dead = 0.32;
        sheerRise = 0.45;
        bowStart = 0.5;
        cockpit0 = 0.02;
        cockpit1 = 0.3;
        cockDepth = 0.35;
    } else if (def.boat == "police") {
        dead = 0.36;
        sheerRise = 0.3;
        bowStart = 0.52;
        cockpit0 = 0.03;
        cockpit1 = 0.42;
        cockDepth = 0.4;
    }
}

// Helper for S.keel
double BoatKeel(double t, double draft, double freeboard) {
    const double u = Clamp((t - 0.55) / 0.45, 0.0, 1.0);
    return -draft + u * u * (draft + freeboard * 0.55);
}

// Helper for S.sheer
double BoatSheer(double t, double freeboard, double sheerRise) {
    return freeboard + sheerRise * t * t;
}

// Helper for S.half
double BoatHalf(double t, double bowStart, double B) {
    const double u = (t - bowStart) / (1 - bowStart);
    return B / 2 * (t < bowStart ? 1 - 0.04 * (1 - t / bowStart) : std::sqrt(Max(0.0, 1 - u * u)) * (1 - 0.03 * u)) + 0.001;
}

} // namespace

// ------------------------------------------------------------------ BuildBoatModel

const BoatModel& BuildBoatModel(const VehicleDef& def) {
    static std::map<std::string, std::unique_ptr<BoatModel>> cache;
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    auto found = cache.find(def.id);
    if (found != cache.end()) return *found->second;

    auto model = std::make_unique<BoatModel>();

    // hullDesign parameters
    double L, B, draft, freeboard, dead, sheerRise, bowStart, cockpit0, cockpit1, cockDepth;
    bool tubes;
    BoatHullDesign(def, L, B, draft, freeboard, dead, sheerRise, bowStart, cockpit0, cockpit1, cockDepth, tubes);

    const double z0 = -L / 2;

    // Parts
    Part P_hull, P_deck, P_trim, P_glass, P_chrome, P_head, P_tail;

    const bool police = def.police;

    // bottom color
    const double bottom[3] = { police ? 0.12 : (def.boat == "rib" ? 0.25 : 0.55),
                               police ? 0.14 : (def.boat == "rib" ? 0.25 : 0.12),
                               police ? 0.2 : (def.boat == "rib" ? 0.27 : 0.1) };

    auto hullCol = [&](double x, double y) -> Pt3 {
        if (y < 0.06) return { bottom[0], bottom[1], bottom[2] };
        if (y < 0.13) return { 0.95, 0.95, 0.95 };
        if (police && y > freeboard * 0.55 && y < freeboard * 0.8) return { 0.1, 0.25, 0.8 };
        return { 1, 1, 1 };
    };

    // stations for hull
    std::vector<double> extraHull = { bowStart, cockpit0, cockpit1 };
    std::vector<double> zs = Stations(0, 1, 0.025, extraHull);

    // ring function
    auto ringFunc = [&](double t) -> std::vector<std::array<double, 2>> {
        const double b = BoatHalf(t, bowStart, B);
        const double k = BoatKeel(t, draft, freeboard);
        const double sh = BoatSheer(t, freeboard, sheerRise);
        const double chineX = b * 0.86;
        const double chineY = k + chineX * std::tan(dead) * (1 - 0.5 * Clamp((t - 0.6) / 0.4, 0.0, 1.0));
        std::vector<std::array<double, 2>> pts;
        for (int i = 0; i <= 4; i++) {
            const double f = i / 4.0;
            pts.push_back({ chineX * f, k + (chineY - k) * (f * 0.85 + 0.15 * f * f) });
        }
        pts.push_back({ b * 0.9, Min(sh - 0.02, chineY + 0.04) });
        for (int i = 1; i <= 4; i++) {
            const double f = i / 4.0;
            const double y = Min(sh, chineY + 0.04 + (sh - chineY - 0.04) * f);
            pts.push_back({ b * (0.9 + 0.1 * std::sin(f * kPi / 2)), y });
        }
        return pts;
    };

    // hull shell: keel -> chine -> topsides -> sheer, right half then mirrored
    for (int side : { 1, -1 }) {
        std::vector<std::vector<Pt3>> rows;
        for (double t : zs) {
            const auto r = ringFunc(t);
            std::vector<Pt3> row;
            for (const auto& p : r) {
                row.push_back({ p[0] * side, p[1], z0 + t * L });
            }
            rows.push_back(row);
        }
        GridOpts o; o.ref = { 0, -0.2, 0 };
        o.colorAt = [&](double x, double y, double z, Part*) { return hullCol(x, y); };
        EmitGrid(rows, [&](int, int) { return &P_hull; }, o);
    }

    // transom
    {
        const auto r0 = ringFunc(0);
        std::vector<Pt3> pts;
        for (const auto& p : r0) pts.push_back({ p[0], p[1], z0 });
        for (auto it = r0.rbegin(); it != r0.rend(); ++it) pts.push_back({ -(*it)[0], (*it)[1], z0 });
        P_hull.color(1, 1, 1);
        P_hull.poly(pts, { 0, 0, -1 });
    }

    // decks: flush foredeck, a sunk cockpit with its floor, side walls and gunwale caps
    auto deckY = [&](double t) { return BoatSheer(t, freeboard, sheerRise) - 0.015; };
    auto cockY = [&](double t) { return BoatSheer(t, freeboard, sheerRise) - cockDepth; };
    auto inCock = [&](double t) { return t > cockpit0 && t < cockpit1 && cockDepth > 0; };
    auto zAt = [&](double t) { return z0 + t * L; };

    {
        double deckCol[3];
        if (def.boat == "cruiser" || police) { deckCol[0] = 0.85; deckCol[1] = 0.84; deckCol[2] = 0.8; }
        else if (def.boat == "rib") { deckCol[0] = 0.2; deckCol[1] = 0.2; deckCol[2] = 0.22; }
        else { deckCol[0] = 0.72; deckCol[1] = 0.52; deckCol[2] = 0.32; }
        P_deck.color(deckCol[0], deckCol[1], deckCol[2]);

        // foredeck and aft deck (outside the cockpit)
        for (const auto& range : std::vector<std::array<double, 2>>{ {cockpit1, 0.995}, {0, cockpit0} }) {
            const double ta = range[0], tb = range[1];
            if (tb - ta < 0.01) continue;
            std::vector<double> ts = Stations(ta, tb, 0.03, {});
            std::vector<std::vector<Pt3>> rows;
            for (double t : ts) {
                const double b = BoatHalf(t, bowStart, B);
                std::vector<Pt3> row;
                for (int i = -6; i <= 6; i++) {
                    const double x = b * i / 6.0 * 0.99;
                    const double y = deckY(t) + 0.035 * (1 - std::pow(x / Max(b, 0.01), 2));
                    row.push_back({ x, y, zAt(t) });
                }
                rows.push_back(row);
            }
            GridOpts o; o.ref = { 0, -2, 0 };
            EmitGrid(rows, [&](int, int) { return &P_deck; }, o);
        }

        if (cockpit1 > cockpit0 && cockDepth > 0) {
            std::vector<double> ts = Stations(cockpit0, cockpit1, 0.03, {});
            auto w = [&](double t) { return BoatHalf(t, bowStart, B) - 0.11; };

            // cockpit floor
            std::vector<std::vector<Pt3>> floorRows;
            for (double t : ts) {
                floorRows.push_back({ { -w(t), cockY(t), zAt(t) }, { w(t), cockY(t), zAt(t) } });
            }
            GridOpts of; of.ref = { 0, -2, 0 };
            EmitGrid(floorRows, [&](int, int) { return &P_deck; }, of);

            P_trim.color(0.92, 0.92, 0.9);
            for (int sd : { 1, -1 }) {
                // side walls
                std::vector<std::vector<Pt3>> wallRows;
                for (double t : ts) {
                    wallRows.push_back({ { sd * w(t), cockY(t), zAt(t) }, { sd * w(t), deckY(t), zAt(t) } });
                }
                GridOpts ow; ow.ref = { 0, 0.3, zAt((cockpit0 + cockpit1) / 2) }; ow.inward = true;
                EmitGrid(wallRows, [&](int, int) { return &P_trim; }, ow);

                // gunwale caps
                std::vector<std::vector<Pt3>> capRows;
                for (double t : ts) {
                    capRows.push_back({ { sd * w(t), deckY(t), zAt(t) }, { sd * (BoatHalf(t, bowStart, B) + 0.005), deckY(t) + 0.01, zAt(t) } });
                }
                GridOpts oc; oc.ref = { 0, -2, 0 };
                EmitGrid(capRows, [&](int, int) { return &P_deck; }, oc);
            }

            // cockpit ends
            for (double t : { cockpit0, cockpit1 }) {
                const double b = w(t), zz = zAt(t);
                P_trim.poly({ { -b, cockY(t), zz }, { b, cockY(t), zz }, { b, deckY(t), zz }, { -b, deckY(t), zz } }, { 0, 0, t == cockpit0 ? 1.0 : -1.0 });
            }
        }
    }

    // rubbing strake along the sheer
    {
        std::vector<Pt3> path;
        for (double t : Stations(0.01, 0.985, 0.04, {})) {
            path.push_back({ BoatHalf(t, bowStart, B) + 0.01, BoatSheer(t, freeboard, sheerRise) - 0.06, zAt(t) });
        }
        std::vector<Pt2> section = { { -0.02, -0.03 }, { 0.03, -0.03 }, { 0.03, 0.03 }, { -0.02, 0.03 } };
        for (int sd : { 1, -1 }) {
            P_trim.color(police ? 0.1 : 0.15, police ? 0.25 : 0.15, police ? 0.7 : 0.17);
            std::vector<Pt3> spath;
            for (const auto& p : path) spath.push_back({ p[0] * sd, p[1], p[2] });
            Sweep(P_trim, spath, section, true, false);
        }
    }

    // inflatable tubes on the RIB
    if (tubes) {
        std::vector<Pt3> path;
        for (double t : Stations(0.0, 0.97, 0.03, {})) {
            path.push_back({ BoatHalf(t, bowStart, B) + 0.05, BoatSheer(t, freeboard, sheerRise) - 0.05, zAt(t) });
        }
        std::vector<Pt2> tube;
        for (int k = 0; k < 12; k++) {
            const double a = k / 12.0 * kTau;
            tube.push_back({ std::cos(a) * 0.22, std::sin(a) * 0.22 });
        }
        P_trim.color(0.12, 0.12, 0.13);
        std::vector<Pt3> full;
        for (auto it = path.rbegin(); it != path.rend(); ++it) full.push_back({ -(*it)[0], (*it)[1], (*it)[2] });
        for (size_t i = 1; i < path.size(); i++) full.push_back(path[i]);
        Sweep(P_trim, full, tube, true, false);
    }

    // Store hull data for physics
    model->hullData.draft = draft;
    model->hullData.free = freeboard;
    model->hullData.L = L;
    model->hullData.z0 = z0;

    // prop position
    model->propPos = { 0, -draft - 0.3, z0 - 0.2 };

    // seats, seatHip and doorPos are set in the per-type fit-out
    model->seatHip = 0.4;
    model->doorPos = { 0, 0, 0 };

    // ---------------- per-type fit-out helpers

    auto console = [&](double t, double w, double h) {
        const double y = cockY(t), z = zAt(t);
        P_trim.color(0.92, 0.92, 0.9);
        RoundBox(P_trim, -w, w, y, y + h, z - 0.35, z + 0.25, 0.08, 2);
        P_trim.color(0.1, 0.1, 0.1); P_trim.box(-w * 0.7, y + h - 0.02, z - 0.2, w * 0.7, y + h + 0.01, z + 0.15);
        P_chrome.color(0.8, 0.8, 0.82); P_chrome.geo(Geo::Torus(0.15, 0.02, 6, 16), BoatM4(0.25, y + h + 0.08, z - 0.25, -0.9, 0, 0));
    };

    auto windscreen = [&](double t, double w, double h, double rake = 0.5) {
        const double y = deckY(t), z = zAt(t);
        std::vector<Pt3> pts;
        for (int k = 0; k <= 8; k++) { const double a = -1 + 2.0 * k / 8; pts.push_back({ a * w, y, z + 0.18 * (1 - a * a) }); }
        std::vector<Pt3> pts2;
        for (const auto& p : pts) pts2.push_back({ p[0] * 0.94, p[1] + h, p[2] - h * rake });
        std::vector<std::vector<Pt3>> rows = { pts, pts2 };
        GridOpts og; og.ref = { 0, y, z - 2 };
        EmitGrid(rows, [&](int, int) { return &P_glass; }, og);
        P_chrome.color(0.75, 0.75, 0.78);
        std::vector<Pt2> chromeSec = { { -0.015, -0.015 }, { 0.015, -0.015 }, { 0.015, 0.015 }, { -0.015, 0.015 } };
        Sweep(P_chrome, pts2, chromeSec, true, false);
    };

    auto seat = [&](double x, double t, double w = 0.26) {
        const double y = cockY(t), z = zAt(t);
        P_trim.color(0.88, 0.86, 0.8);
        RoundBox(P_trim, x - w, x + w, y, y + 0.42, z - 0.25, z + 0.25, 0.06, 2);
        RoundBox(P_trim, x - w, x + w, y + 0.42, y + 0.95, z - 0.3, z - 0.18, 0.05, 2);
        model->seats.push_back({ x, y + 0.42, z });
    };

    auto outboard = [&](double x, int n = 1) {
        for (int k = 0; k < n; k++) {
            const double xx = x + (k - (n - 1) / 2.0) * 0.55;
            const double y = BoatSheer(0, freeboard, sheerRise) - 0.15;
            const double z = z0 - 0.25;
            P_trim.color(0.1, 0.1, 0.11); RoundBox(P_trim, xx - 0.2, xx + 0.2, y, y + 0.55, z - 0.35, z + 0.2, 0.08, 2);
            P_trim.color(0.2, 0.2, 0.22); P_trim.box(xx - 0.06, -draft - 0.35, z - 0.12, xx + 0.06, y, z + 0.08);
            P_chrome.color(0.6, 0.6, 0.6); P_chrome.box(xx - 0.02, -draft - 0.4, z - 0.02, xx + 0.02, -draft - 0.25, z + 0.15);
        }
    };

    auto rail = [&](double t0, double t1, double hgt = 0.6) {
        for (int sd : { 1, -1 }) {
            std::vector<Pt3> path;
            for (double t : Stations(t0, t1, 0.05, {})) {
                path.push_back({ sd * (BoatHalf(t, bowStart, B) - 0.08), deckY(t) + hgt, zAt(t) });
            }
            P_chrome.color(0.82, 0.82, 0.85);
            std::vector<Pt2> railSec = { { -0.015, -0.015 }, { 0.015, -0.015 }, { 0.015, 0.015 }, { -0.015, 0.015 } };
            Sweep(P_chrome, path, railSec, true, false);
            for (size_t k = 0; k < path.size(); k += 3) {
                const double x = path[k][0], y = path[k][1], z = path[k][2];
                P_chrome.box(x - 0.012, y - hgt, z - 0.012, x + 0.012, y, z + 0.012);
            }
        }
    };

    auto cabin = [&](double ta, double tb, double h, bool glassCol = true) -> double {
        std::vector<double> ts = Stations(ta, tb, 0.04, {});
        auto w = [&](double t) { return BoatHalf(t, bowStart, B) - 0.2; };
        auto yb = [&](double t) { return deckY(t); };
        std::vector<std::vector<Pt3>> rows;
        for (double t : ts) {
            const double W = w(t), y = yb(t), z = zAt(t);
            std::vector<Pt3> row = { { W, y, z }, { W * 0.98, y + h * 0.45, z }, { W * 0.92, y + h * 0.9, z }, { W * 0.8, y + h, z }, { 0, y + h + 0.05, z } };
            rows.push_back(row);
        }
        for (int sd : { 1, -1 }) {
            std::vector<std::vector<Pt3>> rr;
            for (const auto& r : rows) {
                std::vector<Pt3> row;
                for (const auto& p : r) row.push_back({ p[0] * sd, p[1], p[2] });
                rr.push_back(row);
            }
            GridOpts og; og.ref = { 0, deckY((ta + tb) / 2) + h * 0.4, zAt((ta + tb) / 2) };
            og.colorAt = [](double, double, double, Part*) { return Pt3{ 1, 1, 1 }; };
            EmitGrid(rr, [&](int i, int j) -> Part* { return j == 1 ? &P_glass : &P_hull; }, og);
        }
        for (double t : { ta, tb }) {
            const auto& r = rows[t == ta ? 0 : rows.size() - 1];
            std::vector<Pt3> ring2;
            for (const auto& p : r) ring2.push_back(p);
            for (auto it = r.rbegin() + 1; it != r.rend(); ++it) ring2.push_back({ -(*it)[0], (*it)[1], (*it)[2] });
            P_hull.color(1, 1, 1);
            P_hull.poly(ring2, { 0, 0, t == ta ? -1.0 : 1.0 });
            const double W = w(t) * 0.8, y = yb(t) + h * 0.5, z = zAt(t) + (t == ta ? -0.01 : 0.01);
            if (glassCol) P_glass.poly({ { -W, y, z }, { W, y, z }, { W * 0.9, y + h * 0.35, z }, { -W * 0.9, y + h * 0.35, z } }, { 0, 0, t == ta ? -1.0 : 1.0 });
        }
        return yb((ta + tb) / 2) + h + 0.05;
    };

    // ---------------- per-type fit-out
    if (def.boat == "rib") {
        console(0.52, 0.28, 0.85);
        windscreen(0.6, 0.35, 0.3, 0.3);
        model->seats.push_back({ 0, cockY(0.4) + 0.62, zAt(0.4) });
        P_trim.color(0.15, 0.15, 0.16); RoundBox(P_trim, -0.3, 0.3, cockY(0.4), cockY(0.4) + 0.6, zAt(0.4) - 0.3, zAt(0.4) + 0.3, 0.1, 2);
        model->seats.push_back({ 0, cockY(0.2) + 0.45, zAt(0.2) });
        P_trim.box(-0.45, cockY(0.2), zAt(0.2) - 0.2, 0.45, cockY(0.2) + 0.43, zAt(0.2) + 0.2);
        outboard(0, 1);
        model->seatHip = 0.08;
    } else if (def.boat == "jetski") {
        const double y = deckY(0.4);
        P_hull.color(1, 1, 1); RoundBox(P_hull, -0.3, 0.3, y - 0.05, y + 0.25, zAt(0.15), zAt(0.75), 0.12, 3);
        P_trim.color(0.1, 0.1, 0.1); RoundBox(P_trim, -0.2, 0.2, y + 0.24, y + 0.36, zAt(0.18), zAt(0.6), 0.08, 3);
        P_hull.color(1, 1, 1); RoundBox(P_hull, -0.25, 0.25, y + 0.1, y + 0.55, zAt(0.7), zAt(0.82), 0.1, 2);
        P_chrome.color(0.7, 0.7, 0.72); P_chrome.geo(Geo::Cylinder(0.018, 0.018, 0.7, 8), BoatM4(0, y + 0.62, zAt(0.74), 0, 0, kPi / 2));
        model->seats.push_back({ 0, y + 0.36, zAt(0.42) });
        model->seatHip = 0.04;
    } else if (def.boat == "cruiser") {
        const double cb_top = cabin(0.3, 0.7, 1.45);
        // flybridge with a second helm, a radar arch and rails
        P_hull.color(1, 1, 1); RoundBox(P_hull, -1.1, 1.1, cb_top, cb_top + 0.5, zAt(0.36), zAt(0.6), 0.1, 2);
        P_glass.poly({ { -1.05, cb_top + 0.5, zAt(0.6) }, { 1.05, cb_top + 0.5, zAt(0.6) }, { 0.95, cb_top + 0.85, zAt(0.57) }, { -0.95, cb_top + 0.85, zAt(0.57) } }, { 0, 0.3, 1 });
        P_chrome.color(0.85, 0.85, 0.88);
        for (int sd : { 1, -1 }) P_chrome.geo(Geo::Cylinder(0.04, 0.04, 1.3, 8), BoatM4(sd * 1.0, cb_top + 1.1, zAt(0.4), 0.15, 0, sd * 0.2));
        P_chrome.box(-1.05, cb_top + 1.72, zAt(0.4) - 0.1, 1.05, cb_top + 1.8, zAt(0.4) + 0.12);
        rail(0.72, 0.97, 0.55);
        seat(0.45, 0.2); seat(-0.45, 0.2);
        model->seats.insert(model->seats.begin(), { 0.35, cb_top + 0.5, zAt(0.48) });
        P_trim.color(0.88, 0.86, 0.8); RoundBox(P_trim, 0.1, 0.6, cb_top, cb_top + 0.45, zAt(0.46), zAt(0.52), 0.05, 2);
        model->seatHip = 0.06;
    } else if (def.boat == "police") {
        const double cb_top = cabin(0.4, 0.7, 1.25);
        // light bar and searchlight on the cabin roof; a gun mount on the foredeck
        auto mkLight = [&](double x0, double x1) -> MeshBuf {
            // CylinderGeometry(0.1, 0.1, x1 - x0, 12), rotateZ(PI / 2), then scale(1, 0.75, 1.3)
            MeshBuf g;
            g.Append(Geo::Cylinder(0.1, 0.1, x1 - x0, 12), Mat4::Compose(0, 0, 0, 0, 0, 0, 1, 0.75, 1.3) * Mat4::Compose(0, 0, 0, 0, 0, kPi / 2));
            return g;
        };
        P_trim.color(0.1, 0.1, 0.1); P_trim.box(-0.7, cb_top, zAt(0.55) - 0.15, 0.7, cb_top + 0.05, zAt(0.55) + 0.15);
        model->hasLightbar = true;
        model->lightRed = mkLight(0.03, 0.65);
        model->lightBlue = mkLight(-0.65, -0.03);
        model->lightRedPos = { (0.03 + 0.65) / 2, cb_top + 0.12, zAt(0.55) };
        model->lightBluePos = { (-0.65 - 0.03) / 2, cb_top + 0.12, zAt(0.55) };

        P_chrome.color(0.35, 0.35, 0.37);
        P_chrome.geo(Geo::Cylinder(0.12, 0.16, 0.5, 10), BoatM4(0, deckY(0.82) + 0.25, zAt(0.82), 0, 0, 0));

        // gun
        model->hasGun = true;
        Part P_gun;
        P_gun.color(0.18, 0.18, 0.2); P_gun.box(-0.14, -0.1, -0.35, 0.14, 0.12, 0.2);
        P_gun.geo(Geo::Cylinder(0.035, 0.035, 1.1, 8), BoatM4(0, 0, 0.7, kPi / 2, 0, 0));
        model->gun = std::move(P_gun.m);
        model->gunPos = { 0, deckY(0.82) + 0.55, zAt(0.82) };

        rail(0.72, 0.96, 0.5);
        seat(0.45, 0.28); seat(-0.45, 0.28);
        model->seats.insert(model->seats.begin(), { 0.4, deckY(0.55) + 0.45, zAt(0.52) });
        model->seats.push_back({ -0.4, deckY(0.55) + 0.45, zAt(0.52) });
        P_trim.color(0.2, 0.2, 0.22); RoundBox(P_trim, 0.15, 0.65, deckY(0.55), deckY(0.55) + 0.42, zAt(0.5), zAt(0.56), 0.05, 2);
        outboard(0, 2);
        model->seatHip = 0.06;
    } else { // speedboat
        windscreen(0.64, B / 2 - 0.15, 0.42, 0.9);
        console(0.58, 0.32, 0.72);
        seat(0.42, 0.5); seat(-0.42, 0.5);
        P_trim.color(0.88, 0.86, 0.8); RoundBox(P_trim, -B / 2 + 0.2, B / 2 - 0.2, cockY(0.12), cockY(0.12) + 0.42, zAt(0.06), zAt(0.22), 0.06, 2);
        model->seats.push_back({ 0.45, cockY(0.14) + 0.42, zAt(0.14) });
        model->seats.push_back({ -0.45, cockY(0.14) + 0.42, zAt(0.14) });
        // engine hatch and twin exhausts in the transom
        P_deck.color(0.95, 0.95, 0.95); RoundBox(P_deck, -0.7, 0.7, deckY(0.02), deckY(0.02) + 0.12, z0 + 0.1, z0 + 0.6, 0.05, 2);
        P_chrome.color(0.7, 0.7, 0.72);
        for (int sd : { 1, -1 }) P_chrome.geo(Geo::Cylinder(0.06, 0.06, 0.1, 10), BoatM4(sd * 0.5, 0.25, z0 - 0.03, kPi / 2, 0, 0));
        rail(0.78, 0.97, 0.25);
        model->seatHip = 0.06;
    }

    // navigation lights: red port, green starboard at the bow, white stern light
    P_head.color(1, 1, 1);
    const double bowT = 0.9;
    P_head.box(-0.04, deckY(bowT) + 0.05, zAt(bowT) - 0.04, 0.04, deckY(bowT) + 0.13, zAt(bowT) + 0.04);
    P_tail.color(1, 1, 1);
    P_tail.box(-0.05, deckY(0.02) + 0.05, z0 + 0.02, 0.05, deckY(0.02) + 0.15, z0 + 0.1);

    if (model->seats.empty()) model->seats.push_back({ 0.4, cockY(0.5) + 0.4, zAt(0.5) });

    // (no glass: a hidden 1 cm box, as the browser game has)
    if (P_glass.empty()) model->glass = Geo::Box(0.01, 0.01, 0.01);
    else model->glass = std::move(P_glass.m);

    // door position (right side of first seat)
    if (!model->seats.empty()) {
        model->doorPos = { B / 2 + 0.7, 0, model->seats[0][2] };
    }

    // Move the parts into the model
    model->hull = std::move(P_hull.m);
    model->deck = std::move(P_deck.m);
    model->trim = std::move(P_trim.m);
    model->chrome = std::move(P_chrome.m);
    model->head = std::move(P_head.m);
    model->tail = std::move(P_tail.m);

    cache[def.id] = std::move(model);
    return *cache[def.id];
}

} // namespace atg