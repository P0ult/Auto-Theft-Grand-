#include "Core.h"

namespace atg {

namespace {
// xoshiro-ish splitmix generator for Math.random (any decent uniform generator will do)
uint64_t GRand = 0x9e3779b97f4a7c15ULL;
}
void SeedRand(uint64_t seed) { GRand = seed ? seed : 0x9e3779b97f4a7c15ULL; }
double Rand() {
	uint64_t z = (GRand += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	z ^= z >> 31;
	return (double)(z >> 11) / 9007199254740992.0;
}

// ------------------------------------------------------------------ Quat
Quat Quat::FromEuler(double ex, double ey, double ez, const char* order) {
	const double c1 = std::cos(ex / 2), c2 = std::cos(ey / 2), c3 = std::cos(ez / 2);
	const double s1 = std::sin(ex / 2), s2 = std::sin(ey / 2), s3 = std::sin(ez / 2);
	Quat q;
	if (order[0] == 'Y' && order[1] == 'X') { // YXZ
		q.x = s1 * c2 * c3 + c1 * s2 * s3; q.y = c1 * s2 * c3 - s1 * c2 * s3; q.z = c1 * c2 * s3 - s1 * s2 * c3; q.w = c1 * c2 * c3 + s1 * s2 * s3;
	} else { // XYZ
		q.x = s1 * c2 * c3 + c1 * s2 * s3; q.y = c1 * s2 * c3 - s1 * c2 * s3; q.z = c1 * c2 * s3 + s1 * s2 * c3; q.w = c1 * c2 * c3 - s1 * s2 * s3;
	}
	return q;
}

Quat Quat::FromAxisAngle(const V3& a, double angle) {
	const double h = angle / 2, s = std::sin(h);
	return { a.x * s, a.y * s, a.z * s, std::cos(h) };
}

Quat Quat::FromUnitVectors(const V3& f, const V3& t) {
	double r = f.dot(t) + 1;
	Quat q;
	if (r < 1e-8) {
		r = 0;
		if (std::fabs(f.x) > std::fabs(f.z)) { q.x = -f.y; q.y = f.x; q.z = 0; q.w = r; }
		else { q.x = 0; q.y = -f.z; q.z = f.y; q.w = r; }
	} else {
		q.x = f.y * t.z - f.z * t.y; q.y = f.z * t.x - f.x * t.z; q.z = f.x * t.y - f.y * t.x; q.w = r;
	}
	return q.normalize();
}

Quat Quat::FromMatrix(const double te[16]) {
	const double m11 = te[0], m12 = te[4], m13 = te[8], m21 = te[1], m22 = te[5], m23 = te[9], m31 = te[2], m32 = te[6], m33 = te[10];
	const double trace = m11 + m22 + m33;
	Quat q;
	if (trace > 0) {
		const double s = 0.5 / std::sqrt(trace + 1.0);
		q.w = 0.25 / s; q.x = (m32 - m23) * s; q.y = (m13 - m31) * s; q.z = (m21 - m12) * s;
	} else if (m11 > m22 && m11 > m33) {
		const double s = 2.0 * std::sqrt(1.0 + m11 - m22 - m33);
		q.w = (m32 - m23) / s; q.x = 0.25 * s; q.y = (m12 + m21) / s; q.z = (m13 + m31) / s;
	} else if (m22 > m33) {
		const double s = 2.0 * std::sqrt(1.0 + m22 - m11 - m33);
		q.w = (m13 - m31) / s; q.x = (m12 + m21) / s; q.y = 0.25 * s; q.z = (m23 + m32) / s;
	} else {
		const double s = 2.0 * std::sqrt(1.0 + m33 - m11 - m22);
		q.w = (m21 - m12) / s; q.x = (m13 + m31) / s; q.y = (m23 + m32) / s; q.z = 0.25 * s;
	}
	return q;
}

Quat Quat::operator*(const Quat& b) const {
	const double qax = x, qay = y, qaz = z, qaw = w, qbx = b.x, qby = b.y, qbz = b.z, qbw = b.w;
	return { qax * qbw + qaw * qbx + qay * qbz - qaz * qby, qay * qbw + qaw * qby + qaz * qbx - qax * qbz,
		qaz * qbw + qaw * qbz + qax * qby - qay * qbx, qaw * qbw - qax * qbx - qay * qby - qaz * qbz };
}

Quat& Quat::normalize() {
	double l = length();
	if (l == 0) { x = y = z = 0; w = 1; }
	else { l = 1 / l; x *= l; y *= l; z *= l; w *= l; }
	return *this;
}

V3 Quat::rotate(const V3& v) const {
	const double tx = 2 * (y * v.z - z * v.y), ty = 2 * (z * v.x - x * v.z), tz = 2 * (x * v.y - y * v.x);
	return { v.x + w * tx + y * tz - z * ty, v.y + w * ty + z * tx - x * tz, v.z + w * tz + x * ty - y * tx };
}

Quat Quat::Slerp(const Quat& a, const Quat& qb, double t) {
	if (t == 0) return a;
	if (t == 1) return qb;
	double cosHalf = a.w * qb.w + a.x * qb.x + a.y * qb.y + a.z * qb.z;
	Quat b = qb;
	if (cosHalf < 0) { b = { -qb.x, -qb.y, -qb.z, -qb.w }; cosHalf = -cosHalf; }
	if (cosHalf >= 1.0) return a;
	const double sqrSin = 1.0 - cosHalf * cosHalf;
	if (sqrSin <= 1e-12) {
		const double s = 1 - t;
		Quat r{ s * a.x + t * b.x, s * a.y + t * b.y, s * a.z + t * b.z, s * a.w + t * b.w };
		return r.normalize();
	}
	const double sinHalf = std::sqrt(sqrSin), halfTheta = std::atan2(sinHalf, cosHalf);
	const double ra = std::sin((1 - t) * halfTheta) / sinHalf, rb = std::sin(t * halfTheta) / sinHalf;
	return { a.x * ra + b.x * rb, a.y * ra + b.y * rb, a.z * ra + b.z * rb, a.w * ra + b.w * rb };
}

V3 Quat::toEulerYXZ() const {
	const M4 m = M4::Compose(V3(), *this);
	const double m11 = m.m[0], m12 = m.m[4], m13 = m.m[8], m21 = m.m[1], m22 = m.m[5], m23 = m.m[9], m31 = m.m[2], m32 = m.m[6], m33 = m.m[10];
	(void)m11; (void)m21; (void)m31; (void)m12;
	V3 e;
	e.x = std::asin(-Clamp(m23, -1, 1));
	if (std::fabs(m23) < 0.9999999) { e.y = std::atan2(m13, m33); e.z = std::atan2(m21, m22); }
	else { e.y = std::atan2(-m31, m11); e.z = 0; }
	return e;
}

// ------------------------------------------------------------------ M4
M4 M4::Compose(const V3& p, const Quat& q, const V3& s) {
	M4 r;
	const double x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
	const double xx = q.x * x2, xy = q.x * y2, xz = q.x * z2, yy = q.y * y2, yz = q.y * z2, zz = q.z * z2, wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
	r.m[0] = (1 - (yy + zz)) * s.x; r.m[1] = (xy + wz) * s.x; r.m[2] = (xz - wy) * s.x; r.m[3] = 0;
	r.m[4] = (xy - wz) * s.y; r.m[5] = (1 - (xx + zz)) * s.y; r.m[6] = (yz + wx) * s.y; r.m[7] = 0;
	r.m[8] = (xz + wy) * s.z; r.m[9] = (yz - wx) * s.z; r.m[10] = (1 - (xx + yy)) * s.z; r.m[11] = 0;
	r.m[12] = p.x; r.m[13] = p.y; r.m[14] = p.z; r.m[15] = 1;
	return r;
}

M4 M4::Basis(const V3& X, const V3& Y, const V3& Z) {
	M4 r;
	r.m[0] = X.x; r.m[1] = X.y; r.m[2] = X.z;
	r.m[4] = Y.x; r.m[5] = Y.y; r.m[6] = Y.z;
	r.m[8] = Z.x; r.m[9] = Z.y; r.m[10] = Z.z;
	return r;
}

M4 M4::operator*(const M4& b) const {
	M4 r;
	for (int c = 0; c < 4; c++) for (int row = 0; row < 4; row++) {
		double s = 0;
		for (int k = 0; k < 4; k++) s += m[k * 4 + row] * b.m[c * 4 + k];
		r.m[c * 4 + row] = s;
	}
	return r;
}

V3 M4::apply(const V3& v) const {
	return { m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12], m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13], m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] };
}
V3 M4::applyDir(const V3& v) const {
	return { m[0] * v.x + m[4] * v.y + m[8] * v.z, m[1] * v.x + m[5] * v.y + m[9] * v.z, m[2] * v.x + m[6] * v.y + m[10] * v.z };
}

