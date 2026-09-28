// Offline preview of the generated meshes (no Unreal needed): a small software rasteriser with back-face
// culling, so a triangle wound the wrong way shows up as a hole. Writes PNGs.
//
//   g++ -std=c++20 -O2 -I../Source/AutoTheftGrand/Private/Gen meshpreview.cpp ../Source/AutoTheftGrand/Private/Gen/*.cpp -o meshpreview
//   ./meshpreview out_dir
#include "WorldMeshes.h"
#include "Models.h"
#include "MapImage.h"
#include <cstdio>
#include <cstring>

using namespace atg;

namespace {
struct Img {
	int w, h;
	std::vector<uint8_t> rgb;
	std::vector<float> z;
	Img(int W, int H) : w(W), h(H), rgb((size_t)W * H * 3, 0), z((size_t)W * H, 1e30f) {
		for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { const double t = (double)y / H; uint8_t* p = &rgb[((size_t)y * W + x) * 3]; p[0] = (uint8_t)(120 + 60 * t); p[1] = (uint8_t)(160 + 50 * t); p[2] = (uint8_t)(210 + 30 * t); }
	}
};
uint32_t Crc(const uint8_t* d, size_t n, uint32_t c = 0xffffffffu) {
	static uint32_t T[256]; static bool init = false;
	if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1; T[i] = k; } init = true; }
	for (size_t i = 0; i < n; i++) c = T[(c ^ d[i]) & 255] ^ (c >> 8);
	return c;
}
void WritePng(const Img& im, const std::string& path) {
	std::vector<uint8_t> raw;
	for (int y = 0; y < im.h; y++) { raw.push_back(0); raw.insert(raw.end(), im.rgb.begin() + (size_t)y * im.w * 3, im.rgb.begin() + (size_t)(y + 1) * im.w * 3); }
	std::vector<uint8_t> z = { 0x78, 0x01 };
	uint32_t a = 1, b = 0;
	for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
	for (size_t i = 0; i < raw.size(); i += 65535) {
		const size_t n = std::min<size_t>(65535, raw.size() - i);
		z.push_back(i + n >= raw.size() ? 1 : 0);
		z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
		z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
	}
	const uint32_t ad = (b << 16) | a;
	for (int k = 3; k >= 0; k--) z.push_back((ad >> (k * 8)) & 255);
	FILE* f = fopen(path.c_str(), "wb");
	const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	fwrite(sig, 1, 8, f);
	auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
		const uint32_t n = (uint32_t)data.size();
		const uint8_t len[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n };
		fwrite(len, 1, 4, f);
		std::vector<uint8_t> td(type, type + 4); td.insert(td.end(), data.begin(), data.end());
		fwrite(td.data(), 1, td.size(), f);
		const uint32_t c = Crc(td.data(), td.size()) ^ 0xffffffffu;
		const uint8_t cb[4] = { (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
		fwrite(cb, 1, 4, f);
	};
	std::vector<uint8_t> ihdr = { (uint8_t)(im.w >> 24), (uint8_t)(im.w >> 16), (uint8_t)(im.w >> 8), (uint8_t)im.w, (uint8_t)(im.h >> 24), (uint8_t)(im.h >> 16), (uint8_t)(im.h >> 8), (uint8_t)im.h, 8, 2, 0, 0, 0 };
	chunk("IHDR", ihdr); chunk("IDAT", z); chunk("IEND", {});
	fclose(f);
}

struct Cam {
	double eye[3], f[3], r[3], u[3], fov;
	Cam(const double e[3], const double t[3], double fovDeg) {
		for (int k = 0; k < 3; k++) { eye[k] = e[k]; f[k] = t[k] - e[k]; }
		double l = Hypot3(f[0], f[1], f[2]); for (double& v : f) v /= l;
		// right = f x up (right-handed, y up)
		r[0] = f[1] * 0 - f[2] * 1; r[1] = f[2] * 0 - f[0] * 0; r[2] = f[0] * 1 - f[1] * 0;
		l = Hypot3(r[0], r[1], r[2]); for (double& v : r) v /= l;
		u[0] = r[1] * f[2] - r[2] * f[1]; u[1] = r[2] * f[0] - r[0] * f[2]; u[2] = r[0] * f[1] - r[1] * f[0];
		fov = fovDeg * kPi / 180;
	}
};

// colour of a vertex: ch1/ch2 (vertex-lit layout), or a fixed colour
enum class Layout { VL, Fixed, Terrain, Building };

void Draw(Img& im, const Cam& cam, const MeshBuf& g, Layout lay, const double fixed[3] = nullptr) {
	const double sun[3] = { 0.45, 0.8, 0.35 };
	const double sl = Hypot3(sun[0], sun[1], sun[2]);
	const double tanF = std::tan(cam.fov / 2);
	auto proj = [&](size_t v, double o[3]) {
		const double d[3] = { g.P[v * 3] - cam.eye[0], g.P[v * 3 + 1] - cam.eye[1], g.P[v * 3 + 2] - cam.eye[2] };
		const double x = d[0] * cam.r[0] + d[1] * cam.r[1] + d[2] * cam.r[2];
		const double y = d[0] * cam.u[0] + d[1] * cam.u[1] + d[2] * cam.u[2];
		const double z = d[0] * cam.f[0] + d[1] * cam.f[1] + d[2] * cam.f[2];
		o[2] = z;
		if (z < 0.05) return false;
		o[0] = (x / (z * tanF) * im.h / 2) + im.w / 2.0;
		o[1] = (-y / (z * tanF) * im.h / 2) + im.h / 2.0;
		return true;
	};
	auto shade = [&](size_t v) {
		double c[3];
		if (lay == Layout::VL) { c[0] = g.C[1][v * 2]; c[1] = g.C[1][v * 2 + 1]; c[2] = g.C[2][v * 2]; }
		else if (lay == Layout::Terrain) { c[0] = g.C[0][v * 2]; c[1] = g.C[0][v * 2 + 1]; c[2] = g.C[1][v * 2]; const double sand = g.C[2][v * 2], forest = g.C[2][v * 2 + 1]; for (int k = 0; k < 3; k++) { const double ds[3] = { 0.55, 0.39, 0.24 }, ff[3] = { 0.2, 0.2, 0.1 }; c[k] = c[k] * (1 - sand) + ds[k] * sand; c[k] = c[k] * (1 - forest) + ff[k] * forest; } }
		else if (lay == Layout::Building) { c[0] = g.C[2][v * 2 + 1] * 0.8; c[1] = g.C[3][v * 2] * 0.8; c[2] = g.C[3][v * 2 + 1] * 0.8; if (g.C[2][v * 2] >= 4) c[0] = c[1] = c[2] = 0.35; }
		else for (int k = 0; k < 3; k++) c[k] = fixed[k];
		const double nd = Max(0, (g.N[v * 3] * sun[0] + g.N[v * 3 + 1] * sun[1] + g.N[v * 3 + 2] * sun[2]) / sl);
		const double k = 0.35 + 0.75 * nd;
		for (int q = 0; q < 3; q++) c[q] = std::pow(Clamp(c[q] * k, 0, 1), 1 / 2.2);
		return std::array<double, 3>{ c[0], c[1], c[2] };
	};
	for (size_t t = 0; t + 2 < g.I.size(); t += 3) {
		const uint32_t id[3] = { g.I[t], g.I[t + 1], g.I[t + 2] };
		double s[3][3];
		if (!proj(id[0], s[0]) || !proj(id[1], s[1]) || !proj(id[2], s[2])) continue;
		// back-face cull: counter-clockwise in world = clockwise on screen (y down)
		const double area = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[1][1] - s[0][1]) * (s[2][0] - s[0][0]);
		if (area >= 0) continue;
		const auto c0 = shade(id[0]), c1 = shade(id[1]), c2 = shade(id[2]);
		const int x0 = (int)Max(0, std::floor(Min(s[0][0], Min(s[1][0], s[2][0])))), x1 = (int)Min(im.w - 1, std::ceil(Max(s[0][0], Max(s[1][0], s[2][0]))));
		const int y0 = (int)Max(0, std::floor(Min(s[0][1], Min(s[1][1], s[2][1])))), y1 = (int)Min(im.h - 1, std::ceil(Max(s[0][1], Max(s[1][1], s[2][1]))));
		for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
			const double px = x + 0.5, py = y + 0.5;
			const double w0 = ((s[1][0] - px) * (s[2][1] - py) - (s[1][1] - py) * (s[2][0] - px)) / area;
			const double w1 = ((s[2][0] - px) * (s[0][1] - py) - (s[2][1] - py) * (s[0][0] - px)) / area;
			const double w2 = 1 - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			const float zz = (float)(w0 * s[0][2] + w1 * s[1][2] + w2 * s[2][2]);
			float& zb = im.z[(size_t)y * im.w + x];
			if (zz >= zb) continue;
			zb = zz;
			uint8_t* p = &im.rgb[((size_t)y * im.w + x) * 3];
			for (int q = 0; q < 3; q++) p[q] = (uint8_t)Clamp((w0 * c0[q] + w1 * c1[q] + w2 * c2[q]) * 255, 0, 255);
		}
	}
}
} // namespace

