// How a simulated person (atg::Character) is drawn: body parts placed on the pose's bones every frame, and
// the weapon in the right hand. (A stand-in for the skinned humanoid of humanoid.js, which comes with the
// living-city phase: the parts are the phase 1 segmented body, coloured from the person's appearance.)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/Core.h"
#include "ATGPerson.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
namespace atg { class Character; }

UCLASS()
class AATGPerson : public AActor {
	GENERATED_BODY()
public:
	AATGPerson();
	static AATGPerson* Spawn(UWorld* World, atg::Character* C);
	void Sync(float Dt);
	bool IsFor(const atg::Character* C) const { return Person.get() == C; }

private:
	atg::Ref<atg::Character> Person;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	TArray<int32> PartBones;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Weapon;
	FString WeaponId;
	void Build(atg::Character* C);
};
