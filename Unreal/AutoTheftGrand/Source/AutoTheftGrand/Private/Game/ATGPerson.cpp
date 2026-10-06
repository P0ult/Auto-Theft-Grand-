#include "Game/ATGPerson.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGWorld.h"
#include "Gen/Models.h"
#include "Sim/Character.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

namespace {
// segmented part -> the bone it rides on
int32 BoneFor(const char* Part) {
	using namespace atg::Bone;
	const FString P(Part);
	if (P == TEXT("hips")) return hips;
	if (P == TEXT("torso")) return spine;
	if (P == TEXT("head")) return neck;
	if (P == TEXT("armL")) return lUpperArm;
	if (P == TEXT("armR")) return rUpperArm;
	if (P == TEXT("foreL")) return lForearm;
	if (P == TEXT("foreR")) return rForearm;
	if (P == TEXT("thighL")) return lThigh;
	if (P == TEXT("thighR")) return rThigh;
	if (P == TEXT("shinL")) return lShin;
	if (P == TEXT("shinR")) return rShin;
	return hips;
}
}

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
	const atg::Appearance& A = C->appearance;
	const uint32 Shirt = A.hasUniform ? A.uniformShirt : (A.shirtType == "jacket" ? A.jacketColor : A.shirt);
	const atg::HumanLook Look{ A.skin, Shirt, A.hasUniform ? A.uniformPants : A.pants, A.shoes, A.hair };
	const TArray<UStaticMesh*>& Meshes = W->HumanMeshes(Look);
	const std::vector<atg::HumanPart>& Layout = W->HumanLayout();
	UMaterialInterface* Lit = ATGMaterials::Get(EATGMat::VertexLit);
	for (int32 I = 0; I < Meshes.Num(); I++) {
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
		M->SetupAttachment(Root);
		M->SetStaticMesh(Meshes[I]);
		M->SetMaterial(0, Lit);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetUsingAbsoluteLocation(true); M->SetUsingAbsoluteRotation(true); M->SetUsingAbsoluteScale(true);
		M->RegisterComponent();
		Parts.Add(M);
		PartBones.Add(BoneFor(Layout[I].name));
	}
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
	SetActorLocation(ATG::ToUE(C->pos));
	const bool bShow = C->visible && !C->removed;
	for (int32 I = 0; I < Parts.Num(); I++) {
		Parts[I]->SetVisibility(bShow);
		if (bShow) Parts[I]->SetWorldTransform(ATG::ToUE(C->pose.world[PartBones[I]]));
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
