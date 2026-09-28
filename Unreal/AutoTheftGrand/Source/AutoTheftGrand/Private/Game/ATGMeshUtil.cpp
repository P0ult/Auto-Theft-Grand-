#include "Game/ATGMeshUtil.h"
#include "AutoTheftGrand.h"
#include "Game/ATGCoords.h"
#include "Gen/MeshBuf.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

namespace {
FVector Pos(const atg::MeshBuf& B, size_t i, EATGAxes Axes) {
	const float* p = &B.P[i * 3];
	return Axes == EATGAxes::World ? ATG::ToUE(p[0], p[1], p[2]) : ATG::LocalToUE(p[0], p[1], p[2]);
}
FVector Nrm(const atg::MeshBuf& B, size_t i, EATGAxes Axes) {
	if (B.N.size() < (i + 1) * 3) return FVector::UpVector;
	const float* n = &B.N[i * 3];
	return Axes == EATGAxes::World ? ATG::DirToUE(n[0], n[1], n[2]) : ATG::LocalDirToUE(n[0], n[1], n[2]);
}
FVector2D UV(const atg::MeshBuf& B, int Ch, size_t i) {
	const std::vector<float>& c = B.C[Ch];
	return c.size() >= (i + 1) * 2 ? FVector2D(c[i * 2], c[i * 2 + 1]) : FVector2D::ZeroVector;
}
// any unit vector at right angles to the normal (the materials use no normal maps; the tangent only has to be valid)
FVector AnyTangent(const FVector& N) {
	const FVector Ref = FMath::Abs(N.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector;
	return FVector::CrossProduct(Ref, N).GetSafeNormal();
}
}

void ATGMesh::ToSection(UProceduralMeshComponent* C, int32 Section, const atg::MeshBuf& B, bool bCollision, EATGAxes Axes) {
	const size_t N = B.Count();
	TArray<FVector> V; V.SetNumUninitialized(N);
	TArray<FVector> Nr; Nr.SetNumUninitialized(N);
	TArray<FVector2D> U[4];
	for (int32 c = 0; c < 4; c++) U[c].SetNumUninitialized(N);
	TArray<FProcMeshTangent> T; T.SetNumUninitialized(N);
	for (size_t i = 0; i < N; i++) {
		V[i] = Pos(B, i, Axes);
		Nr[i] = Nrm(B, i, Axes);
		T[i] = FProcMeshTangent(AnyTangent(Nr[i]), false);
		for (int32 c = 0; c < 4; c++) U[c][i] = UV(B, c, i);
	}
	TArray<int32> I; I.SetNumUninitialized(B.I.size());
	for (size_t k = 0; k < B.I.size(); k++) I[k] = (int32)B.I[k];
	C->CreateMeshSection_LinearColor(Section, V, I, Nr, U[0], U[1], U[2], U[3], TArray<FLinearColor>(), T, bCollision);
}

void ATGMesh::ToCollisionSection(UProceduralMeshComponent* C, int32 Section, const atg::MeshBuf& B) {
	const size_t N = B.Count();
	TArray<FVector> V; V.SetNumUninitialized(N);
	for (size_t i = 0; i < N; i++) V[i] = Pos(B, i, EATGAxes::World);
	TArray<int32> I; I.SetNumUninitialized(B.I.size());
	for (size_t k = 0; k < B.I.size(); k++) I[k] = (int32)B.I[k];
	C->CreateMeshSection(Section, V, I, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), true);
	C->SetMeshSectionVisible(Section, false);
}