int main(int argc, char** argv) {
	const std::string out = argc > 1 ? argv[1] : ".";
	// ---- a driver in the seat (the pose ATGCharacter uses), the car cut away above the waist
	if (argc > 2 && std::string(argv[2]) == "seat") {
		Img im(1200, 800);
		const double e[3] = { 4.2, 2.6, 3.4 }, t[3] = { 0, 0.7, 0 };
		Cam cam(e, t, 50);
		const CarDef& d = *FindCar("meridian");
		const CarModel cm = BuildCarModel(d);
		auto clip = [](const MeshBuf& g, double maxY) {
			MeshBuf o; o.Append(g);
			std::vector<uint32_t> keep;
			for (size_t k = 0; k + 2 < o.I.size(); k += 3) {
				bool ok = true;
				for (int q = 0; q < 3; q++) if (o.P[o.I[k + q] * 3 + 1] > maxY) ok = false;
				if (ok) { keep.push_back(o.I[k]); keep.push_back(o.I[k + 1]); keep.push_back(o.I[k + 2]); }
			}
			o.I = keep;
			return o;
		};
		Draw(im, cam, clip(cm.paint, 0.95), Layout::VL);
		Draw(im, cam, clip(cm.trim, 0.95), Layout::VL);
		for (int sx : { -1, 1 }) for (int sz : { -1, 1 }) { MeshBuf w; w.Append(cm.wheel, Mat4::Compose(sx * d.track / 2, d.wheelR, sz * d.wheelbase / 2)); Draw(im, cam, w, Layout::VL); }
		// ATGCharacter::Animate (Unreal pitch forward = rotation about -x here)
		const std::map<std::string, double> pose = { { "torso", -0.08 }, { "head", 0.05 }, { "thighL", 1.45 }, { "thighR", 1.45 }, { "shinL", -1.35 }, { "shinR", -1.35 },
			{ "armL", 0.95 }, { "armR", 0.95 }, { "foreL", 0.55 }, { "foreR", 0.55 } };
		const auto parts = BuildHuman({ 0x8a5536, 0xf2f2f2, 0x2b3a55, 0xeeeeee, 0x111111 });
		std::map<std::string, Mat4> joints;
		for (const HumanPart& hp : parts) {
			const Mat4 parent = hp.parent[0] ? joints[hp.parent] : Mat4::Compose(cm.seat[0], cm.seat[1] + 0.1 - 0.98, cm.seat[2]);
			auto it = pose.find(hp.name);
			joints[hp.name] = parent * Mat4::Compose(hp.joint[0], hp.joint[1], hp.joint[2], it == pose.end() ? 0 : -it->second, 0, 0);
			MeshBuf g; g.Append(hp.mesh, joints[hp.name]); Draw(im, cam, g, Layout::VL);
		}
		WritePng(im, out + "/seat.png");
		printf("seat %.2f %.2f %.2f door %.2f %.2f\n", cm.seat[0], cm.seat[1], cm.seat[2], cm.door[0], cm.door[1]);
		return 0;
	}
	// ---- the map (world layer with the city layer composited over it, and the city layer alone)
	if (argc > 2 && std::string(argv[2]) == "map") {
		CityMap map;
		const MapImages mi = BuildMapImages(map);
		auto composite = [&](const MapLayer& base, const MapLayer* over, const std::string& name) {
			Img im(base.w, base.h);
			for (int y = 0; y < base.h; y++) for (int x = 0; x < base.w; x++) {
				const uint8_t* s = &base.rgba[((size_t)y * base.w + x) * 4];
				double c[3] = { (double)s[0], (double)s[1], (double)s[2] };
				if (over) {
					const double wx = base.minX + (x + 0.5) / base.w * (base.maxX - base.minX), wz = base.minZ + (y + 0.5) / base.h * (base.maxZ - base.minZ);
					const int ox = (int)((wx - over->minX) / (over->maxX - over->minX) * over->w), oy = (int)((wz - over->minZ) / (over->maxZ - over->minZ) * over->h);
					if (ox >= 0 && oy >= 0 && ox < over->w && oy < over->h) {
						const uint8_t* o = &over->rgba[((size_t)oy * over->w + ox) * 4];
						const double a = o[3] / 255.0;
						for (int k = 0; k < 3; k++) c[k] = c[k] * (1 - a) + o[k] * a;
					}
				}
				uint8_t* d = &im.rgb[((size_t)y * base.w + x) * 3];
				for (int k = 0; k < 3; k++) d[k] = (uint8_t)c[k];
			}
			WritePng(im, out + "/" + name);
		};
		composite(mi.world, &mi.city, "map_world.png");
		composite(mi.city, nullptr, "map_city.png");
		printf("map %dx%d city %dx%d labels %zu\n", mi.world.w, mi.world.h, mi.city.w, mi.city.h, mi.labels.size());
		return 0;
	}
	const auto props = BuildPropTemplates();
	const auto veg = BuildVegTemplates();
	// ---- prop sheet
	{
		Img im(1600, 700);
		const char* names[] = { "streetlight", "trafficlight", "hydrant", "trashcan", "dumpster", "bench", "busstop", "phonebooth", "hoop", "powerpole", "crossbuck", "pump", "palm0", "tree0", "boat", "sandbags", "boothbar", "haybale" };
		const double e[3] = { 55, 14, 34 }, t[3] = { 55, 4, 0 };
		Cam cam(e, t, 60);
		int i = 0;
		for (const char* n : names) {
			const PropTemplate& p = props.at(n);
			const double x = (i % 9) * 12 + 7, z = (i / 9) * -14;
			MeshBuf m; m.Append(p.mesh); m.Translate(x, 0, z);
			Draw(im, cam, m, Layout::VL);
			if (!p.leaves.Empty()) { MeshBuf l; l.Append(p.leaves); l.Translate(x, 0, z); Draw(im, cam, l, Layout::VL); }
			i++;
		}
		const char* vn[] = { "pine", "oak", "cactus", "rock", "deadtree", "bush" };
		int k = 0;
		for (const char* n : vn) {
			const VegTemplate& v = veg.at(n);
			MeshBuf m; m.Append(v.nearMesh); if (!v.nearLeaves.Empty()) m.Append(v.nearLeaves); m.Translate(k * 13 + 3, 0, -30);
			Draw(im, cam, m, Layout::VL);
			k++;
		}
		WritePng(im, out + "/props.png");
	}
	// ---- cars and people
	{
		Img im(1600, 800);
		const double e[3] = { 14, 7, 16 }, t[3] = { 14, 0.6, -4 };
		Cam cam(e, t, 55);
		int i = 0;
		for (const CarDef& d : CarDefs()) {
			const CarModel cm = BuildCarModel(d);
			const double x = (i % 7) * 5.2 - 1, z = (i / 7) * -8;
			const Mat4 at = Mat4::Compose(x, 0, z, 0, 0.6, 0);
			MeshBuf pm; pm.Append(cm.paint, at);
			const double rgb[3] = { ((d.colors[0] >> 16) & 255) / 255.0, ((d.colors[0] >> 8) & 255) / 255.0, (d.colors[0] & 255) / 255.0 };
			for (size_t v = 0; v < pm.Count(); v++) { pm.C[1][v * 2] = (float)std::pow(rgb[0], 2.2); pm.C[1][v * 2 + 1] = (float)std::pow(rgb[1], 2.2); pm.C[2][v * 2] = (float)std::pow(rgb[2], 2.2); }
			Draw(im, cam, pm, Layout::VL);
			MeshBuf tm; tm.Append(cm.trim, at); Draw(im, cam, tm, Layout::VL);
			for (int sx : { -1, 1 }) for (int sz : { -1, 1 }) {
				MeshBuf w; w.Append(cm.wheel, at * Mat4::Compose(sx * d.track / 2, d.wheelR, sz * d.wheelbase / 2));
				Draw(im, cam, w, Layout::VL);
			}
			i++;
		}
		const auto parts = BuildHuman({ 0xc68642, 0xf2f2f2, 0x2f4a78, 0x222222, 0x1a1a1a });
		for (int copy = 0; copy < 2; copy++) {
			std::map<std::string, Mat4> joints;
			for (const HumanPart& hp : parts) {
				Mat4 parent = hp.parent[0] ? joints[hp.parent] : Mat4::Compose(33 + copy * 1.5, 0, -2, 0, copy ? kPi : 0, 0);
				const double swing = std::string(hp.name).find("thighL") == 0 ? 0.5 : std::string(hp.name).find("thighR") == 0 ? -0.5 : 0;
				joints[hp.name] = parent * Mat4::Compose(hp.joint[0], hp.joint[1], hp.joint[2], swing, 0, 0);
				MeshBuf g; g.Append(hp.mesh, joints[hp.name]); Draw(im, cam, g, Layout::VL);
			}
		}
		WritePng(im, out + "/cars.png");
	}
	// ---- the world
	CityMap map;
	fprintf(stderr, "world built\n");
	const RoadMeshes roads = BuildRoadMeshes(map);
	const auto city = BuildBuildings(map);
	const MeshBuf blocks = BuildBlocks(map), lots = BuildLots(map), pads = BuildPads(map), street = BuildStreetPlane();
	size_t rv = 0, cv = 0, bv = 0, dv = 0;
	for (const auto& kv : roads.chunks) { rv += kv.second.road.Count(); cv += kv.second.conc.Count(); }
	for (const auto& kv : city) { bv += kv.second.bld.Count(); dv += kv.second.det.Count(); }
	printf("roads %zu chunks, road verts %zu, concrete verts %zu, sleepers %zu\n", roads.chunks.size(), rv, cv, roads.sleepers.size());
	printf("city chunks %zu, building verts %zu, detail verts %zu, blocks %zu lots %zu pads %zu\n", city.size(), bv, dv, blocks.Count(), lots.Count(), pads.Count());
	const auto pinst = PlaceProps(map);
	const auto cols = BuildPropColliders(map, pinst, props);
	size_t colv = 0; for (const auto& kv : cols) colv += kv.second.Count();
	printf("props placed %zu, collider chunks %zu verts %zu\n", pinst.size(), cols.size(), colv);
	size_t tv = 0; int tc = 0;
	for (int j = 0; j < TerrainChunksZ(); j++) for (int i = 0; i < TerrainChunksX(); i++) if (!TerrainChunkInCity(i, j)) { tv += BuildTerrainChunk(map, i, j, 1).Count(); tc++; }
	printf("terrain chunks %d, 16 m verts %zu\n", tc, tv);
	auto view = [&](const char* name, const double e[3], const double t[3], double fov, double radius) {
		Img im(1400, 800);
		Cam cam(e, t, fov);
		const double cx = t[0], cz = t[2];
		for (int j = 0; j < TerrainChunksZ(); j++) for (int i = 0; i < TerrainChunksX(); i++) {
			const double x0 = WORLD.minX + i * TERRAIN_CH, z0 = WORLD.minZ + j * TERRAIN_CH;
			if (TerrainChunkInCity(i, j)) continue;
			if (std::fabs(x0 + 128 - cx) > radius + 200 || std::fabs(z0 + 128 - cz) > radius + 200) continue;
			Draw(im, cam, BuildTerrainChunk(map, i, j, std::fabs(x0 + 128 - cx) < 600 && std::fabs(z0 + 128 - cz) < 600 ? 0 : 1), Layout::Terrain);
		}
		const double asph[3] = { 0.09, 0.09, 0.1 }, grey[3] = { 0.5, 0.5, 0.48 }, lot[3] = { 0.3, 0.4, 0.2 };
		Draw(im, cam, street, Layout::Fixed, asph);
		Draw(im, cam, blocks, Layout::Fixed, grey);
		Draw(im, cam, lots, Layout::Fixed, lot);
		Draw(im, cam, pads, Layout::Fixed, grey);
		for (const auto& kv : roads.chunks) { Draw(im, cam, kv.second.road, Layout::Fixed, asph); Draw(im, cam, kv.second.conc, Layout::VL); Draw(im, cam, kv.second.rails, Layout::VL); }
		for (const auto& kv : city) { Draw(im, cam, kv.second.bld, Layout::Building); Draw(im, cam, kv.second.det, Layout::VL); }
		for (const PropInstance& p : pinst) {
			if (std::fabs(p.x - cx) > radius || std::fabs(p.z - cz) > radius) continue;
			const PropTemplate& T = props.at(p.type);
			MeshBuf m; m.Append(T.mesh, Mat4::Compose(p.x, p.y, p.z, 0, p.rot, 0, p.scale, p.scale, p.scale));
			Draw(im, cam, m, Layout::VL);
			if (!T.leaves.Empty()) { MeshBuf l; l.Append(T.leaves, Mat4::Compose(p.x, p.y, p.z, 0, p.rot, 0, p.scale, p.scale, p.scale)); Draw(im, cam, l, Layout::VL); }
		}
		WritePng(im, out + "/" + name + ".png");
	};
	{ const double e[3] = { 210, 60, -250 }, t[3] = { 120, 0, -380 }; view("downtown", e, t, 60, 500); }
	{ const double e[3] = { -2400, 90, -120 }, t[3] = { -2520, 0, -240 }; view("ferncreek", e, t, 60, 500); }
	{ const double e[3] = { 660, 160, -3850 }, t[3] = { 420, 0, -4060 }; view("aurelio", e, t, 60, 700); }
	{ const double e[3] = { -600, 40, 60 }, t[3] = { -440, 5, 10 }; view("freeway", e, t, 60, 500); }
	return 0;
}
