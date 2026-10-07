// The mission engine's airborne checkpoint ring (story.js airRing).
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ATGMissionView.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
namespace atg { class Game; }

UCLASS()
class AATGMissionView : public AActor {
	GENERATED_BODY()
public:
	AATGMissionView();
	void Sync(atg::Game* G);
private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ring;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY() TMap<int32, TObjectPtr<UStaticMesh>> Meshes;
};
