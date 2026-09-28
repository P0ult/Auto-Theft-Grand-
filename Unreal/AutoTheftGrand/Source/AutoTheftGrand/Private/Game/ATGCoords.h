// Between the game's axes (metres; x east, y up, z south; right-handed, as in the browser game) and
// Unreal's (centimetres; X, Y, Z up; left-handed). World geometry maps x, y, z -> X = x, Y = z, Z = y. Things
// modelled facing +z (vehicles, people) map x, y, z -> X = z, Y = -x, Z = y, so they face Unreal's +X.
// Both maps are reflections, which is exactly what turns right-handed counter-clockwise triangles into
// Unreal's front faces, so index order is kept.
#pragma once

#include "CoreMinimal.h"

namespace ATG {
	constexpr double M = 100.0;
	inline FVector ToUE(double x, double y, double z) { return FVector(x * M, z * M, y * M); }
	inline FVector DirToUE(double x, double y, double z) { return FVector(x, z, y); }
	inline void FromUE(const FVector& V, double& x, double& y, double& z) { x = V.X / M; y = V.Z / M; z = V.Y / M; }
	inline FVector LocalToUE(double x, double y, double z) { return FVector(z * M, -x * M, y * M); }
	inline FVector LocalDirToUE(double x, double y, double z) { return FVector(z, -x, y); }
	// a world-mapped mesh turned by yaw about +y (three.js rotation.y) -> Unreal yaw in degrees
	inline double MeshYaw(double Yaw) { return -FMath::RadiansToDegrees(Yaw); }
	// something whose heading is yaw (forward = sin yaw, cos yaw) and that faces Unreal +X -> Unreal yaw in degrees
	inline double HeadingYaw(double Yaw) { return 90.0 - FMath::RadiansToDegrees(Yaw); }
	inline double HeadingFromUE(double Deg) { return FMath::DegreesToRadians(90.0 - Deg); }
}
