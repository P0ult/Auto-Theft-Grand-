// The whole generated world as one actor: it runs the generator on a worker thread, then builds the
// meshes over a few frames (terrain, roads, city ground, buildings, water, props and trees as instances,
// collision), streams the fine terrain (with collision) round the player, and runs the sky and the clock.
// The generator's data stays available for gameplay (ground height, zone names, parking spots, ...).
#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include <vector>
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
class USkeletalMesh;
class USkeleton;
class UTexture2D;
struct FATGWorldData;
namespace atg { class CityMap; class Game; struct MeshBuf; struct VehicleDef; struct TrainModel; struct Appearance; }

USTRUCT()
struct FATGVehicleMeshes {
	GENERATED_BODY()
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> Parts; // (one per VehicleModel part)
	UPROPERTY() TObjectPtr<UStaticMesh> Wheel = nullptr;
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

	// ---- the simulation (the whole game; valid once ready)
	atg::Game* Game() const;

	// ---- the clock comes from the simulation (Hours mirrors it)
	float Hours = 8.5f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float SunLux = 10.f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float MoonLux = 0.3f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float MinExposureEV100 = 1.f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float MaxExposureEV100 = 14.f;
	UPROPERTY(EditAnywhere, Category = "ATG|Lighting") float LampCandelas = 150.f;
	float Night = 0.f, StreetLights = 0.f;
	FString TimeString() const;

	// ---- streaming focus (the player, in Unreal space)
	void SetFocus(const FVector& Where) { Focus = Where; bHasFocus = true; }


	// ---- the map (radar and full-screen map)
	UPROPERTY(Transient) TObjectPtr<UTexture2D> MapWorldTex = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> MapCityTex = nullptr;
	FBox2D MapWorldRect, MapCityRect; // game metres: (x, z)
	TArray<FATGMapLabel> MapLabels;

	// ---- models (built on first use)
	// the WASTED / BUSTED look (postfx.js uDesat, uDeath, uDeathBoost): desaturated, darker, a heavy vignette
	void SetDeathLook(double Desat, double Death, double Boost);
	// postfx.js uChroma (0.0022 normally, more with the special ability) as the scene's colour fringe
	void SetFringe(double Chroma);
	// a prop's mesh (built in the world's axes), for its debris when it is knocked over
	UStaticMesh* PropMesh(int32 PropIndex) const;
	const FATGVehicleMeshes& VehicleMeshes(const atg::VehicleDef& Def);
	// the trains' rolling stock (one entry per TrainModel)
	const FATGVehicleMeshes& TrainMeshes(const atg::TrainModel& Model);
	// a two-wheeler's: body, trim, fork, crank, glass, head, tail (empty ones null), and the wheel
	const FATGVehicleMeshes& BikeMeshes(const atg::VehicleDef& Def);
	// a mesh modelled facing +z, built once and kept under Key (aircraft parts); null when G is empty
	UStaticMesh* LocalMesh(const FString& Key, const atg::MeshBuf& G);
	// a person's skinned mesh (shared by people who look the same, while any of them is alive)
	USkeletalMesh* HumanMesh(const atg::Appearance& A);
	UStaticMesh* WeaponMesh(const FString& Id);

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

	UPROPERTY(Transient) TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> Meshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> PropMeshes;
	UPROPERTY(Transient) TMap<FString, FATGVehicleMeshes> VehicleCache;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UStaticMesh>> LocalMeshCache;
	TMap<FString, TWeakObjectPtr<USkeletalMesh>> HumanCache;
	UPROPERTY(Transient) TObjectPtr<USkeleton> HumanSkeleton;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UStaticMesh>> WeaponCache;

	// props: which instanced mesh and instance draws each (to hide smashed ones)
	TArray<TPair<int32, int32>> PropInstances;
	int32 PropVersion = 0;
	void SyncBrokenProps();

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
