#include "Game/ATGAnimals.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshUtil.h"
#include "Sim/Animal.h"
#include "Sim/Game.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

AATGAnimals::AATGAnimals() {
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

UStaticMesh* AATGAnimals::Mesh(const FString& Key, const atg::MeshBuf& Buffer) {
	if (const auto* Found = Meshes.Find(Key)) return *Found;
	UStaticMesh* M = ATGMesh::BuildStaticMesh(this, *Key, TArray<FATGPart>{ { &Buffer, Material } }, EATGAxes::Local);
	Meshes.Add(Key, M); return M;
}

void AATGAnimals::Build(FATGAnimalView& View, const atg::Animal& A) {
	if (!Material) {
		Material = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Standard), this);
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
		Material->SetVectorParameterValue(TEXT("Surface"), FLinearColor(0.92f, 0.f, 1.f, 0.f));
	}
	const FString Breed = UTF8_TO_TCHAR(A.breed.c_str());
	auto Add = [&](const TCHAR* Part, const atg::MeshBuf& B) {
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Root);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(!A.sp.bird && A.sp.h * A.scale > 0.3);
		C->SetStaticMesh(Mesh(Breed + TEXT("_") + Part, B));
		C->RegisterComponent(); View.Parts.Add(C);
	};
	Add(TEXT("body"), A.P.body);
	if (A.sp.bird) { Add(TEXT("wing"), A.P.wing); Add(TEXT("wing"), A.P.wing); }
	else {
		Add(TEXT("head"), A.P.head); Add(TEXT("tail"), A.P.tail);
		Add(TEXT("front"), A.P.frontLeg); Add(TEXT("front"), A.P.frontLeg);
		Add(TEXT("hind"), A.P.hindLeg); Add(TEXT("hind"), A.P.hindLeg);
	}
}

void AATGAnimals::Pose(FATGAnimalView& View, const atg::Animal& A) {
	using atg::M4; using atg::Quat; using atg::V3;
	const auto& P = A.P;
	const M4 R = A.rootMatrix(), B = R * M4::Compose(V3(0, A.bodyY, 0), Quat::FromEuler(A.bodyRotX, 0, 0));
	auto Set = [&](int32 I, const M4& T) { View.Parts[I]->SetWorldTransform(ATG::ToUE(T)); View.Parts[I]->SetVisibility(A.visible && !A.removed); };
	Set(0, B);
	if (A.sp.bird) {
		for (int32 I = 0; I < 2; I++) {
			const double S = I == 0 ? -1 : 1;
			const M4 W = B * M4::Compose(V3(S * P.wingX, P.wingY, 0), Quat::FromEuler(0, A.wingRotY[I], A.wingRotZ[I]));
			Set(I + 1, W * M4::Compose(V3(), Quat::FromEuler(0, I == 0 ? atg::kPi : 0, 0)));
		}
	} else {
		Set(1, B * M4::Compose(V3(P.headPos[0], P.headPos[1], P.headPos[2]), Quat::FromEuler(A.headRotX, A.headRotY, 0)));
		Set(2, B * M4::Compose(V3(P.tailPos[0], P.tailPos[1], P.tailPos[2]), Quat::FromEuler(0, A.tailRotY, 0)));
		for (int32 I = 0; I < 4; I++) Set(I + 3, R * M4::Compose(V3((I % 2 ? -1 : 1) * P.legX, A.legY[I], I < 2 ? P.legZf : P.legZh), Quat::FromEuler(A.legRotX[I], 0, 0)));
	}
}

void AATGAnimals::Sync(atg::Game* G) {
	if (!G || !G->wildlife) return;
	for (auto& KV : Animals) KV.Value.bSeen = false;
	for (atg::Animal* A : G->wildlife->all()) {
		if (A->removed) continue;
		FATGAnimalView& V = Animals.FindOrAdd(A->id);
		V.bSeen = true;
		if (V.Parts.IsEmpty()) Build(V, *A);
		Pose(V, *A);
	}
	for (auto It = Animals.CreateIterator(); It; ++It) if (!It.Value().bSeen) {
		for (UStaticMeshComponent* C : It.Value().Parts) C->DestroyComponent();
		It.RemoveCurrent();
	}
}
