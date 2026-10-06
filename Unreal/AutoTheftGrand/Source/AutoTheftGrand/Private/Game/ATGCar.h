// How a simulated vehicle (atg::Vehicle) is drawn: the model's parts as meshes on the body (which tilts on its
// springs), the driver's door, bonnet and boot on their hinges, the wheels spinning and steering, lights,
// the police light bar, dents and torn-off panels. All the behaviour is in the simulation; this actor only
// copies its state every frame.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/Core.h"
#include "ATGCar.generated.h"

class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USpotLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class AATGWorld;
namespace atg { class Vehicle; class Train; struct VehicleModel; }

UCLASS()
class AATGCar : public AActor {
	GENERATED_BODY()
public:
	AATGCar();
	static AATGCar* Spawn(UWorld* World, atg::Vehicle* V);
	void Sync(float Dt);
	atg::Vehicle* Sim() const { return Vehicle.get(); }
	bool IsFor(const atg::Vehicle* V) const { return Vehicle.get() == V; }

private:
	atg::Ref<atg::Vehicle> Vehicle;
	const atg::VehicleModel* Model = nullptr;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Body;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> DoorPivot;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> HoodPivot;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> TrunkPivot;
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> RearPivots;
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> WheelPivots;
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> WheelSpins;
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> Parts; // (one per model part, same order)
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PaintMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HeadMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TailMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RedMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BlueMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BurntMat;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpotLightComponent> HeadLight;
	// trains: the locomotive's parts on the root, one scene component per carriage or wagon
	bool bTrain = false;
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> TrainCars;
	int32 HeadState = -1;
	void BuildTrain(atg::Train* T);
	void SyncTrain(atg::Train* T);
	int32 DentVersion = 0;
	uint32 PaintColor = 0; // (the colour of a respray)
	int32 Detached = 0;
	bool bGlassBroken = false, bBurnt = false, bLights = false;
	int32 TailState = -1;
	void Build(atg::Vehicle* V);
	void ApplyDents();
};
