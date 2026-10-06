// Between the game's axes (metres; x east, y up, z south; right-handed, as in the browser game) and
// Unreal's (centimetres; X, Y, Z up; left-handed). World geometry maps x, y, z -> X = x, Y = z, Z = y. Things
// modelled facing +z (vehicles, people) map x, y, z -> X = z, Y = -x, Z = y, so they face Unreal's +X.
// Both maps are reflections, which is exactly what turns right-handed counter-clockwise triangles into
// Unreal's front faces, so index order is kept.
//
// The simulation hands out transforms as game-space matrices (atg::M4). A matrix that takes a thing's own
// frame to the world becomes an Unreal transform with ToUE(M) (world map on the outside, local map inside);
// a transform between two of a thing's own frames (a wheel on its car, a bone on its parent) with LocalToUE(M).
#pragma once

#include "CoreMinimal.h"
#include "Sim/Core.h"

namespace ATG {
	constexpr double M = 100.0;
	inline FVector ToUE(double x, double y, double z) { return FVector(x * M, z * M, y * M); }
	inline FVector ToUE(const atg::V3& v) { return ToUE(v.x, v.y, v.z); }
	inline FVector DirToUE(double x, double y, double z) { return FVector(x, z, y); }
	inline FVector DirToUE(const atg::V3& v) { return FVector(v.x, v.z, v.y); }
	inline void FromUE(const FVector& V, double& x, double& y, double& z) { x = V.X / M; y = V.Z / M; z = V.Y / M; }
	inline atg::V3 FromUE(const FVector& V) { return atg::V3(V.X / M, V.Z / M, V.Y / M); }
	inline FVector LocalToUE(double x, double y, double z) { return FVector(z * M, -x * M, y * M); }
	inline FVector LocalToUE(const atg::V3& v) { return LocalToUE(v.x, v.y, v.z); }
	inline FVector LocalDirToUE(double x, double y, double z) { return FVector(z, -x, y); }
	// a world-mapped mesh turned by yaw about +y (three.js rotation.y) -> Unreal yaw in degrees
	inline double MeshYaw(double Yaw) { return -FMath::RadiansToDegrees(Yaw); }
	// something whose heading is yaw (forward = sin yaw, cos yaw) and that faces Unreal +X -> Unreal yaw in degrees
	inline double HeadingYaw(double Yaw) { return 90.0 - FMath::RadiansToDegrees(Yaw); }
	inline double HeadingFromUE(double Deg) { return FMath::DegreesToRadians(90.0 - Deg); }

	namespace Detail {
		// game -> Unreal axis maps (without the centimetres): World (x, y, z) -> (x, z, y); Local (x, y, z) -> (z, -x, y)
		inline FVector MapW(const atg::V3& v) { return FVector(v.x, v.z, v.y); }
		inline FVector MapL(const atg::V3& v) { return FVector(v.z, -v.x, v.y); }
		inline atg::V3 UnmapL(const FVector& v) { return atg::V3(-v.Y, v.Z, v.X); }
		inline atg::V3 UnmapW(const FVector& v) { return atg::V3(v.X, v.Z, v.Y); }
		inline FTransform Make(const atg::M4& m, bool bOuterWorld, bool bInnerWorld) {
			const atg::V3 c0(m.m[0], m.m[1], m.m[2]), c1(m.m[4], m.m[5], m.m[6]), c2(m.m[8], m.m[9], m.m[10]);
			const double s = FMath::Max(1e-9, c0.length());
			auto Apply = [&](const atg::V3& v) { return (c0 * v.x + c1 * v.y + c2 * v.z) / s; };
			const FVector E[3] = { FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1) };
			FVector Rows[3];
			for (int32 I = 0; I < 3; I++) {
				const atg::V3 g = bInnerWorld ? UnmapW(E[I]) : UnmapL(E[I]);
				const atg::V3 r = Apply(g);
				Rows[I] = bOuterWorld ? MapW(r) : MapL(r);
			}
			FMatrix R = FMatrix::Identity;
			R.SetAxes(&Rows[0], &Rows[1], &Rows[2]);
			const atg::V3 t(m.m[12], m.m[13], m.m[14]);
			return FTransform(FQuat(R), (bOuterWorld ? MapW(t) : MapL(t)) * M, FVector(s));
		}
	}
	// a thing modelled facing +z, placed in the world by m
	inline FTransform ToUE(const atg::M4& m) { return Detail::Make(m, true, false); }
	// a world-mapped mesh (props, the world) placed by m
	inline FTransform WorldToUE(const atg::M4& m) { return Detail::Make(m, true, true); }
	// one of a thing's own frames relative to another (both modelled facing +z)
	inline FTransform LocalToUE(const atg::M4& m) { return Detail::Make(m, false, false); }
}