Quat M4::rotation() const {
	const double sx = V3(m[0], m[1], m[2]).length(), sy = V3(m[4], m[5], m[6]).length(), sz = V3(m[8], m[9], m[10]).length();
	double r[16] = { 0 };
	for (int i = 0; i < 3; i++) { r[i] = m[i] / sx; r[4 + i] = m[4 + i] / sy; r[8 + i] = m[8 + i] / sz; }
	return Quat::FromMatrix(r);
}

M4 M4::inverse() const {
	const double* te = m;
	const double n11 = te[0], n21 = te[1], n31 = te[2], n41 = te[3], n12 = te[4], n22 = te[5], n32 = te[6], n42 = te[7];
	const double n13 = te[8], n23 = te[9], n33 = te[10], n43 = te[11], n14 = te[12], n24 = te[13], n34 = te[14], n44 = te[15];
	const double t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
	const double t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
	const double t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
	const double t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;
	const double det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
	M4 r;
	if (det == 0) { for (double& v : r.m) v = 0; return r; }
	const double di = 1 / det;
	double* o = r.m;
	o[0] = t11 * di;
	o[1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * di;
	o[2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * di;
	o[3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * di;
	o[4] = t12 * di;
	o[5] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * di;
	o[6] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * di;
	o[7] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * di;
	o[8] = t13 * di;
	o[9] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * di;
	o[10] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * di;
	o[11] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * di;
	o[12] = t14 * di;
	o[13] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * di;
	o[14] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * di;
	o[15] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * di;
	return r;
}

} // namespace atg
