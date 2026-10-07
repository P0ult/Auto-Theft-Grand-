#include "Game/ATGPerson.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshUtil.h"
#include "Game/ATGWorld.h"
#include "Sim/Character.h"
#include "Sim/Game.h"
#include "Sim/Peds.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

AATGPerson::AATGPerson() {
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

AATGPerson* AATGPerson::Spawn(UWorld* W, atg::Character* C) {
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AATGPerson* A = W->SpawnActor<AATGPerson>(AATGPerson::StaticClass(), ATG::ToUE(C->pos), FRotator::ZeroRotator, P);
	if (A) A->Build(C);
	return A;
}

void AATGPerson::Build(atg::Character* C) {
	Person = atg::Ref<atg::Character>(C);
	AATGWorld* W = AATGWorld::Get(this);
	if (!W) return;
	Body = NewObject<UPoseableMeshComponent>(this);
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetSkinnedAssetAndUpdate(W->HumanMesh(C->appearance));
	Body->RegisterComponent();
	UMaterialInterface* Lit = ATGMaterials::Get(EATGMat::VertexLit);
	Weapon = NewObject<UStaticMeshComponent>(this);
	Weapon->SetupAttachment(Root);
	Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Weapon->SetUsingAbsoluteLocation(true); Weapon->SetUsingAbsoluteRotation(true); Weapon->SetUsingAbsoluteScale(true);
	Weapon->SetMaterial(0, Lit);
	Weapon->RegisterComponent();
	Sync(0);
}

void AATGPerson::Sync(float Dt) {
	atg::Character* C = Person.get();
	if (!C) return;
	const bool bShow = C->visible && !C->removed;
	Body->SetVisibility(bShow);
	if (bShow) {
		// the root group (position, heading, height scale; a seat when in a vehicle), then each bone on its parent
		SetActorTransform(ATG::ToUE(C->rootMatrix()));
		TArray<FTransform>& Bones = Body->BoneSpaceTransforms;
		if (Bones.Num() == atg::Bone::COUNT) {
			for (int32 I = 0; I < atg::Bone::COUNT; I++) Bones[I] = ATG::LocalToUE(atg::M4::Compose(C->pose.pos[I], C->pose.rot[I]));
			Body->RefreshBoneTransforms();
		}
	}
	const FString Want = C->hasWeaponModel() && C->weaponVisible ? FString(UTF8_TO_TCHAR(C->weapon.c_str())) : FString();
	if (Want != WeaponId) {
		WeaponId = Want;
		AATGWorld* W = AATGWorld::Get(this);
		Weapon->SetStaticMesh(Want.IsEmpty() || !W ? nullptr : W->WeaponMesh(Want));
	}
	Weapon->SetVisibility(bShow && !WeaponId.IsEmpty());
	if (bShow && !WeaponId.IsEmpty()) Weapon->SetWorldTransform(ATG::ToUE(C->weaponMatrix()));
	if (atg::Ped* P = dynamic_cast<atg::Ped*>(C)) {
		if (P->targetArrow && !TargetArrow) {
			atg::MeshBuf B; B.Color(1, 1, 1); B.Add(atg::Geo::Cone(0.2, 0.42, 4), atg::Mat4::Compose(0, 0, 0, atg::kPi, 0, 0));
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Unlit), this);
			Mat->SetVectorParameterValue(TEXT("Color"), FLinearColor(2.4f, 0.18f, 0.12f));
			TargetArrow = NewObject<UStaticMeshComponent>(this); TargetArrow->SetupAttachment(Root);
			TargetArrow->SetCollisionEnabled(ECollisionEnabled::NoCollision); TargetArrow->SetCastShadow(false);
			TargetArrow->SetUsingAbsoluteLocation(true); TargetArrow->SetUsingAbsoluteRotation(true); TargetArrow->SetUsingAbsoluteScale(true);
			TargetArrow->SetStaticMesh(ATGMesh::BuildStaticMesh(this, TEXT("MissionTargetArrow"), TArray<FATGPart>{ { &B, Mat } }, EATGAxes::Local)); TargetArrow->RegisterComponent();
		}
		if (TargetArrow) {
			TargetArrow->SetVisibility(bShow && P->targetArrow && !P->dead && !P->ragdolling && !P->vehicle);
			const double T = P->game.time;
			TargetArrow->SetWorldTransform(ATG::ToUE(atg::M4::Compose(P->pos + atg::V3(0, 2.3 + std::sin(T * 4 + P->targetArrowPhase) * 0.08, 0), atg::Quat::FromEuler(0, P->yaw + T * 2 + P->targetArrowPhase, 0))));
		}
	}
}
