// Draws the simulation's effects (Sim/Effects, the port of effects.js): the three particle pools as camera-
// facing quads rebuilt every frame, the decals, skid marks and tracers, the three flash lights, and debris
// (knocked-over props and torn-off panels) as meshes.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ATGEffects.generated.h"

class UProceduralMeshComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
namespace atg { class Game; class Effects; }

UCLASS()
class AATGEffects : public AActor {
	GENERATED_BODY()
public:
	AATGEffects();
	void Sync(atg::Game* G);

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> Lights;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Debris;
	UPROPERTY(Transient) TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> PaintMats;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GlassMat;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TrimMat;
	int32 DecalVersion = -1, SkidVersion = -1;
	bool bInit = false;
	void Init();
};
