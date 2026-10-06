// Draws the police helicopter (police.js Helicopter): the body, the spinning main and tail rotors, and at
// night the searchlight's cone and its spot light on the target. The flying is in Sim/Police.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ATGPoliceHeli.generated.h"

class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USpotLightComponent;
class UStaticMeshComponent;
namespace atg { class Game; }

UCLASS()
class AATGPoliceHeli : public AActor {
	GENERATED_BODY()
public:
	AATGPoliceHeli();
	void Sync(atg::Game* G);

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Rotor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> TailRotor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Cone;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpotLightComponent> Spot;
	bool bBuilt = false;
	void Build();
};
