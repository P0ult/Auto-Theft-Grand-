#include "Collision.h"
#include "WorldMeshes.h"

namespace atg {

namespace {
constexpr double CELL = 24;
int64_t Key(int64_t ix, int64_t iz) { return ix * 100003 + iz; }
double N3[3] = { 0, 0, 0 };
double RayBox(double ox, double oy, double oz, double dx, double dy, double dz, double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
	double tmin = -kInf, tmax = kInf; int axis = -1;
	if (std::fabs(dx) < 1e-9) { if (ox < minX || ox > maxX) return -1; }
	else { double t1 = (minX - ox) / dx, t2 = (maxX - ox) / dx; if (t1 > t2) std::swap(t1, t2); if (t1 > tmin) { tmin = t1; axis = 0; } if (t2 < tmax) tmax = t2; if (tmin > tmax) return -1; }
	if (std::fabs(dy) < 1e-9) { if (oy < minY || oy > maxY) return -1; }
	else { double t1 = (minY - oy) / dy, t2 = (maxY - oy) / dy; if (t1 > t2) std::swap(t1, t2); if (t1 > tmin) { tmin = t1; axis = 1; } if (t2 < tmax) tmax = t2; if (tmin > tmax) return -1; }
	if (std::fabs(dz) < 1e-9) { if (oz < minZ || oz > maxZ) return -1; }
	else { double t1 = (minZ - oz) / dz, t2 = (maxZ - oz) / dz; if (t1 > t2) std::swap(t1, t2); if (t1 > tmin) { tmin = t1; axis = 2; } if (t2 < tmax) tmax = t2; if (tmin > tmax) return -1; }
	if (tmax < 0) return -1;
	N3[0] = N3[1] = N3[2] = 0;
	if (axis == 0) N3[0] = dx > 0 ? -1 : 1; else if (axis == 1) N3[1] = dy > 0 ? -1 : 1; else if (axis == 2) N3[2] = dz > 0 ? -1 : 1;
	return Max(0, tmin);
}
}

bool SatOBB(double cx, double cz, double fx, double fz, double rx, double rz, double hx, double hz,
	double bx, double bz, double a1x, double a1z, double bhx, double bhz, double a2x, double a2z, Contact& out) {
	const double dx = bx - cx, dz = bz - cz;
	const double axes[4][2] = { { a1x, a1z }, { a2x, a2z }, { rx, rz }, { fx, fz } };
	double minOverlap = kInf, nx = 0, nz = 0;
	for (const auto& a : axes) {
		const double ax = a[0], az = a[1];
		const double ra = std::fabs(rx * ax + rz * az) * hx + std::fabs(fx * ax + fz * az) * hz;
		const double rb = std::fabs(a1x * ax + a1z * az) * bhx + std::fabs(a2x * ax + a2z * az) * bhz;
		const double dist = dx * ax + dz * az;
		const double ov = ra + rb - std::fabs(dist);
		if (ov <= 0) return false;
		if (ov < minOverlap) { minOverlap = ov; const double sgn = dist > 0 ? -1 : 1; nx = ax * sgn; nz = az * sgn; }
	}
	double best = -kInf, px = cx, pz = cz;
	const int corners[4][2] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
	for (const auto& c : corners) {
		const double qx = cx + rx * hx * c[0] + fx * hz * c[1], qz = cz + rz * hx * c[0] + fz * hz * c[1];
		const double d = -(qx * nx + qz * nz);
		if (d > best) { best = d; px = qx; pz = qz; }
	}
	const double l1 = Clamp((px - bx) * a1x + (pz - bz) * a1z, -bhx, bhx), l2 = Clamp((px - bx) * a2x + (pz - bz) * a2z, -bhz, bhz);
	out.nx = nx; out.nz = nz; out.depth = minOverlap; out.px = bx + a1x * l1 + a2x * l2; out.pz = bz + a1z * l1 + a2z * l2; out.obj = nullptr;
	return true;
}

CollisionWorld::CollisionWorld(const CityMap& m) : map(m) {
	for (const auto& c : m.colliders) {
		if (c.oriented) addOBox(c.cx, c.cz, c.hx, c.hz, c.yaw, c.minY, c.maxY, c.type, c.soft);
		else addBox(c.minX, c.minY, c.minZ, c.maxX, c.maxY, c.maxZ, c.type, c.soft);
	}
}

CollObj* CollisionWorld::insert(std::unique_ptr<CollObj> o, double minX, double minZ, double maxX, double maxZ) {
	CollObj* p = o.get();
	const int64_t x0 = (int64_t)std::floor(minX / CELL), x1 = (int64_t)std::floor(maxX / CELL);
	const int64_t z0 = (int64_t)std::floor(minZ / CELL), z1 = (int64_t)std::floor(maxZ / CELL);
	for (int64_t ix = x0; ix <= x1; ix++) for (int64_t iz = z0; iz <= z1; iz++) {
		const int64_t k = Key(ix, iz);
		cells[k].push_back(p);
		p->cells.push_back(k);
	}
	objects.push_back(std::move(o));
	return p;
}

void CollisionWorld::remove(CollObj* o) {
	if (!o || o->removed) return;
	for (int64_t k : o->cells) {
		auto it = cells.find(k);
		if (it == cells.end()) continue;
		auto& v = it->second;
		for (size_t i = 0; i < v.size(); i++) if (v[i] == o) { v.erase(v.begin() + i); break; }
	}
	o->cells.clear();
	o->removed = true;
}

CollObj* CollisionWorld::addBox(double minX, double minY, double minZ, double maxX, double maxY, double maxZ, const std::string& type, bool soft) {
	auto o = std::make_unique<CollObj>();
	o->kind = CollObj::Box; o->minX = minX; o->minY = minY; o->minZ = minZ; o->maxX = maxX; o->maxY = maxY; o->maxZ = maxZ; o->type = type; o->soft = soft;
	return insert(std::move(o), minX, minZ, maxX, maxZ);
}

CollObj* CollisionWorld::addOBox(double cx, double cz, double hx, double hz, double yaw, double minY, double maxY, const std::string& type, bool soft, bool low) {
	auto o = std::make_unique<CollObj>();
	o->kind = CollObj::OBox; o->cx = cx; o->cz = cz; o->hx = hx; o->hz = hz; o->yaw = yaw; o->minY = minY; o->maxY = maxY; o->type = type; o->soft = soft; o->low = low;
	o->s = std::sin(yaw); o->c = std::cos(yaw);
	const double ex = std::fabs(o->c) * hx + std::fabs(o->s) * hz, ez = std::fabs(o->s) * hx + std::fabs(o->c) * hz;
	o->minX = cx - ex; o->maxX = cx + ex; o->minZ = cz - ez; o->maxZ = cz + ez;
	const double a = o->minX, b = o->minZ, c = o->maxX, d = o->maxZ;
	return insert(std::move(o), a, b, c, d);
}

CollObj* CollisionWorld::addCircle(double x, double z, double r, double h, double y0, const std::string& type, int prop, bool breakable) {
	auto o = std::make_unique<CollObj>();
	o->kind = CollObj::Circle; o->x = x; o->z = z; o->r = r; o->h = h; o->y0 = y0; o->type = type; o->prop = prop; o->breakable = breakable;
	return insert(std::move(o), x - r, z - r, x + r, z + r);
}

CollObj* CollisionWorld::addDeck(double ax, double az, double ay, double bx, double bz, double by, double hl, double hr, int edge, bool pavement, bool skate) {
	auto o = std::make_unique<CollObj>();
	CollObj& d = *o;
	d.kind = CollObj::Deck; d.ax = ax; d.az = az; d.ay = ay; d.bx = bx; d.bz = bz; d.by = by; d.hl = hl; d.hr = hr; d.edge = edge; d.pavement = pavement; d.skate = skate;
	const double dx = bx - ax, dz = bz - az;
	d.len = Hypot(dx, dz); if (d.len == 0) d.len = 1;
	d.dx = dx / d.len; d.dz = dz / d.len;
	const double pad = Max(hl, hr) + 0.5;
	d.minX = Min(ax, bx) - pad; d.maxX = Max(ax, bx) + pad;
	d.minZ = Min(az, bz) - pad; d.maxZ = Max(az, bz) + pad;
	d.minY = Min(ay, by) - DECK_H; d.maxY = Max(ay, by);
	const double a = d.minX, b = d.minZ, c = d.maxX, e = d.maxZ;
	return insert(std::move(o), a, b, c, e);
}

CollObj* CollisionWorld::add(const ColPrim& p) {
	switch (p.kind) {
	case ColPrim::Box: return addBox(p.minX, p.minY, p.minZ, p.maxX, p.maxY, p.maxZ, p.type, p.soft);
	case ColPrim::OBox: return addOBox(p.cx, p.cz, p.hx, p.hz, p.yaw, p.minY, p.maxY, p.type, p.soft, p.low);
	case ColPrim::Circle: return addCircle(p.x, p.z, p.r, p.h, p.y0, p.type, p.prop, p.breakable);
	default: return addDeck(p.ax, p.az, p.ay, p.bx, p.bz, p.by, p.hl, p.hr, p.edge, p.pavement, p.skate);
	}
}

double CollisionWorld::deckAt(const CollObj* d, double x, double z) const {
	const double px = x - d->ax, pz = z - d->az;
	const double t = px * d->dx + pz * d->dz;
	if (t < -0.3 || t > d->len + 0.3) return -kInf;
	const double lat = px * -d->dz + pz * d->dx;
	if (lat < -d->hl || lat > d->hr) return -kInf;
	const double k = Clamp(t / d->len, 0, 1);
	return d->ay + (d->by - d->ay) * k;
}
double CollisionWorld::deckAtClamped(const CollObj* d, double t) const { const double k = Clamp(t / d->len, 0, 1); return d->ay + (d->by - d->ay) * k; }

std::vector<CollObj*>& CollisionWorld::query(double minX, double minZ, double maxX, double maxZ, std::vector<CollObj*>& out) {
	stampN++;
	const int64_t x0 = (int64_t)std::floor(minX / CELL), x1 = (int64_t)std::floor(maxX / CELL);
	const int64_t z0 = (int64_t)std::floor(minZ / CELL), z1 = (int64_t)std::floor(maxZ / CELL);
	if (x1 - x0 > 400 || z1 - z0 > 400) return out; // (a NaN or runaway query)
	for (int64_t ix = x0; ix <= x1; ix++) for (int64_t iz = z0; iz <= z1; iz++) {
		auto it = cells.find(Key(ix, iz));
		if (it == cells.end()) continue;
		for (CollObj* o : it->second) if (o->stamp != stampN) { o->stamp = stampN; out.push_back(o); }
	}
	return out;
}

CollisionWorld::CircleRes CollisionWorld::resolveCircle(double x, double z, double r, double y, double h) {
	CircleRes out{ x, z, nullptr };
	if (!Finite(x) || !Finite(z)) return out;
	tmp.clear();
	query(x - r - 1, z - r - 1, x + r + 1, z + r + 1, tmp);
	for (CollObj* o : tmp) {
		if (o->kind != CollObj::Circle) {
			if (y + h < o->minY || y > o->maxY - 0.05) continue;
			if (o->maxY - y < 0.45 && o->type != "building") continue; // can step over low stuff
			if (o->kind == CollObj::Deck) {
				const double dh = deckAt(o, out.x, out.z);
				if (dh == -kInf) {
					const double px = out.x - o->ax, pz = out.z - o->az;
					const double t = px * o->dx + pz * o->dz;
					if (t < 0 || t > o->len) continue;
					const double lat = px * -o->dz + pz * o->dx;
					const double edge = lat < 0 ? -o->hl : o->hr;
					const double d = lat - edge;
					if (std::fabs(d) < r && y < deckAtClamped(o, t) - 0.45) {
						const double push = (r - std::fabs(d)) * (d != 0 ? Sgn(d) : 1);
						out.x += -o->dz * push; out.z += o->dx * push; out.hit = o;
					}
				}
				continue;
			}
			double lx, lz, hx, hz;
			if (o->kind == CollObj::OBox) { const double dx = out.x - o->cx, dz = out.z - o->cz; lx = dx * o->c - dz * o->s; lz = dx * o->s + dz * o->c; hx = o->hx; hz = o->hz; }
			else { lx = out.x - (o->minX + o->maxX) / 2; lz = out.z - (o->minZ + o->maxZ) / 2; hx = (o->maxX - o->minX) / 2; hz = (o->maxZ - o->minZ) / 2; }
			const double qx = Clamp(lx, -hx, hx), qz = Clamp(lz, -hz, hz);
			const double dx = lx - qx, dz = lz - qz;
			const double d2 = dx * dx + dz * dz;
			if (d2 < r * r) {
				double nlx = lx, nlz = lz;
				if (d2 > 1e-8) { const double d = std::sqrt(d2), push = r - d; nlx = lx + dx / d * push; nlz = lz + dz / d * push; }
				else {
					const double l = lx + hx, rr = hx - lx, t = lz + hz, bb = hz - lz;
					const double m = Min(Min(l, rr), Min(t, bb));
					if (m == l) nlx = -hx - r; else if (m == rr) nlx = hx + r; else if (m == t) nlz = -hz - r; else nlz = hz + r;
				}
				if (o->kind == CollObj::OBox) { out.x = o->cx + nlx * o->c + nlz * o->s; out.z = o->cz - nlx * o->s + nlz * o->c; }
				else { out.x = (o->minX + o->maxX) / 2 + nlx; out.z = (o->minZ + o->maxZ) / 2 + nlz; }
				out.hit = o;
			}
		} else if (!o->broken) {
			if (y > o->top()) continue;
			const double dx = out.x - o->x, dz = out.z - o->z;
			const double rr = r + o->r;
			const double d2 = dx * dx + dz * dz;
			if (d2 < rr * rr && d2 > 1e-8) {
				const double d = std::sqrt(d2);
				out.x = o->x + dx / d * rr; out.z = o->z + dz / d * rr;
				out.hit = o;
			}
		}
	}
	tmp.clear();
	return out;
}

std::vector<Contact>& CollisionWorld::obbContacts(double cx, double cz, double yaw, double hx, double hz, double y, std::vector<Contact>& out) {
	out.clear();
	const double c = std::cos(yaw), s = std::sin(yaw);
	const double fx = s, fz = c, rx = c, rz = -s;
	const double ex = std::fabs(rx) * hx + std::fabs(fx) * hz, ez = std::fabs(rz) * hx + std::fabs(fz) * hz;
	tmp.clear();
	query(cx - ex - 1, cz - ez - 1, cx + ex + 1, cz + ez + 1, tmp);
	for (CollObj* o : tmp) {
		if (o->kind != CollObj::Circle) {
			if (o->rayOnly) continue;
			if (o->kind == CollObj::Deck) {
				if (y + 1.0 < o->minY) continue;
				const double top = Max(deckAt(o, cx, cz), -1e9);
				if (top > -1e9) continue;
				if (y + 0.4 > o->maxY) continue;
				const double bx = (o->ax + o->bx) / 2 + -o->dz * (o->hr - o->hl) / 2, bz = (o->az + o->bz) / 2 + o->dx * (o->hr - o->hl) / 2;
				Contact res;
				if (!SatOBB(cx, cz, fx, fz, rx, rz, hx, hz, bx, bz, -o->dz, o->dx, (o->hl + o->hr) / 2, o->len / 2, o->dx, o->dz, res)) continue;
				const double dh = deckAtClamped(o, (res.px - o->ax) * o->dx + (res.pz - o->az) * o->dz);
				if (dh - y < 0.45) continue;
				res.obj = o;
				out.push_back(res);
				continue;
			}
			if (y + 1.2 < o->minY || y + 0.25 > o->maxY) continue;
			double bx, bz, a1x, a1z, bhx, bhz;
			if (o->kind == CollObj::OBox) { bx = o->cx; bz = o->cz; bhx = o->hx; bhz = o->hz; a1x = o->c; a1z = -o->s; }
			else { bx = (o->minX + o->maxX) / 2; bz = (o->minZ + o->maxZ) / 2; bhx = (o->maxX - o->minX) / 2; bhz = (o->maxZ - o->minZ) / 2; a1x = 1; a1z = 0; }
			const double a2x = o->kind == CollObj::OBox ? o->s : 0, a2z = o->kind == CollObj::OBox ? o->c : 1;
			Contact res;
			if (!SatOBB(cx, cz, fx, fz, rx, rz, hx, hz, bx, bz, a1x, a1z, bhx, bhz, a2x, a2z, res)) continue;
			res.obj = o;
			out.push_back(res);
		} else if (!o->broken) {
			if (y > o->top()) continue;
			const double lx = o->x - cx, lz = o->z - cz;
			const double lr = lx * rx + lz * rz, lf = lx * fx + lz * fz;
			const double qr = Clamp(lr, -hx, hx), qf = Clamp(lf, -hz, hz);
			const double wx = cx + rx * qr + fx * qf, wz = cz + rz * qr + fz * qf;
			double dx = wx - o->x, dz = wz - o->z;
			const double d2 = dx * dx + dz * dz;
			if (d2 < o->r * o->r) {
				double d = std::sqrt(d2); if (d == 0) d = 1e-4;
				if (d2 < 1e-8) { dx = cx - o->x; dz = cz - o->z; }
				double dl = Hypot(dx, dz); if (dl == 0) dl = 1;
				out.push_back({ dx / dl, dz / dl, o->r - d, wx, wz, o });
			}
		}
	}
	tmp.clear();
	return out;
}

bool CollisionWorld::raycast(double ox, double oy, double oz, double dx, double dy, double dz, double maxDist, RayHit& hit, const RayOpts& opts) {
	double bestT = maxDist, nX = 0, nY = 0, nZ = 0;
	CollObj* bestObj = nullptr;
	bool ground = false;
	if (!Finite(ox) || !Finite(oy) || !Finite(oz) || !Finite(dx) || !Finite(dy) || !Finite(dz) || !Finite(maxDist)) return false;
	const double step = maxDist > 600 ? 4 : 2;
	double prevT = 0;
	for (double t = 0; t <= Min(maxDist, 1500); t += step) {
		const double px = ox + dx * t, py = oy + dy * t, pz = oz + dz * t;
		const double gh = map.GroundHeight(px, pz);
		if (py < gh) {
			double a = prevT, b = t;
			for (int k = 0; k < 8; k++) { const double m = (a + b) / 2; const double my = oy + dy * m; if (my < map.GroundHeight(ox + dx * m, oz + dz * m)) b = m; else a = m; }
			if (b < bestT) { bestT = b; ground = true; nX = 0; nY = 1; nZ = 0; }
			break;
		}
		prevT = t;
	}
	const double ex = ox + dx * bestT, ez = oz + dz * bestT;
	tmp.clear();
	query(Min(ox, ex), Min(oz, ez), Max(ox, ex), Max(oz, ez), tmp);
	for (CollObj* o : tmp) {
		if (o->kind == CollObj::Box) {
			if (opts.ignoreSoft && o->soft) continue;
			const double t = RayBox(ox, oy, oz, dx, dy, dz, o->minX, o->minY, o->minZ, o->maxX, o->maxY, o->maxZ);
			if (t >= 0 && t < bestT) { bestT = t; bestObj = o; ground = false; nX = N3[0]; nY = N3[1]; nZ = N3[2]; }
		} else if (o->kind == CollObj::OBox || o->kind == CollObj::Deck) {
			if (opts.ignoreSoft && o->soft) continue;
			double cxo, czo, s, c, hx, hz, y0, y1;
			if (o->kind == CollObj::OBox) { cxo = o->cx; czo = o->cz; s = o->s; c = o->c; hx = o->hx; hz = o->hz; y0 = o->minY; y1 = o->maxY; }
			else { cxo = (o->ax + o->bx) / 2 + -o->dz * (o->hr - o->hl) / 2; czo = (o->az + o->bz) / 2 + o->dx * (o->hr - o->hl) / 2; s = o->dx; c = o->dz; hx = (o->hl + o->hr) / 2; hz = o->len / 2; y0 = o->minY; y1 = o->maxY; }
			const double lox = (ox - cxo) * c - (oz - czo) * s, loz = (ox - cxo) * s + (oz - czo) * c;
			const double ldx = dx * c - dz * s, ldz = dx * s + dz * c;
			const double t = RayBox(lox, oy, loz, ldx, dy, ldz, -hx, y0, -hz, hx, y1, hz);
			if (t >= 0 && t < bestT) {
				bestT = t; bestObj = o; ground = false;
				const double nlx = N3[0], nlz = N3[2];
				nX = nlx * c + nlz * s; nY = N3[1]; nZ = -nlx * s + nlz * c;
			}
		} else if (!o->broken && !opts.ignoreProps) {
			const double lx = ox - o->x, lz = oz - o->z;
			const double a = dx * dx + dz * dz; if (a < 1e-8) continue;
			const double b = 2 * (lx * dx + lz * dz), c = lx * lx + lz * lz - o->r * o->r;
			const double disc = b * b - 4 * a * c; if (disc < 0) continue;
			const double t = (-b - std::sqrt(disc)) / (2 * a);
			if (t >= 0 && t < bestT) {
				const double hy = oy + dy * t;
				if (hy < o->top() && hy > (IsSet(o->y0) ? o->y0 : -50)) {
					bestT = t; bestObj = o; ground = false;
					const double hx = ox + dx * t - o->x, hz = oz + dz * t - o->z;
					double hl = Hypot(hx, hz); if (hl == 0) hl = 1;
					nX = hx / hl; nY = 0; nZ = hz / hl;
				}
			}
		}
	}
	tmp.clear();
	if (!bestObj && !ground) return false;
	hit = { bestT, ox + dx * bestT, oy + dy * bestT, oz + dz * bestT, nX, nY, nZ, bestObj, ground && !bestObj };
	return true;
}

double CollisionWorld::floorHeight(double x, double z, double y, double step) {
	double best = map.GroundHeight(x, z);
	if (!Finite(x) || !Finite(z)) return best;
	auto it = cells.find(Key((int64_t)std::floor(x / CELL), (int64_t)std::floor(z / CELL)));
	if (it == cells.end()) return best;
	const auto& arr = it->second;
	if (y - best < 0.3) {
		bool anyDeck = false;
		for (const CollObj* o : arr) if (o->kind == CollObj::Deck) { anyDeck = true; break; }
		if (!anyDeck) return best;
	}
	for (const CollObj* o : arr) {
		if (o->removed) continue;
		if (o->kind == CollObj::Deck) { const double h = deckAt(o, x, z); if (h <= y + step && h > best) best = h; continue; }
		if (o->kind == CollObj::Box) {
			if (x < o->minX || x > o->maxX || z < o->minZ || z > o->maxZ) continue;
			if (o->maxY <= y + step && o->maxY > best) best = o->maxY;
		} else if (o->kind == CollObj::OBox) {
			const double dx = x - o->cx, dz = z - o->cz;
			if (std::fabs(dx * o->c - dz * o->s) > o->hx || std::fabs(dx * o->s + dz * o->c) > o->hz) continue;
			if (o->maxY <= y + step && o->maxY > best && !o->low) best = o->maxY;
		}
	}
	return best;
}

double CollisionWorld::surfaceHeight(double x, double z, double y, double step) {
	double best = map.GroundHeight(x, z);
	if (!Finite(x) || !Finite(z)) return best;
	auto it = cells.find(Key((int64_t)std::floor(x / CELL), (int64_t)std::floor(z / CELL)));
	if (it == cells.end()) return best;
	for (const CollObj* o : it->second) {
		if (o->kind != CollObj::Deck) continue;
		const double h = deckAt(o, x, z);
		if (h <= y + step && h > best) best = h;
	}
	return best;
}

bool CollisionWorld::lineOfSight(double ax, double ay, double az, double bx, double by, double bz) {
	const double dx = bx - ax, dy = by - ay, dz = bz - az;
	const double d = Hypot3(dx, dy, dz);
	if (d < 0.01) return true;
	RayHit h;
	RayOpts o; o.ignoreProps = true; o.ignoreSoft = true;
	return !raycast(ax, ay, az, dx / d, dy / d, dz / d, d, h, o);
}

} // namespace atg
