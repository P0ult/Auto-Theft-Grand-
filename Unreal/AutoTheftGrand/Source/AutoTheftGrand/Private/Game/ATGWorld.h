// The whole generated world as one actor: it runs the generator on a worker thread, then builds the
// meshes over a few frames (terrain, roads, city ground, buildings, water, props and trees as instances,
// collision), streams the fine terrain (with collision) round the player, and runs the sky and the clock.
// The generator's data stays available for gameplay (ground height, zone names, parking spots, ...).
#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include "Async/Future.h"
#include "GameFramework/Actor.h"
#include "ATGWorld.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UPointLightComponent;
class UPostProcessComponent;
class UProceduralMeshComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMesh;
class UTexture2D;
struct FATGWorldData;
namespace atg { class CityMap; struct MeshBuf; }

USTRUCT()
struct FATGCarMeshes {
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UStaticMesh> Paint = nullptr;
	UPROPERTY() TObjectPtr<UStaticMesh> Trim = nullptr;
	UPROPERTY() TObjectPtr<UStaticMesh> Wheel = nullptr;
};

// street furniture, trunks and rocks as circles (what the vehicles hit; the breakable ones get smashed)
struct FATGCircle {
	double X = 0, Z = 0, R = 0, Y0 = 0, Top = 0;
	bool bBreakable = false, bBroken = false;
	int32 Mesh = -1, Instance = -1; // the instanced mesh it is drawn by (props only)
	int32 Chunk = -1;               // its collision chunk (props only)
};

struct FATGMapLabel { FString Name; double X = 0, Z = 0; bool bBig = false; };

UCLASS()
class AATGWorld : public AActor {
	GENERATED_BODY()
public:
	AATGWorld();
	virtual ~AATGWorld() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	static AATGWorld* Get(const UObject* WorldContext);

	// ---- loading
	bool IsReady() const { return bReady; }
	FString LoadingText() const;

	// ---- the generated data (valid once ready)
	const atg::CityMap* Map() const;
	double GroundHeight(double X, double Z) const;   // metres, game axes
	FString ZoneName(double X, double Z) const;
	void PlayerStart(double& X, double& Y, double& Z, double& Yaw) const;

	// ---- the clock (hours) and how fast it runs (game minutes per real second)
	UPROPERTY(EditAnywhere, Category = "ATG|Time") float Hours = 8.5f;
	UPROPERTY(EditAnywhere, Category = "ATG|Time") float TimeScale = 1.f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float SunLux = 10.f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float MoonLux = 0.3f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float MinExposureEV100 = 1.f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float MaxExposureEV100 = 14.f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float LampCandelas = 150.f;
	float Night = 0.f, StreetLights = 0.f;
	FString TimeString() const;

	// ---- streaming focus (the player, in Unreal space)
	void SetFocus(const FVector& Where) { Focus = Where; bHasFocus = true; }

	// ---- circles near a point (vehicles), and smashing one
	void QueryCircles(double X, double Z, double R, TArray<int32>& Out) const;
	FATGCircle& Circle(int32 I) { return Circles[I]; }
	void BreakProp(int32 CircleIndex);

	// ---- the map (radar and full-screen map)
	UPROPERTY(Transient) TObjectPtr<UTexture2D> MapWorldTex = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> MapCityTex = nullptr;
	FBox2D MapWorldRect, MapCityRect; // game metres: (x, z)
	TArray<FATGMapLabel> MapLabels;

	// ---- vehicle models (built on first use)
	const FATGCarMeshes& CarMeshes(const FString& Id);

private:
	// generation
	FATGWorldData* Data = nullptr;
	TFuture<FATGWorldData*> Job;
	std::atomic<int32> LoadStage{ 0 };
	TArray<TFunction<void()>> Steps;
	int32 StepIndex = 0;
	bool bReady = false;

	void QueueBuild();
	UProceduralMeshComponent* NewMeshComponent(const TCHAR* Name, bool bCollision);
	UHierarchicalInstancedStaticMeshComponent* NewInstances(const TCHAR* Name, UStaticMesh* Mesh, int32 CustomFloats);
	void BuildPropColliderChunk(int32 Chunk);

	UPROPERTY(Transient) TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> Meshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> PropMeshes;
	UPROPERTY(Transient) TMap<FString, FATGCarMeshes> CarCache;

	// props' collision chunks (rebuilt when something breaks)
	UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> PropColliders;
	TArray<TArray<int32>> PropColliderCircles;
	TSet<int32> DirtyPropChunks;
	double DirtyTimer = 0;

	// circles
	TArray<FATGCircle> Circles;
	TMap<int64, TArray<int32>> CircleCells;

	// terrain: coarse chunks everywhere, fine ones (with collision) near the player
	UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> TerrainFar;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UProceduralMeshComponent>> TerrainNear;
	TMap<int32, TFuture<atg::MeshBuf*>> TerrainJobs;
	double TerrainTimer = 0;
	FVector Focus = FVector::ZeroVector;
	bool bHasFocus = false;
	void UpdateTerrain(bool bBlocking);
	void AddNearChunk(int32 Key, atg::MeshBuf* Mesh);

	// sky and light
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDirectionalLightComponent> Moon;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> Lamps;
	TArray<FVector> LampPositions; // game metres (x, y, z of the lamp head)
	double LampTimer = 0;
	void UpdateSky(float Dt);
	void UpdateLamps(float Dt);
};
