// Draws the animals' procedural rigs. Behaviour and poses live in Sim/Animal and Sim/Wildlife.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ATGAnimals.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
namespace atg { class Game; class Animal; struct MeshBuf; }

USTRUCT()
struct FATGAnimalView {
	GENERATED_BODY()
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	bool bSeen = false;
};

UCLASS()
class AATGAnimals : public AActor {
	GENERATED_BODY()
public:
	AATGAnimals();
	void Sync(atg::Game* G);
private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY() TMap<FString, TObjectPtr<UStaticMesh>> Meshes;
	UPROPERTY() TMap<int32, FATGAnimalView> Animals;
	UStaticMesh* Mesh(const FString& Key, const atg::MeshBuf& Buffer);
	void Build(FATGAnimalView& View, const atg::Animal& A);
	void Pose(FATGAnimalView& View, const atg::Animal& A);
};
