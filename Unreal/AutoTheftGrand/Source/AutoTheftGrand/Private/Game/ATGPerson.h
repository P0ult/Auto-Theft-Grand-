// How a simulated person (atg::Character) is drawn: the skinned humanoid of humanoid.js (ATGHumanMesh) on a
// poseable mesh whose 17 bones copy the simulation's pose every frame (the animator's gait, actions and
// ragdoll), and the weapon in the right hand.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/Core.h"
#include "ATGPerson.generated.h"

class UStaticMeshComponent;
class UPoseableMeshComponent;
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
	UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> Body;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Weapon;
	FString WeaponId;
	void Build(atg::Character* C);
};
