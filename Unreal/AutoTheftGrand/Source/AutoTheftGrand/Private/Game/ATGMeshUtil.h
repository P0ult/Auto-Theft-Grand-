// Turning the generator's MeshBufs into Unreal geometry: procedural mesh sections (the big one-off
// meshes: terrain, roads, ground, buildings, water, colliders) and runtime static meshes (things that are
// instanced or reused: props, vegetation, car parts). See ATGCoords.h for the axis mapping.
#pragma once

#include "CoreMinimal.h"

namespace atg { struct MeshBuf; }
class UProceduralMeshComponent;
class UStaticMesh;
class UMaterialInterface;

enum class EATGAxes : uint8 {
	World, // x, y, z -> X = x, Y = z, Z = y (the world, props, trees)
	Local, // x, y, z -> X = z, Y = -x, Z = y (things modelled facing +z: vehicles, people)
};

struct FATGPart {
	const atg::MeshBuf* Mesh = nullptr;
	UMaterialInterface* Material = nullptr;
};

namespace ATGMesh {
	// Upload a buffer as a section of a procedural mesh component (optionally with collision)
	void ToSection(UProceduralMeshComponent* C, int32 Section, const atg::MeshBuf& B, bool bCollision, EATGAxes Axes = EATGAxes::World);
	// Collision-only section (positions and triangles only)
	void ToCollisionSection(UProceduralMeshComponent* C, int32 Section, const atg::MeshBuf& B);
	// A static mesh built at runtime: one level of detail per entry of Lods, one section per part
	UStaticMesh* BuildStaticMesh(UObject* Outer, const TCHAR* Name, const TArray<TArray<FATGPart>>& Lods, EATGAxes Axes = EATGAxes::World);
	inline UStaticMesh* BuildStaticMesh(UObject* Outer, const TCHAR* Name, const TArray<FATGPart>& Parts, EATGAxes Axes = EATGAxes::World) {
		TArray<TArray<FATGPart>> L; L.Add(Parts); return BuildStaticMesh(Outer, Name, L, Axes);
	}
	// set the distance (metres) at which each LOD after the first takes over, from the mesh's bounds
	void SetLodDistances(UStaticMesh* Mesh, const TArray<double>& Metres);
}
