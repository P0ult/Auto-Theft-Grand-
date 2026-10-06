#include "Game/ATGHumanMesh.h"
#include "AutoTheftGrand.h"
#include "Game/ATGCoords.h"
#include "Sim/Humanoid.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace {
// humanoidMaterial's roughness per material id (cloth, skin, hair, leather, eye, denim, metal); the vertex-lit
// material takes it from the fourth uv channel
const float GRough[7] = { 0.9f, 0.52f, 0.55f, 0.38f, 0.06f, 0.93f, 0.3f };

FVector3f AnyTangent(const FVector3f& N) {
	const FVector3f Ref = FMath::Abs(N.Z) < 0.9f ? FVector3f(0, 0, 1) : FVector3f(1, 0, 0);
	return FVector3f::CrossProduct(Ref, N).GetSafeNormal();
}
}

FString ATGHuman::Key(const atg::Appearance& A) {
	return FString::Printf(TEXT("%d_%06x_%06x_%hs_%06x_%hs_%06x_%d_%06x_%lld_%.4f_%d_%d_%06x_%lld_%d_%06x_%06x_%lld_%lld_%d_%hs_%lld_%lld_%lld"),
		A.female ? 1 : 0, A.skin, A.hair, A.hairStyle.c_str(), A.shirt, A.shirtType.c_str(), A.pants, A.shorts ? 1 : 0, A.shoes, (long long)A.hat,
		A.build, A.glasses ? 1 : 0, A.beard ? 1 : 0, A.jacketColor, (long long)A.bandana, A.hasUniform ? 1 : 0, A.uniformShirt, A.uniformPants,
		(long long)A.uniformHat, (long long)A.uniformBand, A.noBadge ? 1 : 0, A.vest.c_str(), (long long)A.vestColor, (long long)A.mask, (long long)A.hardhat);
}

