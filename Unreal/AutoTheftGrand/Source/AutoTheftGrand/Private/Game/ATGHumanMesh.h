// The skinned humanoid as an Unreal skeletal mesh, built at runtime from the simulation's mesh
// (Sim/Humanoid.cpp, the port of humanoid.js): the 17 bones in the rest pose, the smooth-skinned shell with
// up to four influences a vertex, and the colours in the vertex-lit material's channels. The same steps
// as the engine's own runtime mesh merge (SkeletalMeshMerge.cpp), so it works in a packaged game too.
#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class USkeleton;
class UMaterialInterface;
namespace atg { struct Appearance; }

namespace ATGHuman {
	// a key that is the same for two appearances with the same mesh
	FString Key(const atg::Appearance& A);
	// Skeleton: shared by every person (built from the first mesh when null)
	USkeletalMesh* Build(UObject* Outer, const atg::Appearance& A, USkeleton*& Skeleton, UMaterialInterface* Material);
}
