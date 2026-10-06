#include "Game/ATGPerson.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGWorld.h"
#include "Sim/Character.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

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
}