USkeletalMesh* ATGHuman::Build(UObject* Outer, const atg::Appearance& A, USkeleton*& Skeleton, UMaterialInterface* Material) {
	using namespace atg;
	const HumanoidMesh H = BuildHumanoidGeometry(A);
	const int32 NV = (int32)H.Count();
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Outer, NAME_None, RF_Transient);

	// ---- the bones, in the rest pose (each relative to its parent; no rotation)
	FReferenceSkeleton RefSkel;
	{
		FReferenceSkeletonModifier Mod(RefSkel, nullptr);
		for (int32 I = 0; I < Bone::COUNT; I++) {
			const FName Name(BONE_NAMES[I]);
			Mod.Add(FMeshBoneInfo(Name, Name.ToString(), BONE_PARENT[I]), FTransform(ATG::LocalToUE(H.rest[I])));
		}
	}
	Mesh->SetRefSkeleton(RefSkel);
	Mesh->CalculateInvRefMatrices();
	if (!Skeleton) {
		Skeleton = NewObject<USkeleton>(GetTransientPackage(), TEXT("ATG_HumanSkeleton"), RF_Transient);
		Skeleton->MergeAllBonesToBoneTree(Mesh, false);
	}
	Mesh->SetSkeleton(Skeleton);

	// ---- one LOD, one section
	FSkeletalMeshLODInfo& Info = Mesh->AddLODInfo();
	Info.ScreenSize = 1.f;
	Info.LODHysteresis = 0.02f;
	Info.BuildSettings.bUseFullPrecisionUVs = true;
	Mesh->AllocateResourceForRendering();
	FSkeletalMeshRenderData* RD = Mesh->GetResourceForRendering();
	FSkeletalMeshLODRenderData& LOD = *new FSkeletalMeshLODRenderData;
	RD->LODRenderData.Add(&LOD);

	FBox Box(ForceInit);
	LOD.StaticVertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
	LOD.StaticVertexBuffers.PositionVertexBuffer.Init(NV, false);
	LOD.StaticVertexBuffers.StaticMeshVertexBuffer.Init(NV, 4, false);
	TArray<FSkinWeightInfo> Weights;
	Weights.SetNumZeroed(NV);
	for (int32 I = 0; I < NV; I++) {
		const FVector P = ATG::LocalToUE(H.P[I * 3], H.P[I * 3 + 1], H.P[I * 3 + 2]);
		Box += P;
		LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I) = FVector3f(P);
		const FVector3f N = FVector3f(ATG::LocalDirToUE(H.N[I * 3], H.N[I * 3 + 1], H.N[I * 3 + 2])).GetSafeNormal();
		const FVector3f Tx = AnyTangent(N);
		LOD.StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(I, Tx, FVector3f::CrossProduct(N, Tx), N);
		const int32 Mat = FMath::Clamp((int32)FMath::RoundToInt(H.mat[I]), 0, 6);
		LOD.StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(I, 0, FVector2f(0, 0));
		LOD.StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(I, 1, FVector2f(H.C[I * 3], H.C[I * 3 + 1]));
		LOD.StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(I, 2, FVector2f(H.C[I * 3 + 2], 0));
		LOD.StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(I, 3, FVector2f(0, GRough[Mat]));
		// weights as 16-bit fractions that add up to exactly 65535
		FSkinWeightInfo& W = Weights[I];
		int32 Total = 0, Heaviest = 0;
		for (int32 K = 0; K < 4; K++) {
			W.InfluenceBones[K] = (FBoneIndexType)H.si[I * 4 + K];
			W.InfluenceWeights[K] = (uint16)FMath::RoundToInt(FMath::Clamp(H.sw[I * 4 + K], 0.f, 1.f) * 65535.f);
			Total += W.InfluenceWeights[K];
			if (W.InfluenceWeights[K] > W.InfluenceWeights[Heaviest]) Heaviest = K;
		}
		W.InfluenceWeights[Heaviest] = (uint16)FMath::Clamp(W.InfluenceWeights[Heaviest] + (65535 - Total), 0, 65535);
	}
	LOD.SkinWeightVertexBuffer.SetMaxBoneInfluences(4);
	LOD.SkinWeightVertexBuffer.SetUse16BitBoneIndex(false);
	LOD.SkinWeightVertexBuffer.SetUse16BitBoneWeight(true);
	LOD.SkinWeightVertexBuffer.SetNeedsCPUAccess(false);
	LOD.SkinWeightVertexBuffer = Weights;

	TArray<uint32> Indices;
	Indices.Reserve((int32)H.I.size());
	for (uint32 Ix : H.I) Indices.Add(Ix);
	LOD.MultiSizeIndexContainer.RebuildIndexBuffer(NV < (int32)MAX_uint16 ? sizeof(uint16) : sizeof(uint32), Indices);

	FSkelMeshRenderSection& S = LOD.RenderSections.AddDefaulted_GetRef();
	S.MaterialIndex = 0;
	S.BaseIndex = 0;
	S.NumTriangles = (uint32)(Indices.Num() / 3);
	S.BaseVertexIndex = 0;
	S.NumVertices = (uint32)NV;
	S.MaxBoneInfluences = 4;
	S.bCastShadow = true;
	S.bVisibleInRayTracing = true;
	for (int32 I = 0; I < Bone::COUNT; I++) { S.BoneMap.Add((FBoneIndexType)I); LOD.ActiveBoneIndices.Add((FBoneIndexType)I); LOD.RequiredBones.Add((FBoneIndexType)I); }
	S.DuplicatedVerticesBuffer.DupVertData.ResizeBuffer(1);
	S.DuplicatedVerticesBuffer.DupVertIndexData.ResizeBuffer(S.NumVertices);
	FMemory::Memzero(S.DuplicatedVerticesBuffer.DupVertIndexData.GetDataPointer(), S.NumVertices * sizeof(FIndexLengthPair));
	FMemory::Memzero(S.DuplicatedVerticesBuffer.DupVertData.GetDataPointer(), sizeof(uint32));
	RD->InitUnifiedBoneMapFromLODs();

	// (posed limbs reach past the rest pose: raised arms, a crouch, a ragdoll)
	Box = Box.ExpandBy(60.0);
	Mesh->SetImportedBounds(FBoxSphereBounds(Box));
	Mesh->SetHasVertexColors(false);
	TArray<FSkeletalMaterial> Mats;
	Mats.Add(FSkeletalMaterial(Material));
	Mesh->SetMaterials(Mats);
	Mesh->NeverStream = true;
	Mesh->InitResources();
	return Mesh;
}
