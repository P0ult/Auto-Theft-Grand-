// Draws the pickups and markers (pickups.js): the spinning, bobbing pickup models over their glow discs, and
// the markers' glowing cylinders with their bobbing arrows. The state is in Sim/Pickups.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ATGPickups.generated.h"

class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
namespace atg { class Game; }

USTRUCT()
struct FATGPickupView {
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Glow;
	FString Key;
	bool bSeen = false;
};

USTRUCT()
struct FATGMarkerView {
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Cyl;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Arrow;
	bool bSeen = false;
};

UCLASS()
class AATGPickups : public AActor {
	GENERATED_BODY()
public:
	AATGPickups();
	void Sync(atg::Game* G);

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	// (keyed by the simulation's objects; rebuilt when one goes and another takes its address)
	UPROPERTY(Transient) TMap<uint64, FATGPickupView> Pickups;
	UPROPERTY(Transient) TMap<uint64, FATGMarkerView> Markers;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UStaticMesh>> Meshes;
	UStaticMesh* PickupMesh(const FString& Kind, const FString& Weapon, float& Scale);
	UStaticMesh* GlowMesh(uint32 Color);
	UStaticMesh* ArrowMesh(uint32 Color);
	UStaticMeshComponent* NewMesh();
};
