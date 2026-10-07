#include "Game/ATGMissionView.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshUtil.h"
#include "Gen/MeshBuf.h"
#include "Sim/Game.h"
#include "Sim/Missions.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

AATGMissionView::AATGMissionView() {
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root")); RootComponent = Root;
	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring")); Ring->SetupAttachment(Root);
	Ring->SetMobility(EComponentMobility::Movable);
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision); Ring->SetCastShadow(false); Ring->SetVisibility(false);
}
void AATGMissionView::Sync(atg::Game* G) {
	if (!G || !G->missions || !G->missions->active || !G->missions->active->currentRing) { Ring->SetVisibility(false); return; }
	const auto& R = *G->missions->active->currentRing;
	if (!Material) {
		Material = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Glass), this);
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor::Black);
		Material->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(2.6f, 1.9f, 0.35f));
		Material->SetVectorParameterValue(TEXT("Surface"), FLinearColor(1.f, 0.f, 0.85f, 0.f));
	}
	const int32 Key = FMath::RoundToInt(R.radius * 100);
	TObjectPtr<UStaticMesh>& Mesh = Meshes.FindOrAdd(Key);
	if (!Mesh) { atg::MeshBuf B = atg::Geo::Torus(R.radius, 0.9, 8, 40); Mesh = ATGMesh::BuildStaticMesh(this, *FString::Printf(TEXT("AirRing_%d"), Key), TArray<FATGPart>{ { &B, Material } }); }
	Ring->SetStaticMesh(Mesh); Ring->SetVisibility(true);
	SetActorTransform(ATG::WorldToUE(atg::M4::Compose(R.c, atg::Quat::FromEuler(0, R.yaw, 0))));
}