UStaticMesh* ATGMesh::BuildStaticMesh(UObject* Outer, const TCHAR* Name, const TArray<TArray<FATGPart>>& Lods, EATGAxes Axes) {
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer, MakeUniqueObjectName(Outer, UStaticMesh::StaticClass(), FName(Name)));
	// one material slot per distinct material, named so the polygon groups can find theirs
	TArray<UMaterialInterface*> Mats;
	for (const TArray<FATGPart>& L : Lods) for (const FATGPart& P : L) if (P.Mesh && !P.Mesh->Empty()) Mats.AddUnique(P.Material);
	auto SlotName = [](int32 i) { return FName(*FString::Printf(TEXT("Slot%d"), i)); };
	for (int32 i = 0; i < Mats.Num(); i++) SM->GetStaticMaterials().Add(FStaticMaterial(Mats[i], SlotName(i)));

	TArray<FMeshDescription> Descs;
	Descs.SetNum(Lods.Num());
	for (int32 l = 0; l < Lods.Num(); l++) {
		FMeshDescription& MD = Descs[l];
		FStaticMeshAttributes A(MD);
		A.Register();
		TVertexAttributesRef<FVector3f> Positions = A.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = A.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = A.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = A.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector4f> Colors = A.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = A.GetVertexInstanceUVs();
		UVs.SetNumChannels(4);
		TPolygonGroupAttributesRef<FName> Slots = A.GetPolygonGroupMaterialSlotNames();
		int32 NV = 0, NT = 0;
		for (const FATGPart& P : Lods[l]) if (P.Mesh) { NV += (int32)P.Mesh->Count(); NT += (int32)P.Mesh->I.size() / 3; }
		MD.ReserveNewVertices(NV);
		MD.ReserveNewVertexInstances(NV);
		MD.ReserveNewTriangles(NT);
		MD.ReserveNewPolygons(NT);
		MD.ReserveNewEdges(NT * 2);
		for (const FATGPart& P : Lods[l]) {
			if (!P.Mesh || P.Mesh->Empty()) continue;
			const atg::MeshBuf& B = *P.Mesh;
			const FPolygonGroupID G = MD.CreatePolygonGroup();
			Slots[G] = SlotName(Mats.IndexOfByKey(P.Material));
			TArray<FVertexInstanceID> VI;
			VI.SetNumUninitialized(B.Count());
			for (size_t i = 0; i < B.Count(); i++) {
				const FVertexID Vx = MD.CreateVertex();
				Positions[Vx] = FVector3f(Pos(B, i, Axes));
				const FVertexInstanceID Id = MD.CreateVertexInstance(Vx);
				const FVector Nn = Nrm(B, i, Axes);
				Normals[Id] = FVector3f(Nn);
				Tangents[Id] = FVector3f(AnyTangent(Nn));
				Signs[Id] = 1.f;
				Colors[Id] = FVector4f(1.f, 1.f, 1.f, 1.f);
				for (int32 c = 0; c < 4; c++) UVs.Set(Id, c, FVector2f(UV(B, c, i)));
				VI[i] = Id;
			}
			for (size_t k = 0; k + 2 < B.I.size(); k += 3) {
				const uint32_t a = B.I[k], b = B.I[k + 1], c = B.I[k + 2];
				if (a == b || b == c || a == c) continue;
				const FVertexInstanceID Tri[3] = { VI[a], VI[b], VI[c] };
				MD.CreateTriangle(G, TArrayView<const FVertexInstanceID>(Tri, 3));
			}
		}
	}
	TArray<const FMeshDescription*> Ptrs;
	for (const FMeshDescription& D : Descs) Ptrs.Add(&D);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bBuildSimpleCollision = false;
	Params.bAllowCpuAccess = false;
	Params.bMarkPackageDirty = false;
	Params.bCommitMeshDescription = false;
	if (!SM->BuildFromMeshDescriptions(Ptrs, Params)) UE_LOG(LogATG, Warning, TEXT("Could not build mesh %s"), Name);
	return SM;
}

void ATGMesh::SetLodDistances(UStaticMesh* Mesh, const TArray<double>& Metres) {
	FStaticMeshRenderData* RD = Mesh ? Mesh->GetRenderData() : nullptr;
	if (!RD) return;
	// Unreal picks a level of detail from the fraction of the screen the bounding sphere covers
	// (about radius / distance at a 90 degree field of view)
	const double R = FMath::Max(1.0, (double)Mesh->GetBounds().SphereRadius);
	for (int32 l = 1; l < RD->LODResources.Num() && l - 1 < Metres.Num(); l++) RD->ScreenSize[l].Default = (float)FMath::Clamp(R / (Metres[l - 1] * 100.0), 0.0005, 1.0);
}
