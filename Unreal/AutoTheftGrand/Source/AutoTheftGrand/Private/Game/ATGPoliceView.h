// Draws what the police bring that isn't a car or a person: the helicopter (police.js Helicopter) with its
// spinning rotors and, at night, the searchlight's cone and spot light; and the roadblocks' spike strips
// (roadblocks.js spikeMesh). The logic is in Sim/Police and Sim/Roadblocks.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ATGPoliceView.generated.h"

class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USpotLightComponent;
class UStaticMeshComponent;
namespace atg { class Game; }

UCLASS()
class AATGPoliceView : public AActor {
	GENERATED_BODY()
public:
	AATGPoliceView();
	void Sync(atg::Game* G);

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Rotor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> TailRotor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Cone;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpotLightComponent> Spot;
	// spike strips: one metre of stinger, stretched, per segment
	UPROPERTY(Transient) TObjectPtr<class UStaticMesh> SpikeMesh;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Spikes;
	void SyncSpikes(atg::Game* G);
	bool bBuilt = false;
	void Build();
};
