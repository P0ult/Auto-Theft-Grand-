#include "Game/ATGWorld.h"
#include "Game/ATGMeshComponent.h"
#include "AutoTheftGrand.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshUtil.h"
#include "Game/ATGPictures.h"
#include "Gen/BikeModels.h"
#include "Gen/CitySigns.h"
#include "Gen/Landmarks.h"
#include "Gen/InteriorMesh.h"
#include "Gen/MapImage.h"
#include "Game/ATGHumanMesh.h"
#include "Gen/TrainModels.h"
#include "Gen/VehicleModels.h"
#include "Gen/WeaponModels.h"
#include "Gen/WorldMeshes.h"
#include "Sim/Boats.h"
#include "Sim/ShipRaid.h"
#include "Sim/Skateparks.h"
#include "Sim/Game.h"
#include "Sim/Setup.h"

#include "Async/Async.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "ProceduralMeshComponent.h"

// Everything the worker thread makes: the generated map and the (plain C++) geometry
struct FATGWorldData {
	atg::CityMap Map;
	atg::RoadMeshes Roads;
	atg::MeshBuf Street, Blocks, Lots, Pads, Pools, Lake, Container, Sleeper;
	std::map<std::pair<int, int>, atg::CityChunk> City;
	std::map<std::string, atg::PropTemplate> PropDefs;
	std::vector<atg::PropInstance> Props;
	std::map<std::string, atg::VegTemplate> VegDefs;
	struct FTerrain { int I, J; atg::MeshBuf Mesh; };
	std::vector<FTerrain> Terrain;
	std::vector<atg::MeshBuf> Sea;
	atg::MapImages MapImg;
	std::unique_ptr<atg::Game> Sim; // the game itself (collision, player, vehicles, systems)
};

namespace {
double SrgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
FLinearColor HexLinear(uint32 Hex) {
	return FLinearColor((float)SrgbToLinear(((Hex >> 16) & 255) / 255.0), (float)SrgbToLinear(((Hex >> 8) & 255) / 255.0), (float)SrgbToLinear((Hex & 255) / 255.0), 1.f);
}
double SmoothStep(double A, double B, double X) { const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0); return T * T * (3 - 2 * T); }
FString Str(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

// The sea: a grid (8 m) over the world wherever the ground is below sea level, carrying the depth for the
// shoreline foam (the browser game used a depth texture), plus the open ocean round the world's edges
std::vector<atg::MeshBuf> BuildSea(const atg::CityMap& Map) {
	using namespace atg;
	std::vector<MeshBuf> Out;
	const double CH = 256, ST = 8;
	const int N = (int)(CH / ST) + 1;
	const int NX = (int)std::ceil((WORLD.maxX - WORLD.minX) / CH), NZ = (int)std::ceil((WORLD.maxZ - WORLD.minZ) / CH);
	std::vector<double> D((size_t)N * N);
	for (int CJ = 0; CJ < NZ; CJ++) for (int CI = 0; CI < NX; CI++) {
		const double X0 = WORLD.minX + CI * CH, Z0 = WORLD.minZ + CJ * CH;
		for (int J = 0; J < N; J++) for (int I = 0; I < N; I++) D[(size_t)J * N + I] = WATER_Y - Map.hf.Sample(X0 + I * ST, Z0 + J * ST);
		MeshBuf G;
		std::vector<int64_t> Idx((size_t)N * N, -1);
		auto Vert = [&](int I, int J) {
			int64_t& K = Idx[(size_t)J * N + I];
			if (K < 0) K = G.V(X0 + I * ST, WATER_Y, Z0 + J * ST, 0, 1, 0, atg::Clamp(D[(size_t)J * N + I], -1.0, 10.0), 0);
			return (uint32_t)K;
		};
		for (int J = 0; J + 1 < N; J++) for (int I = 0; I + 1 < N; I++) {
			const double M = atg::Max(atg::Max(D[(size_t)J * N + I], D[(size_t)J * N + I + 1]), atg::Max(D[(size_t)(J + 1) * N + I], D[(size_t)(J + 1) * N + I + 1]));
			if (M < 0.05) continue;
			const uint32_t A = Vert(I, J), B = Vert(I + 1, J), C = Vert(I, J + 1), E = Vert(I + 1, J + 1);
			G.Tri(A, C, E); G.Tri(A, E, B);
		}
		if (!G.Empty()) Out.push_back(std::move(G));
	}
	// open ocean: four big frames round the world rectangle, 30 km out
	MeshBuf O;
	const double F = 30000;
	auto Quad = [&](double X0, double Z0, double X1, double Z1) {
		const uint32_t A = O.V(X0, WATER_Y, Z0, 0, 1, 0, 10, 0), B = O.V(X1, WATER_Y, Z0, 0, 1, 0, 10, 0);
		const uint32_t C = O.V(X0, WATER_Y, Z1, 0, 1, 0, 10, 0), E = O.V(X1, WATER_Y, Z1, 0, 1, 0, 10, 0);
		O.Tri(A, C, E); O.Tri(A, E, B);
	};
	Quad(WORLD.minX - F, WORLD.minZ - F, WORLD.maxX + F, WORLD.minZ);
	Quad(WORLD.minX - F, WORLD.maxZ, WORLD.maxX + F, WORLD.maxZ + F);
	Quad(WORLD.minX - F, WORLD.minZ, WORLD.minX, WORLD.maxZ);
	Quad(WORLD.maxX, WORLD.minZ, WORLD.maxX + F, WORLD.maxZ);
	Out.push_back(std::move(O));
	return Out;
}

// Lake Mirador: a disc at its own level (the pool colours, as in the browser game)
atg::MeshBuf BuildLake() {
	using namespace atg;
	MeshBuf G;
	const int N = 64;
	const double R = LAKE.r + 24;
	const uint32_t C = G.V(LAKE.x, LAKE.y, LAKE.z, 0, 1, 0, 2, 1);
	std::vector<uint32_t> Ring;
	for (int K = 0; K < N; K++) { const double A = (double)K / N * 2 * UE_DOUBLE_PI; Ring.push_back(G.V(LAKE.x + std::cos(A) * R, LAKE.y, LAKE.z + std::sin(A) * R, 0, 1, 0, 2, 1)); }
	for (int K = 0; K < N; K++) G.Tri(C, Ring[(K + 1) % N], Ring[K]);
	return G;
}

FATGWorldData* Generate(std::atomic<int32>& Stage) {
	using namespace atg;
	const double T0 = FPlatformTime::Seconds();
	Stage = 1;
	FATGWorldData* D = new FATGWorldData();
	const CityMap& M = D->Map;
	Stage = 2;
	D->Roads = BuildRoadMeshes(M);
	Stage = 3;
	D->Street = BuildStreetPlane();
	D->Blocks = BuildBlocks(M);
	D->Lots = BuildLots(M);
	D->Pads = BuildPads(M);
	D->City = BuildBuildings(M);
	D->Pools = BuildPools(M);
	for (size_t I = 0; I < D->Pools.Count(); I++) { D->Pools.C[0][I * 2] = 2; D->Pools.C[0][I * 2 + 1] = 1; }
	Stage = 4;
	D->PropDefs = BuildPropTemplates();
	D->Props = PlaceProps(M);
	D->VegDefs = BuildVegTemplates();
	D->Container = ContainerGeo();
	{ MeshBuf S; S.ColorHex(0x4d443c); S.Rough(0.95); S.Add(Geo::Box(2.6, 0.16, 0.26), Mat4::Identity()); D->Sleeper = std::move(S); }
	Stage = 5;
	for (int J = 0; J < TerrainChunksZ(); J++) for (int I = 0; I < TerrainChunksX(); I++) {
		if (TerrainChunkInCity(I, J)) continue;
		D->Terrain.push_back({ I, J, BuildTerrainChunk(M, I, J, 1) });
	}
	Stage = 6;
	D->Sea = BuildSea(M);
	D->Lake = BuildLake();
	Stage = 7;
	D->MapImg = BuildMapImages(M);
	{
		WorldData W;
		W.map = &D->Map; W.roadPrims = D->Roads.prims; W.roadDecks = D->Roads.decks; W.props = D->Props; W.propDefs = &D->PropDefs;
		D->Sim = std::make_unique<Game>(W);
		InstallSystems(*D->Sim);
	}
	UE_LOG(LogATG, Log, TEXT("World generated in %.1f s"), FPlatformTime::Seconds() - T0);
	Stage = 8;
	return D;
}

UTexture2D* MakeTexture(const atg::MapLayer& L) {
	UTexture2D* T = UTexture2D::CreateTransient(L.w, L.h, PF_R8G8B8A8);
	if (!T) return nullptr;
	T->SRGB = true;
	T->Filter = TF_Bilinear;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
	void* Dst = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dst, L.rgba.data(), L.rgba.size());
	Mip.BulkData.Unlock();
	T->UpdateResource();
	return T;
}
} // namespace

// ==================================================================== actor
AATGWorld::AATGWorld() {
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Static);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetAtmosphereSunLightIndex(0);
	Sun->SetIntensity(SunLux);
	Sun->SetCastShadows(true);
	// (the sun lights translucency, water and fog; at dusk the moon is up too and must not compete)
	Sun->ForwardShadingPriority = 1;

	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(Root);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetAtmosphereSunLight(true);
	Moon->SetAtmosphereSunLightIndex(1);
	Moon->SetIntensity(0.f);
	Moon->SetLightColor(FLinearColor(0.55f, 0.65f, 1.f));
	Moon->SetCastShadows(false);
	Moon->ForwardShadingPriority = 0;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = SLS_CapturedScene;

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Fog->SetFogDensity(0.012f);
	Fog->SetFogHeightFalloff(0.02f);

	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(Root);
	Post->bUnbound = true;
}

AATGWorld::~AATGWorld() { delete Data; }

AATGWorld* AATGWorld::Get(const UObject* WorldContext) {
	static TWeakObjectPtr<AATGWorld> Cached;
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	if (Cached.IsValid() && Cached->GetWorld() == W) return Cached.Get();
	for (TActorIterator<AATGWorld> It(W); It; ++It) { Cached = *It; return *It; }
	return nullptr;
}

void AATGWorld::BeginPlay() {
	Super::BeginPlay();
	FPostProcessSettings& S = Post->Settings;
	S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = MinExposureEV100;
	S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = MaxExposureEV100;
	S.bOverride_BloomIntensity = true; S.BloomIntensity = 0.6f;
	S.bOverride_MotionBlurAmount = true; S.MotionBlurAmount = 0.f; // (the browser game has none)
	// warm up the materials (in the editor they are generated here if missing)
	for (int32 I = 0; I < (int32)EATGMat::Count; I++) ATGMaterials::Get((EATGMat)I);
	for (int32 I = 0; I < 12; I++) {
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(Root);
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(0.f);
		L->SetAttenuationRadius(3400.f);
		L->SetLightColor(FLinearColor(1.f, 0.76f, 0.48f));
		L->SetCastShadows(false);
		L->SetVisibility(false);
		L->RegisterComponent();
		Lamps.Add(L);
	}
	// the shops' light (PointLight(0xfff0dc, 24, 20, 1.3); street lamps' 95 became LampCandelas)
	InteriorLight = NewObject<UPointLightComponent>(this);
	InteriorLight->SetupAttachment(Root);
	InteriorLight->SetMobility(EComponentMobility::Movable);
	InteriorLight->SetIntensityUnits(ELightUnits::Candelas);
	InteriorLight->SetIntensity(0.f);
	InteriorLight->SetAttenuationRadius(2000.f);
	// (three.js let it fall off as d^-1.3; a sphere light keeps the ceiling just above it from burning out the same way)
	InteriorLight->SetSourceRadius(60.f);
	InteriorLight->SetLightColor(HexLinear(0xfff0dc));
	InteriorLight->SetCastShadows(false);
	InteriorLight->SetVisibility(false);
	InteriorLight->RegisterComponent();
	UpdateSky(0.f);
	std::atomic<int32>* StagePtr = &LoadStage;
	Job = Async(EAsyncExecution::Thread, [StagePtr]() { return Generate(*StagePtr); });
}

void AATGWorld::EndPlay(const EEndPlayReason::Type Reason) {
	if (Job.IsValid()) { FATGWorldData* D = Job.Get(); if (!Data) Data = D; else delete D; }
	for (auto& J : TerrainJobs) delete J.Value.Get();
	TerrainJobs.Empty();
	Super::EndPlay(Reason);
}

FString AATGWorld::LoadingText() const {
	static const TCHAR* Names[] = { TEXT("Starting"), TEXT("Laying out the state"), TEXT("Paving the roads"), TEXT("Putting up the buildings"),
		TEXT("Placing the street furniture"), TEXT("Shaping the terrain"), TEXT("Filling the sea"), TEXT("Drawing the map"), TEXT("Building the world") };
	const int32 S = FMath::Clamp(LoadStage.load(), 0, 8);
	if (S == 8 && Steps.Num()) return FString::Printf(TEXT("%s  %d%%"), Names[8], FMath::RoundToInt(100.f * StepIndex / Steps.Num()));
	return FString(Names[S]);
}

float AATGWorld::LoadingProgress() const {
	const int32 S = FMath::Clamp(LoadStage.load(), 0, 8);
	const float Build = S == 8 && Steps.Num() ? (float)StepIndex / Steps.Num() : 0.f;
	return FMath::Clamp((S + Build) / 9.f, 0.f, 1.f);
}

const atg::CityMap* AATGWorld::Map() const { return Data ? &Data->Map : nullptr; }
double AATGWorld::GroundHeight(double X, double Z) const { return Data ? Data->Map.GroundHeight(X, Z) : 0.0; }
FString AATGWorld::ZoneName(double X, double Z) const { return Data ? Str(Data->Map.ZoneName(X, Z)) : FString(); }

void AATGWorld::PlayerStart(double& X, double& Y, double& Z, double& Yaw) const {
	X = 0; Z = 0; Yaw = 0;
	if (Data) {
		auto It = Data->Map.landmarks.find("home");
		if (It != Data->Map.landmarks.end()) { X = It->second.x; Z = It->second.z; }
	}
	Y = GroundHeight(X, Z);
}

FString AATGWorld::TimeString() const {
	const int32 H = FMath::FloorToInt(Hours), M = FMath::FloorToInt((Hours - H) * 60);
	return FString::Printf(TEXT("%02d:%02d"), H, M);
}

atg::Game* AATGWorld::Game() const { return Data && bReady ? Data->Sim.get() : nullptr; }

bool AATGWorld::RestartSimulation() {
	if (!Data || !bReady) return false;
	atg::WorldData W;
	W.map = &Data->Map; W.roadPrims = Data->Roads.prims; W.roadDecks = Data->Roads.decks; W.props = Data->Props; W.propDefs = &Data->PropDefs;
	Data->Sim.reset();
	Data->Sim = std::make_unique<atg::Game>(W);
	atg::InstallSystems(*Data->Sim);
	return true;
}

// ------------------------------------------------------------------ building the components
UProceduralMeshComponent* AATGWorld::NewMeshComponent(const TCHAR* Name, bool bCollision) {
	UProceduralMeshComponent* C = NewObject<UATGMeshComponent>(this, MakeUniqueObjectName(this, UATGMeshComponent::StaticClass(), FName(Name)));
	C->SetupAttachment(Root);
	C->SetMobility(EComponentMobility::Static);
	(void)bCollision; // (nothing collides in Unreal: the simulation has its own collision world)
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	return C;
}

UHierarchicalInstancedStaticMeshComponent* AATGWorld::NewInstances(const TCHAR* Name, UStaticMesh* Mesh, int32 CustomFloats) {
	UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UHierarchicalInstancedStaticMeshComponent::StaticClass(), FName(Name)));
	C->SetupAttachment(Root);
	C->SetMobility(EComponentMobility::Static);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetStaticMesh(Mesh);
	if (CustomFloats) C->SetNumCustomDataFloats(CustomFloats);
	C->RegisterComponent();
	return C;
}

void AATGWorld::QueueBuild() {
	using namespace atg;
	FATGWorldData* D = Data;
	UMaterialInterface* MTerrain = ATGMaterials::Get(EATGMat::Terrain);
	UMaterialInterface* MRoad = ATGMaterials::Get(EATGMat::Road);
	UMaterialInterface* MStreet = ATGMaterials::Get(EATGMat::Street);
	UMaterialInterface* MGround = ATGMaterials::Get(EATGMat::Ground);
	UMaterialInterface* MBuilding = ATGMaterials::Get(EATGMat::Building);
	UMaterialInterface* MLit = ATGMaterials::Get(EATGMat::VertexLit);
	UMaterialInterface* MFrond = ATGMaterials::Get(EATGMat::Frond);
	UMaterialInterface* MWater = ATGMaterials::Get(EATGMat::Water);

	// ---- coarse terrain (16 m), in batches
	TerrainFar.SetNum(TerrainChunksX() * TerrainChunksZ());
	for (size_t Start = 0; Start < D->Terrain.size(); Start += 48) {
		Steps.Add([this, D, Start, MTerrain]() {
			for (size_t K = Start; K < (std::min)(D->Terrain.size(), Start + 48); K++) {
				FATGWorldData::FTerrain& T = D->Terrain[K];
				UProceduralMeshComponent* C = NewMeshComponent(TEXT("Terrain"), false);
				ATGMesh::ToSection(C, 0, T.Mesh, false);
				C->SetMaterial(0, MTerrain);
				TerrainFar[T.I + T.J * TerrainChunksX()] = C;
				T.Mesh = MeshBuf(); // (the component keeps its own copy)
			}
		});
	}
	// ---- the city's ground (collision: it is the floor)
	Steps.Add([this, D, MStreet, MGround, MLit]() {
		UProceduralMeshComponent* S = NewMeshComponent(TEXT("Streets"), true);
		ATGMesh::ToSection(S, 0, D->Street, true);
		S->SetMaterial(0, MStreet);
		UProceduralMeshComponent* G = NewMeshComponent(TEXT("Ground"), true);
		ATGMesh::ToSection(G, 0, D->Blocks, true);
		ATGMesh::ToSection(G, 1, D->Lots, true);
		ATGMesh::ToSection(G, 2, D->Pads, true);
		for (int32 I = 0; I < 3; I++) G->SetMaterial(I, MGround);
	});
	// ---- the skateparks' ramps, ledges, walls and lights (skatepark.js: concrete and steel, vertex coloured), and
	// the pontoons
	Steps.Add([this, D]() {
		atg::Skateparks* SP = D->Sim ? dynamic_cast<atg::Skateparks*>(D->Sim->system("skateparks")) : nullptr;
		if (!SP) return;
		auto Mat = [&](double Rough, double Metal) {
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Standard), this);
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
			M->SetVectorParameterValue(TEXT("Surface"), FLinearColor((float)Rough, (float)Metal, 1.f, 0.f));
			return M;
		};
		UMaterialInstanceDynamic* Concrete = Mat(0.85, 0.05);
		UMaterialInstanceDynamic* Steel = Mat(0.3, 0.85);
		for (const atg::Skateparks::Park& P : SP->parks) {
			UProceduralMeshComponent* C = NewMeshComponent(TEXT("Skatepark"), false);
			ATGMesh::ToSection(C, 0, P.concrete, false);
			ATGMesh::ToSection(C, 1, P.metal, false);
			C->SetMaterial(0, Concrete);
			C->SetMaterial(1, Steel);
		}
		// the marinas' pontoons (boats.js: planks on floats, vertex coloured)
		if (atg::BoatSystem* BS = dynamic_cast<atg::BoatSystem*>(D->Sim->system("boats"))) {
			UMaterialInstanceDynamic* Wood = Mat(0.85, 0);
			for (const atg::MeshBuf& P : BS->pontoons) {
				UProceduralMeshComponent* C = NewMeshComponent(TEXT("Pontoon"), false);
				ATGMesh::ToSection(C, 0, P, false);
				C->SetMaterial(0, Wood);
			}
		}
	});
	// ---- the walk-in shops' rooms (interiors.js buildInteriorMesh): the lit surfaces, the glowing ones, the glass
	// cases, the posters and menu boards, and the weapons on display
	Steps.Add([this, D]() {
		if (D->Map.interiors.empty()) return;
		auto Mat = [&](EATGMat Which, const FLinearColor& Col, double Rough, double Metal, double Opacity) {
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(Which), this);
			M->SetVectorParameterValue(TEXT("Color"), Col);
			M->SetVectorParameterValue(TEXT("Surface"), FLinearColor((float)Rough, (float)Metal, (float)Opacity, 0.f));
			return M;
		};
		UMaterialInstanceDynamic* Solid = Mat(EATGMat::Standard, FLinearColor::White, 0.78, 0.02, 1);
		UMaterialInterface* Glow = ATGMaterials::Get(EATGMat::Unlit);
		UMaterialInstanceDynamic* GlassMat = Mat(EATGMat::Glass, HexLinear(0xbfe6ff), 0.05, 0.1, 0.22);
		for (const InteriorShell& It : D->Map.interiors) {
			const InteriorMesh R = BuildInteriorMesh(It);
			UProceduralMeshComponent* C = NewMeshComponent(TEXT("Interior"), false);
			C->SetCastShadow(false);
			ATGMesh::ToSection(C, 0, R.solid, false);
			C->SetMaterial(0, Solid);
			int32 Sec = 1;
			if (!R.glow.Empty()) { ATGMesh::ToSection(C, Sec, R.glow, false); C->SetMaterial(Sec++, Glow); }
			MeshBuf G;
			G.Color(1, 1, 1);
			for (const InteriorMesh::Glass& Gl : R.glass) G.Add(Geo::Box(Gl.sx, Gl.sy, Gl.sz), Mat4::Compose(Gl.x, Gl.y, Gl.z));
			if (!G.Empty()) { ATGMesh::ToSection(C, Sec, G, false); C->SetMaterial(Sec++, GlassMat); }
			// (three.js PlaneGeometry facing its local +z, turned by yaw; the canvas's top left at uv (0, 0))
			for (const InteriorMesh::Panel& P : R.panels) {
				UTexture* Tex = Picture(Str(P.picture));
				if (!Tex) continue;
				const double Rx = std::cos(P.yaw), Rz = -std::sin(P.yaw), Hw = P.width / 2, Hh = P.height / 2;
				const double P0[3] = { P.x - Rx * Hw, P.y - Hh, P.z - Rz * Hw }, P1[3] = { P.x + Rx * Hw, P.y - Hh, P.z + Rz * Hw };
				const double P2[3] = { P.x + Rx * Hw, P.y + Hh, P.z + Rz * Hw }, P3v[3] = { P.x - Rx * Hw, P.y + Hh, P.z - Rz * Hw };
				const double N[3] = { std::sin(P.yaw), 0, std::cos(P.yaw) };
				const double U0[2] = { 0, 1 }, U1[2] = { 1, 1 }, U2[2] = { 1, 0 }, U3[2] = { 0, 0 };
				MeshBuf Q;
				Q.Color(1, 1, 1);
				Q.Quad(P0, P1, P2, P3v, N, U0, U1, U2, U3);
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Picture), this);
				M->SetTextureParameterValue(TEXT("Picture"), Tex);
				M->SetVectorParameterValue(TEXT("Mode"), FLinearColor(P.lit ? 1.f : 0.f, P.lit ? 0.f : 1.f, 0, 0));
				ATGMesh::ToSection(C, Sec, Q, false);
				C->SetMaterial(Sec++, M);
			}
			for (const InteriorMesh::Weapon& Wp : R.weapons) {
				UStaticMesh* SM = WeaponMesh(Str(Wp.id));
				if (!SM) continue;
				UStaticMeshComponent* S = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("ShopWeapon")));
				S->SetupAttachment(Root);
				S->SetMobility(EComponentMobility::Static);
				S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				S->SetCastShadow(false);
				S->SetStaticMesh(SM);
				S->SetWorldTransform(ATG::ToUE(M4::Compose(V3(Wp.x, Wp.y, Wp.z), Quat::FromEuler(0, Wp.yaw, Wp.roll))));
				S->RegisterComponent();
			}
		}
	});
	// ---- the rooftop billboards and the shops' name signs (city.js _billboards, _signs): the faces and signs are
	// painted pictures, lit by the sun and glowing at night
	Steps.Add([this, D, MLit]() {
		atg::CityBillboards BB = atg::BuildBillboards(D->Map);
		if (!BB.frame.Empty()) {
			UProceduralMeshComponent* F = NewMeshComponent(TEXT("Billboards"), false);
			ATGMesh::ToSection(F, 0, BB.frame, false);
			F->SetMaterial(0, MLit);
			// (the atlas's v runs up in three.js; the render target's runs down)
			for (size_t I = 1; I < BB.faces.C[0].size(); I += 2) BB.faces.C[0][I] = 1.f - BB.faces.C[0][I];
			ATGMesh::ToSection(F, 1, BB.faces, false);
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Picture), this);
			if (UTexture* Ads = Picture(TEXT("ads"))) M->SetTextureParameterValue(TEXT("Picture"), Ads);
			M->SetVectorParameterValue(TEXT("Mode"), FLinearColor(1.f, 0.05f, 0.5f, 0.f));
			F->SetMaterial(1, M);
			BillboardMat = M;
		}
		// the chain-link panels: translucent steel, seen from both sides (std({ color: 0x9aa0a6, roughness 0.5,
		// metalness 0.6, opacity 0.35, DoubleSide }))
		atg::MeshBuf Chain = atg::BuildChainLink(D->Map);
		if (!Chain.Empty()) {
			Chain.Color(1, 1, 1);
			for (size_t I = 0; I < Chain.C[1].size(); I += 2) { Chain.C[1][I] = 1; Chain.C[1][I + 1] = 1; }
			for (size_t I = 0; I < Chain.C[2].size(); I += 2) Chain.C[2][I] = 1;
			const size_t Tris = Chain.I.size();
			for (size_t I = 0; I < Tris; I += 3) Chain.Tri(Chain.I[I], Chain.I[I + 2], Chain.I[I + 1]);
			UProceduralMeshComponent* Cl = NewMeshComponent(TEXT("ChainLink"), false);
			ATGMesh::ToSection(Cl, 0, Chain, false);
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Glass), this);
			M->SetVectorParameterValue(TEXT("Color"), HexLinear(0x9aa0a6));
			M->SetVectorParameterValue(TEXT("Surface"), FLinearColor(0.5f, 0.6f, 0.35f, 0.f));
			Cl->SetMaterial(0, M);
			Cl->SetCastShadow(false);
		}
		// one quad per sign (three.js PlaneGeometry facing its local +z, turned by yaw)
		TMap<FString, UMaterialInstanceDynamic*> ByText;
		UProceduralMeshComponent* S = NewMeshComponent(TEXT("Signs"), false);
		S->SetCastShadow(false);
		int32 Sec = 0;
		for (const atg::CitySign& Sg : atg::PlaceSigns(D->Map)) {
			const FString Text = Str(Sg.text);
			UMaterialInstanceDynamic** Found = ByText.Find(Text);
			UMaterialInstanceDynamic* M = Found ? *Found : nullptr;
			if (!M) {
				M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Picture), this);
				if (UTexture* T = Picture(TEXT("sign:") + Text)) M->SetTextureParameterValue(TEXT("Picture"), T);
				M->SetVectorParameterValue(TEXT("Mode"), FLinearColor(0.f, 0.55f, 0.f, 0.f));
				ByText.Add(Text, M);
				SignMats.Add(M);
			}
			const double Rx = std::cos(Sg.yaw), Rz = -std::sin(Sg.yaw), Hw = Sg.w / 2, Hh = Sg.h / 2;
			const double P0[3] = { Sg.x - Rx * Hw, Sg.y - Hh, Sg.z - Rz * Hw }, P1[3] = { Sg.x + Rx * Hw, Sg.y - Hh, Sg.z + Rz * Hw };
			const double P2[3] = { Sg.x + Rx * Hw, Sg.y + Hh, Sg.z + Rz * Hw }, P3v[3] = { Sg.x - Rx * Hw, Sg.y + Hh, Sg.z - Rz * Hw };
			const double N[3] = { std::sin(Sg.yaw), 0, std::cos(Sg.yaw) };
			const double U0[2] = { 0, 1 }, U1[2] = { 1, 1 }, U2[2] = { 1, 0 }, U3[2] = { 0, 0 };
			atg::MeshBuf Q; Q.Color(1, 1, 1);
			Q.Quad(P0, P1, P2, P3v, N, U0, U1, U2, U3);
			ATGMesh::ToSection(S, Sec, Q, false);
			S->SetMaterial(Sec++, M);
		}
	});
	// ---- the landmarks (landmarks.js): the Santa Luz pier and its Ferris wheel, the Vistawood sign, the dock cranes,
	// and the aviation beacons on tall roofs and cranes (city.js)
	Steps.Add([this, D, MLit]() {
		const atg::Landmarks L = atg::BuildLandmarks(D->Map);
		auto Std = [&](const FLinearColor& Col, double Rough, double Metal) {
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Standard), this);
			M->SetVectorParameterValue(TEXT("Color"), Col);
			M->SetVectorParameterValue(TEXT("Surface"), FLinearColor((float)Rough, (float)Metal, 1.f, 0.f));
			return M;
		};
		auto Spheres = [](const std::vector<atg::LmPoint>& At, double R, int W, int H) {
			atg::MeshBuf G; G.Color(1, 1, 1);
			const atg::MeshBuf S = atg::Geo::Sphere(R, W, H);
			for (const atg::LmPoint& P : At) G.Add(S, atg::Mat4::Compose(P.x, P.y, P.z));
			return G;
		};
		// the pier: the planked deck, the posts, rails and booths, the lamp poles and their globes
		UProceduralMeshComponent* Pier = NewMeshComponent(TEXT("Pier"), false);
		ATGMesh::ToSection(Pier, 0, L.pierDeck, false); Pier->SetMaterial(0, ATGMaterials::Get(EATGMat::Wood));
		ATGMesh::ToSection(Pier, 1, L.pierPosts, false); Pier->SetMaterial(1, MLit);
		ATGMesh::ToSection(Pier, 2, L.lampPoles, false); Pier->SetMaterial(2, MLit);
		LandmarkLampMat = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Unlit), this);
		ATGMesh::ToSection(Pier, 3, Spheres(L.lamps, 0.28, 10, 8), false); Pier->SetMaterial(3, LandmarkLampMat);
		// the Ferris wheel: the rim, spokes and neon turn about the hub; the gondolas hang level from the rim
		UMaterialInstanceDynamic* Steel = Std(HexLinear(0xf2f2f2), 0.4, 0.6);
		WheelHub = ATG::ToUE(L.hubX, L.hubY, L.hubZ);
		WheelR = L.wheelR;
		Wheel = NewMeshComponent(TEXT("FerrisWheel"), false);
		Wheel->SetMobility(EComponentMobility::Movable);
		Wheel->SetWorldLocation(WheelHub);
		ATGMesh::ToSection(Wheel, 0, L.wheel, false); Wheel->SetMaterial(0, Steel);
		NeonMat = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Unlit), this);
		ATGMesh::ToSection(Wheel, 1, L.neon, false); Wheel->SetMaterial(1, NeonMat);
		UProceduralMeshComponent* Sup = NewMeshComponent(TEXT("FerrisSupport"), false);
		ATGMesh::ToSection(Sup, 0, L.support, false); Sup->SetMaterial(0, Steel);
		UStaticMesh* GondolaMesh = ATGMesh::BuildStaticMesh(this, TEXT("Gondola"), TArray<FATGPart>{ { &L.gondola, ATGMaterials::Get(EATGMat::Standard) } });
		Meshes.Add(GondolaMesh);
		for (int32 K = 0; K < 16; K++) {
			UStaticMeshComponent* Gc = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("Gondola")));
			Gc->SetupAttachment(Root);
			Gc->SetMobility(EComponentMobility::Movable);
			Gc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Gc->SetStaticMesh(GondolaMesh);
			Gc->SetMaterial(0, Std(FLinearColor((float)L.gondolaColor[K][0], (float)L.gondolaColor[K][1], (float)L.gondolaColor[K][2]), 0.5, 0.2));
			Gc->RegisterComponent();
			Gondolas.Add(Gc);
		}
		// the Vistawood sign: the letters (cut out of their pictures, tilted back 0.12) and the scaffolding
		UProceduralMeshComponent* Sign = NewMeshComponent(TEXT("Vistawood"), false);
		ATGMesh::ToSection(Sign, 0, L.scaffold, false); Sign->SetMaterial(0, MLit);
		int32 Sec = 1;
		const double Th = -0.12, Ct = std::cos(Th), St = std::sin(Th);
		for (const atg::Landmarks::Letter& Lt : L.letters) {
			auto Pt = [&](double U, double V, double* Out) { Out[0] = Lt.x + U; Out[1] = Lt.y + V * Ct; Out[2] = Lt.z + V * St; };
			double A[3], B[3], C[3], E[3];
			Pt(-6.5, -7.5, A); Pt(6.5, -7.5, B); Pt(6.5, 7.5, C); Pt(-6.5, 7.5, E);
			const double N[3] = { 0, -St, Ct };
			const double U0[2] = { 0, 1 }, U1[2] = { 1, 1 }, U2[2] = { 1, 0 }, U3[2] = { 0, 0 };
			atg::MeshBuf Q; Q.Color(1, 1, 1);
			Q.Quad(A, B, C, E, N, U0, U1, U2, U3);
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Cutout), this);
			if (UTexture* T = Picture(FString::Printf(TEXT("letter:%c"), (TCHAR)Lt.ch))) M->SetTextureParameterValue(TEXT("Picture"), T);
			M->SetVectorParameterValue(TEXT("Color"), HexLinear(0xf4f4f0));
			M->SetVectorParameterValue(TEXT("Mode"), FLinearColor(1.f, 0.f, 0.6f, 0.f));
			ATGMesh::ToSection(Sign, Sec, Q, false);
			Sign->SetMaterial(Sec++, M);
			LetterMats.Add(M);
		}
		// the dock cranes
		UProceduralMeshComponent* Cr = NewMeshComponent(TEXT("Cranes"), false);
		ATGMesh::ToSection(Cr, 0, L.cranes, false); Cr->SetMaterial(0, Std(HexLinear(0xc0392b), 0.5, 0.4));
		// the aviation beacons (blinking red): the roofs' first, then the cranes'
		std::vector<atg::LmPoint> All = atg::BuildingBeacons(D->Map);
		All.insert(All.end(), L.beacons.begin(), L.beacons.end());
		BeaconMat = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Unlit), this);
		UProceduralMeshComponent* Bc = NewMeshComponent(TEXT("Beacons"), false);
		Bc->SetCastShadow(false);
		ATGMesh::ToSection(Bc, 0, Spheres(All, 0.35, 8, 6), false); Bc->SetMaterial(0, BeaconMat);
	});
	// ---- the rain (effects.js Rain): 6000 streaks, placed and animated by their material round the camera
	Steps.Add([this]() {
		FRandomStream Rng(5);
		atg::MeshBuf M;
		for (int32 I = 0; I < 6000; I++) {
			const double X = Rng.FRand() * 60, Y = Rng.FRand() * 40, Z = Rng.FRand() * 60, Seed = Rng.FRand();
			M.Set(1, Y, Seed);
			uint32_t Q[4];
			const double Corner[4][2] = { { 0, -1 }, { 0, 1 }, { 1, 1 }, { 1, -1 } };
			for (int32 K = 0; K < 4; K++) { M.Set(2, Corner[K][0], Corner[K][1]); Q[K] = M.V(X, Y + Corner[K][0] * 0.55, Z, 0, 1, 0, X, Z); }
			M.Tri(Q[0], Q[1], Q[2]); M.Tri(Q[0], Q[2], Q[3]);
		}
		RainComp = NewMeshComponent(TEXT("Rain"), false);
		ATGMesh::ToSection(RainComp, 0, M, false);
		RainComp->SetMaterial(0, ATGMaterials::Get(EATGMat::Rain));
		RainComp->SetCastShadow(false);
		RainComp->SetBoundsScale(5000.f); // (the streaks are moved to the camera in the material)
		RainComp->SetVisibility(false);
	});
	// ---- the MV Pacific Star at Port Morena (cargoship.js: one material per part; its lamps shine at night)
	Steps.Add([this, D]() {
		atg::ShipRaid* SR = D->Sim ? dynamic_cast<atg::ShipRaid*>(D->Sim->system("shipRaid")) : nullptr;
		if (!SR) return;
		auto Mat = [&](const FLinearColor& Col, double Rough, double Metal) {
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Standard), this);
			M->SetVectorParameterValue(TEXT("Color"), Col);
			M->SetVectorParameterValue(TEXT("Surface"), FLinearColor((float)Rough, (float)Metal, 1.f, 0.f));
			return M;
		};
		const atg::CargoShip& S = SR->ship;
		struct FShipPart { const atg::MeshBuf* Mesh; UMaterialInterface* Mat; bool bShadow; };
		ShipLampMat = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Unlit), this);
		const FShipPart Parts[] = {
			{ &S.hull, Mat(FLinearColor::White, 0.55, 0.35), true }, { &S.deck, Mat(FLinearColor::White, 0.85, 0.1), false },
			{ &S.steel, Mat(FLinearColor::White, 0.6, 0.4), true }, { &S.white, Mat(FLinearColor::White, 0.6, 0.1), true },
			{ &S.cont, Mat(FLinearColor::White, 0.75, 0.3), true }, { &S.glass, Mat(HexLinear(0x1a2733), 0.08, 0.6), false },
			{ &S.lamp, ShipLampMat, false },
		};
		for (const FShipPart& P : Parts) {
			if (P.Mesh->Empty()) continue;
			UProceduralMeshComponent* C = NewMeshComponent(TEXT("CargoShip"), false);
			ATGMesh::ToSection(C, 0, *P.Mesh, false);
			C->SetMaterial(0, P.Mat);
			C->SetCastShadow(P.bShadow);
		}
	});
	// ---- roads (surfaces and concrete collide; rails don't)
	std::vector<std::pair<int, int>> RoadKeys;
	for (const auto& R : D->Roads.chunks) RoadKeys.push_back(R.first);
	for (size_t Start = 0; Start < RoadKeys.size(); Start += 12) {
		Steps.Add([this, D, RoadKeys, Start, MRoad, MLit]() {
			for (size_t K = Start; K < (std::min)(RoadKeys.size(), Start + 12); K++) {
				RoadChunk& R = D->Roads.chunks[RoadKeys[K]];
				UProceduralMeshComponent* C = NewMeshComponent(TEXT("Roads"), true);
				int32 Sec = 0;
				if (!R.road.Empty()) { ATGMesh::ToSection(C, Sec, R.road, true); C->SetMaterial(Sec++, MRoad); }
				if (!R.conc.Empty()) { ATGMesh::ToSection(C, Sec, R.conc, true); C->SetMaterial(Sec++, MLit); }
				if (!R.rails.Empty()) { ATGMesh::ToSection(C, Sec, R.rails, false); C->SetMaterial(Sec++, MLit); }
				R = RoadChunk();
			}
		});
	}
	// ---- buildings (their collision is the box colliders below)
	std::vector<std::pair<int, int>> CityKeys;
	for (const auto& C : D->City) CityKeys.push_back(C.first);
	for (size_t Start = 0; Start < CityKeys.size(); Start += 16) {
		Steps.Add([this, D, CityKeys, Start, MBuilding, MLit]() {
			for (size_t K = Start; K < (std::min)(CityKeys.size(), Start + 16); K++) {
				CityChunk& Ch = D->City[CityKeys[K]];
				UProceduralMeshComponent* C = NewMeshComponent(TEXT("Buildings"), false);
				int32 Sec = 0;
				if (!Ch.bld.Empty()) { ATGMesh::ToSection(C, Sec, Ch.bld, false); C->SetMaterial(Sec++, MBuilding); }
				if (!Ch.det.Empty()) { ATGMesh::ToSection(C, Sec, Ch.det, false); C->SetMaterial(Sec++, MLit); }
				Ch = CityChunk();
			}
		});
	}
	// ---- water
	Steps.Add([this, D, MWater]() {
		for (const MeshBuf& W : D->Sea) {
			UProceduralMeshComponent* C = NewMeshComponent(TEXT("Sea"), false);
			ATGMesh::ToSection(C, 0, W, false);
			C->SetMaterial(0, MWater);
			C->SetCastShadow(false);
		}
		UProceduralMeshComponent* L = NewMeshComponent(TEXT("Lakes"), false);
		ATGMesh::ToSection(L, 0, D->Lake, false);
		L->SetMaterial(0, MWater);
		if (!D->Pools.Empty()) { ATGMesh::ToSection(L, 1, D->Pools, false); L->SetMaterial(1, MWater); }
		L->SetCastShadow(false);
		D->Sea.clear();
	});
	// ---- props: one instanced mesh per kind (their collision circles are in the simulation)
	Steps.Add([this, D, MLit, MFrond]() {
		TMap<FString, int32> MeshOf;
		TArray<TArray<FTransform>> Xf;
		TArray<TArray<float>> Phase;
		PropInstances.Init(TPair<int32, int32>(-1, -1), (int32)D->Props.size());
		for (int32 PropIdx = 0; PropIdx < (int32)D->Props.size(); PropIdx++) {
			const PropInstance& P = D->Props[PropIdx];
			auto Def = D->PropDefs.find(P.type);
			if (Def == D->PropDefs.end()) continue;
			const FString Type = Str(P.type);
			int32* M = MeshOf.Find(Type);
			if (!M) {
				TArray<FATGPart> Parts;
				Parts.Add({ &Def->second.mesh, MLit });
				if (!Def->second.leaves.Empty()) Parts.Add({ &Def->second.leaves, Def->second.frondMat ? MFrond : MLit });
				UStaticMesh* SM = ATGMesh::BuildStaticMesh(this, *(TEXT("Prop_") + Type), Parts);
				Meshes.Add(SM);
				UHierarchicalInstancedStaticMeshComponent* H = NewInstances(*(TEXT("Props_") + Type), SM, 4);
				const bool bTree = Type.StartsWith(TEXT("tree")) || Type.StartsWith(TEXT("palm"));
				H->SetCullDistances(bTree ? 120000 : 50000, bTree ? 150000 : 60000);
				M = &MeshOf.Add(Type, PropMeshes.Add(H));
				Xf.AddDefaulted(); Phase.AddDefaulted();
			}
			const int32 Inst = Xf[*M].Add(FTransform(FRotator(0, ATG::MeshYaw(P.rot), 0), ATG::ToUE(P.x, P.y, P.z), FVector(P.scale)));
			Phase[*M].Add((float)P.phase);
			PropInstances[PropIdx] = TPair<int32, int32>(*M, Inst);
		}
		for (int32 M = 0; M < PropMeshes.Num(); M++) {
			PropMeshes[M]->AddInstances(Xf[M], false, true);
			for (int32 I = 0; I < Xf[M].Num(); I++) {
				const float Cd[4] = { Phase[M][I], 1.f, 1.f, 1.f };
				PropMeshes[M]->SetCustomData(I, TArrayView<const float>(Cd, 4), false);
			}
			PropMeshes[M]->MarkRenderStateDirty();
		}
		// the lamp heads, for the few real lights that follow the player at night
		for (const PropInstance& P : D->Props) if (P.type == "streetlight") LampPositions.Add(FVector(P.x + std::sin(P.rot) * 3.1, P.y + 7.6, P.z + std::cos(P.rot) * 3.1));
	});
	// ---- vegetation: near and far models as two levels of detail
	Steps.Add([this, D, MLit, MFrond]() {
		const Vegetation& V = D->Map.vegetation;
		const std::pair<const char*, const std::vector<float>*> Lists[] = { { "pine", &V.pine }, { "oak", &V.oak }, { "bush", &V.bush }, { "cactus", &V.cactus },
			{ "rock", &V.rock }, { "deadtree", &V.deadtree }, { "palm", &V.palm } };
		for (const auto& L : Lists) {
			auto Def = D->VegDefs.find(L.first);
			if (Def == D->VegDefs.end() || L.second->empty()) continue;
			const VegTemplate& T = Def->second;
			TArray<TArray<FATGPart>> Lods;
			TArray<FATGPart>& L0 = Lods.AddDefaulted_GetRef();
			L0.Add({ &T.nearMesh, MLit });
			if (!T.nearLeaves.Empty()) L0.Add({ &T.nearLeaves, T.frond ? MFrond : MLit });
			if (!T.farMesh.Empty()) Lods.AddDefaulted_GetRef().Add({ &T.farMesh, MLit });
			const FString Name = Str(L.first);
			UStaticMesh* SM = ATGMesh::BuildStaticMesh(this, *(TEXT("Veg_") + Name), Lods);
			if (Lods.Num() > 1) ATGMesh::SetLodDistances(SM, { T.nearD > 0 ? T.nearD : 120.0 });
			Meshes.Add(SM);
			UHierarchicalInstancedStaticMeshComponent* H = NewInstances(*(TEXT("Veg_") + Name), SM, 0);
			const int32 FarCm = (int32)((T.farD > 0 ? T.farD : 1500.0) * 100);
			H->SetCullDistances(FarCm * 9 / 10, FarCm);
			TArray<FTransform> Xf;
			const std::vector<float>& A = *L.second;
			for (size_t I = 0; I + 4 < A.size(); I += 5) Xf.Add(FTransform(FRotator(0, ATG::MeshYaw(A[I + 3]), 0), ATG::ToUE(A[I], A[I + 1], A[I + 2]), FVector(A[I + 4])));
			H->AddInstances(Xf, false, true);
		}
	});
	// ---- containers (coloured per instance) and railway sleepers
	Steps.Add([this, D, MLit]() {
		const CityMap& M = D->Map;
		if (!M.containers.empty()) {
			UStaticMesh* SM = ATGMesh::BuildStaticMesh(this, TEXT("Container"), TArray<FATGPart>{ { &D->Container, MLit } });
			Meshes.Add(SM);
			UHierarchicalInstancedStaticMeshComponent* H = NewInstances(TEXT("Containers"), SM, 4);
			static const uint32 Colors[] = { 0xb03a2e, 0x1f618d, 0x117a65, 0xd4ac0d, 0x6c3483, 0xba4a00 };
			TArray<FTransform> Xf;
			for (const Container& C : M.containers) Xf.Add(FTransform(FRotator(0, ATG::MeshYaw(C.rot), 0), ATG::ToUE(C.x, (atg::IsSet(C.y) ? C.y : 0) + CURB_H + C.level * 2.6, C.z)));
			H->AddInstances(Xf, false, true);
			for (int32 I = 0; I < (int32)M.containers.size(); I++) {
				const FLinearColor Col = HexLinear(Colors[M.containers[I].color % 6]);
				const float Cd[4] = { 0.f, Col.R, Col.G, Col.B };
				H->SetCustomData(I, TArrayView<const float>(Cd, 4), false);
			}
			H->MarkRenderStateDirty();
		}
		if (!D->Roads.sleepers.empty()) {
			UStaticMesh* SM = ATGMesh::BuildStaticMesh(this, TEXT("Sleeper"), TArray<FATGPart>{ { &D->Sleeper, MLit } });
			Meshes.Add(SM);
			UHierarchicalInstancedStaticMeshComponent* H = NewInstances(TEXT("Sleepers"), SM, 0);
			H->SetCullDistances(30000, 40000);
			TArray<FTransform> Xf;
			for (const Sleeper& S : D->Roads.sleepers) Xf.Add(FTransform(FRotator(0, ATG::MeshYaw(S.yaw), 0), ATG::ToUE(S.x, S.y, S.z)));
			H->AddInstances(Xf, false, true);
		}
	});
	// ---- the map, the circle lookup, and the fine terrain round the start
	Steps.Add([this, D]() {
		MapWorldTex = MakeTexture(D->MapImg.world);
		MapCityTex = MakeTexture(D->MapImg.city);
		const MapLayer& W = D->MapImg.world; const MapLayer& C = D->MapImg.city;
		MapWorldRect = FBox2D(FVector2D(W.minX, W.minZ), FVector2D(W.maxX, W.maxZ));
		MapCityRect = FBox2D(FVector2D(C.minX, C.minZ), FVector2D(C.maxX, C.maxZ));
		for (const MapLabel& L : D->MapImg.labels) MapLabels.Add({ Str(L.name), L.x, L.z, L.big });
		D->MapImg = MapImages();
		if (!bHasFocus) {
			double X, Y, Z, Yaw;
			PlayerStart(X, Y, Z, Yaw);
			Focus = ATG::ToUE(X, Y, Z);
		}
		UpdateTerrain(true);
	});
}

void AATGWorld::SyncBrokenProps() {
	atg::Game* G = Game();
	if (!G || G->propVersion == PropVersion) return;
	PropVersion = G->propVersion;
	for (int32 I = 0; I < PropInstances.Num(); I++) {
		const TPair<int32, int32> Pi = PropInstances[I];
		if (Pi.Key < 0 || !PropMeshes.IsValidIndex(Pi.Key)) continue;
		const atg::PropInstance& P = G->props[I];
		const bool bBroken = G->brokenProps.count(I) > 0;
		FTransform T;
		PropMeshes[Pi.Key]->GetInstanceTransform(Pi.Value, T, true);
		const bool bGone = T.GetScale3D().X < 0.01;
		if (bBroken == bGone) continue;
		const FTransform Want = bBroken ? FTransform(FRotator::ZeroRotator, ATG::ToUE(P.x, -200, P.z), FVector(0.001))
			: FTransform(FRotator(0, ATG::MeshYaw(P.rot), 0), ATG::ToUE(P.x, P.y, P.z), FVector(P.scale));
		PropMeshes[Pi.Key]->UpdateInstanceTransform(Pi.Value, Want, true, true, true);
	}
}

UStaticMesh* AATGWorld::BeamMesh() {
	static TWeakObjectPtr<UStaticMesh> Cached;
	if (Cached.IsValid() && Cached->GetOuter() == this) return Cached.Get();
	// (in the car's own axes: 7 across, 11 along z, centred; uv y is 1 at the near end, 0 at the far end)
	atg::MeshBuf Q; Q.Color(1, 1, 1);
	const double A[3] = { -3.5, 0, -5.5 }, B[3] = { 3.5, 0, -5.5 }, C[3] = { 3.5, 0, 5.5 }, D[3] = { -3.5, 0, 5.5 }, Up[3] = { 0, 1, 0 };
	const double U0[2] = { 0, 1 }, U1[2] = { 1, 1 }, U2[2] = { 1, 0 }, U3[2] = { 0, 0 };
	Q.Quad(A, D, C, B, Up, U0, U3, U2, U1);
	UStaticMesh* M = ATGMesh::BuildStaticMesh(this, TEXT("HeadlightBeam"), TArray<FATGPart>{ { &Q, ATGMaterials::Get(EATGMat::Beam) } }, EATGAxes::Local);
	Meshes.Add(M);
	Cached = M;
	return M;
}

const FATGVehicleMeshes& AATGWorld::VehicleMeshes(const atg::VehicleDef& Def) {
	const FString Id = Str(Def.id);
	if (const FATGVehicleMeshes* M = VehicleCache.Find(Id)) return *M;
	FATGVehicleMeshes& M = VehicleCache.Add(Id);
	const atg::VehicleModel& Model = atg::BuildVehicleModel(Def);
	UMaterialInterface* Std = ATGMaterials::Get(EATGMat::Standard);
	for (const atg::VPart& P : Model.parts) M.Parts.Add(ATGMesh::BuildStaticMesh(this, *(TEXT("Veh_") + Id + TEXT("_") + Str(P.name)), TArray<FATGPart>{ { &P.mesh, Std } }, EATGAxes::Local));
	M.Wheel = ATGMesh::BuildStaticMesh(this, *(TEXT("Veh_") + Id + TEXT("_Wheel")), TArray<FATGPart>{ { &Model.wheel, Std } }, EATGAxes::Local);
	return M;
}

const FATGVehicleMeshes& AATGWorld::TrainMeshes(const atg::TrainModel& Model) {
	const FString Id = FString::Printf(TEXT("Train_%p"), (const void*)&Model);
	if (const FATGVehicleMeshes* M = VehicleCache.Find(Id)) return *M;
	FATGVehicleMeshes& M = VehicleCache.Add(Id);
	UMaterialInterface* Std = ATGMaterials::Get(EATGMat::Standard);
	for (const atg::VPart& P : Model.parts) M.Parts.Add(ATGMesh::BuildStaticMesh(this, *(Id + TEXT("_") + Str(P.name)), TArray<FATGPart>{ { &P.mesh, Std } }, EATGAxes::Local));
	return M;
}

const FATGVehicleMeshes& AATGWorld::BikeMeshes(const atg::VehicleDef& Def) {
	const FString Id = TEXT("Bike_") + Str(Def.id);
	if (const FATGVehicleMeshes* M = VehicleCache.Find(Id)) return *M;
	FATGVehicleMeshes& M = VehicleCache.Add(Id);
	const atg::BikeModel& B = atg::BuildBikeModel(Def);
	UMaterialInterface* Std = ATGMaterials::Get(EATGMat::Standard);
	auto Mesh = [&](const atg::MeshBuf& G, const TCHAR* Name) -> UStaticMesh* { return G.Empty() ? nullptr : ATGMesh::BuildStaticMesh(this, *(Id + TEXT("_") + Name), TArray<FATGPart>{ { &G, Std } }, EATGAxes::Local); };
	const bool Moto = Def.bike == "moto";
	// the windscreen (BoxGeometry(0.3, 0.2, 0.012) placed by its matrix) and the lamps (bikes.js headMesh / tailMesh)
	atg::MeshBuf Glass, Head, Tail;
	Glass.Color(1, 1, 1); Head.Color(1, 1, 1); Tail.Color(1, 1, 1);
	if (B.hasGlass) Glass.Add(atg::Geo::Box(0.3, 0.2, 0.012), B.glassMatrix);
	Head.Add(atg::Geo::Box(Moto ? 0.16 : 0.05, Moto ? 0.1 : 0.04, 0.03), atg::Mat4::Compose(0, B.headY, B.headZ));
	Tail.Add(atg::Geo::Box(Moto ? 0.14 : 0.05, 0.05, 0.02), atg::Mat4::Compose(0, B.tailY, B.tailZ));
	M.Parts.Add(Mesh(B.body, TEXT("Body")));
	M.Parts.Add(Mesh(B.trim, TEXT("Trim")));
	M.Parts.Add(Mesh(B.fork, TEXT("Fork")));
	M.Parts.Add(Mesh(B.crank, TEXT("Crank")));
	M.Parts.Add(Mesh(Glass, TEXT("Glass")));
	M.Parts.Add(Mesh(Head, TEXT("Head")));
	M.Parts.Add(Mesh(Tail, TEXT("Tail")));
	M.Wheel = Mesh(B.wheel, TEXT("Wheel"));
	return M;
}

UStaticMesh* AATGWorld::LocalMesh(const FString& Key, const atg::MeshBuf& G) {
	if (TObjectPtr<UStaticMesh>* M = LocalMeshCache.Find(Key)) return *M;
	UStaticMesh* M = G.Empty() ? nullptr : ATGMesh::BuildStaticMesh(this, *Key, TArray<FATGPart>{ { &G, ATGMaterials::Get(EATGMat::Standard) } }, EATGAxes::Local);
	LocalMeshCache.Add(Key, M);
	return M;
}

UStaticMesh* AATGWorld::PropMesh(int32 PropIndex) const {
	if (!PropInstances.IsValidIndex(PropIndex)) return nullptr;
	const int32 M = PropInstances[PropIndex].Key;
	return PropMeshes.IsValidIndex(M) ? PropMeshes[M]->GetStaticMesh() : nullptr;
}

void AATGWorld::SetDeathLook(double Desat, double Death, double Boost) {
	FPostProcessSettings& S = Post->Settings;
	// grey = mix(colour, luma, desat) then mix(..., luma, death * 1.3)
	const double Sat = (1 - FMath::Clamp(Desat, 0.0, 1.0)) * (1 - FMath::Clamp(Death * 1.3, 0.0, 1.0));
	const bool bOn = Desat > 0 || Death > 0;
	S.bOverride_ColorSaturation = bOn; S.ColorSaturation = FVector4(1, 1, 1, Sat);
	// brighter by death * boost, darker by 18% of death unless the night boost lifts it
	const double Gain = (1 + Death * Boost) * (1 - 0.18 * Death * (1 - FMath::Clamp(Boost * 0.5, 0.0, 1.0)));
	S.bOverride_ColorGain = bOn; S.ColorGain = FVector4(1, 1, 1, Gain);
	S.bOverride_VignetteIntensity = bOn; S.VignetteIntensity = 0.4f + 0.9f * (float)Death;
}

void AATGWorld::SetFringe(double Chroma) {
	FPostProcessSettings& S = Post->Settings;
	// (the browser game's 0.0022 offsets the corners by about a pixel and a half: Unreal's 0.3)
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = (float)(Chroma / 0.0022 * 0.3);
}

USkeletalMesh* AATGWorld::HumanMesh(const atg::Appearance& A) {
	const FString Key = ATGHuman::Key(A);
	if (TWeakObjectPtr<USkeletalMesh>* M = HumanCache.Find(Key)) if (M->IsValid()) return M->Get();
	USkeleton* Skel = HumanSkeleton;
	USkeletalMesh* M = ATGHuman::Build(this, A, Skel, ATGMaterials::Get(EATGMat::VertexLit));
	HumanSkeleton = Skel;
	HumanCache.Add(Key, M);
	// (forget the ones nobody uses any more)
	if (HumanCache.Num() > 400) for (auto It = HumanCache.CreateIterator(); It; ++It) if (!It.Value().IsValid()) It.RemoveCurrent();
	return M;
}

UTexture* AATGWorld::Picture(const FString& Name) {
	if (TObjectPtr<UTextureRenderTarget2D>* T = Pictures.Find(Name)) return *T;
	UTextureRenderTarget2D* T = ATGPictures::Paint(this, Name);
	Pictures.Add(Name, T);
	return T;
}

UStaticMesh* AATGWorld::WeaponMesh(const FString& Id) {
	if (TObjectPtr<UStaticMesh>* M = WeaponCache.Find(Id)) return *M;
	const atg::MeshBuf& G = atg::WeaponGeometry(TCHAR_TO_UTF8(*Id));
	UStaticMesh* M = G.Empty() ? nullptr : ATGMesh::BuildStaticMesh(this, *(TEXT("Weapon_") + Id), TArray<FATGPart>{ { &G, ATGMaterials::Get(EATGMat::VertexLit) } }, EATGAxes::Local);
	WeaponCache.Add(Id, M);
	return M;
}

// ------------------------------------------------------------------ fine terrain
void AATGWorld::AddNearChunk(int32 Key, atg::MeshBuf* Mesh) {
	UProceduralMeshComponent* C = NewMeshComponent(TEXT("TerrainNear"), true);
	ATGMesh::ToSection(C, 0, *Mesh, true);
	C->SetMaterial(0, ATGMaterials::Get(EATGMat::Terrain));
	delete Mesh;
	TerrainNear.Add(Key, C);
	if (TerrainFar.IsValidIndex(Key) && TerrainFar[Key]) TerrainFar[Key]->SetVisibility(false);
}

void AATGWorld::UpdateTerrain(bool bBlocking) {
	using namespace atg;
	if (!Data) return;
	// finished jobs
	for (auto It = TerrainJobs.CreateIterator(); It; ++It) {
		if (!It.Value().IsReady()) continue;
		AddNearChunk(It.Key(), It.Value().Get());
		It.RemoveCurrent();
	}
	double Fx, Fy, Fz;
	ATG::FromUE(Focus, Fx, Fy, Fz);
	const double Alt = FMath::Max(0.0, Fy - Data->Map.hf.Sample(Fx, Fz));
	const double R0 = 330 + Alt * 0.6;
	const int32 NX = TerrainChunksX(), NZ = TerrainChunksZ();
	const CityMap* M = &Data->Map;
	for (int32 J = 0; J < NZ; J++) for (int32 I = 0; I < NX; I++) {
		if (TerrainChunkInCity(I, J)) continue;
		const int32 Key = I + J * NX;
		const double Cx = WORLD.minX + (I + 0.5) * TERRAIN_CH, Cz = WORLD.minZ + (J + 0.5) * TERRAIN_CH;
		const double Dx = FMath::Max(FMath::Abs(Fx - Cx) - TERRAIN_CH / 2, 0.0), Dz = FMath::Max(FMath::Abs(Fz - Cz) - TERRAIN_CH / 2, 0.0);
		const double Dist = FMath::Sqrt(Dx * Dx + Dz * Dz);
		const bool bHave = TerrainNear.Contains(Key), bPending = TerrainJobs.Contains(Key);
		if (Dist < R0 && !bHave && !bPending) {
			if (bBlocking) AddNearChunk(Key, new MeshBuf(BuildTerrainChunk(*M, I, J, 0)));
			else if (TerrainJobs.Num() < 3) TerrainJobs.Add(Key, Async(EAsyncExecution::ThreadPool, [M, I, J]() { return new MeshBuf(BuildTerrainChunk(*M, I, J, 0)); }));
		} else if (bHave && Dist > R0 + 400) {
			TObjectPtr<UProceduralMeshComponent> C;
			TerrainNear.RemoveAndCopyValue(Key, C);
			if (C) C->DestroyComponent();
			if (TerrainFar.IsValidIndex(Key) && TerrainFar[Key]) TerrainFar[Key]->SetVisibility(true);
		}
	}
}

// ------------------------------------------------------------------ tick
void AATGWorld::Tick(float Dt) {
	Super::Tick(Dt);
	if (!Data && Job.IsValid() && Job.IsReady()) {
		Data = Job.Get();
		Job.Reset();
		QueueBuild();
	}
	if (Data && !bReady) {
		// build for up to ~12 ms a frame so the loading screen keeps drawing
		const double Until = FPlatformTime::Seconds() + 0.012;
		while (StepIndex < Steps.Num() && FPlatformTime::Seconds() < Until) Steps[StepIndex++]();
		if (StepIndex >= Steps.Num()) { bReady = true; Steps.Empty(); UE_LOG(LogATG, Log, TEXT("World ready")); }
	}
	if (bReady) {
		TerrainTimer -= Dt;
		if (TerrainTimer <= 0) { TerrainTimer = 0.2; UpdateTerrain(false); }
		SyncBrokenProps();
		UpdateLamps(Dt);
	}
	UpdateSky(Dt);
}

// ------------------------------------------------------------------ sky, sun and moon (environment.js)
void AATGWorld::UpdateSky(float Dt) {
	// (the clock runs in the simulation; before it exists, the loading screen shows the start time)
	if (atg::Game* G = Game()) Hours = (float)G->env.hours;
	const double Theta = (Hours - 6.0) / 24.0 * 2 * UE_DOUBLE_PI, Tilt = 0.45;
	FVector SunDir(FMath::Cos(Theta), FMath::Sin(Theta) * FMath::Cos(Tilt) + 0.2, FMath::Sin(Theta) * FMath::Sin(Tilt) + 0.08); // game axes
	SunDir.Normalize();
	FVector MoonDir(-FMath::Cos(Theta) * 0.9, -FMath::Sin(Theta) * FMath::Cos(Tilt) * 0.95 + 0.12, -FMath::Sin(Theta) * FMath::Sin(Tilt) - 0.2);
	MoonDir.Normalize();
	const double SunY = SunDir.Y;
	Night = (float)(1 - SmoothStep(-0.18, 0.06, SunY));
	// the weather (environment.js): cloud and rain dim the sun, fog thickens, lightning flashes, and the street
	// lights come on earlier under a gloomy sky
	double Cloud = 0.35, Rain = 0, FogBoost = 0, Lightning = 0;
	if (atg::Game* G = Game()) { Cloud = G->env.cloudCover; Rain = G->env.rain; FogBoost = G->env.fogBoost; Lightning = G->env.lightning; }
	const double Gloom = Cloud * 0.05 + Rain * 0.08 + FogBoost * 0.04;
	StreetLights = (float)(1 - SmoothStep(0.02, 0.15, SunY - Gloom));

	// directional lights shine along -direction (from the sky down)
	const FVector SunUE = ATG::DirToUE(SunDir.X, SunDir.Y, SunDir.Z);
	Sun->SetWorldRotation((-SunUE).Rotation());
	const float SunK = (float)SmoothStep(-0.12, 0.02, SunY);
	const float Weather = (float)((1 - Cloud * 0.55) * (1 - Rain * 0.4));
	Sun->SetIntensity(SunLux * SunK * Weather + SunLux * (float)Lightning * 1.8f);
	Sun->SetLightColor(Lightning > 0 ? FLinearColor(0.8f, 0.85f, 1.f) : FLinearColor::White);
	Sun->SetCastShadows(SunK > 0.01f);
	if (MoonDir.Y < 0.12) { MoonDir.Y = 0.12; MoonDir.Normalize(); }
	const FVector MoonUE = ATG::DirToUE(MoonDir.X, MoonDir.Y, MoonDir.Z);
	Moon->SetWorldRotation((-MoonUE).Rotation());
	const float MoonK = Night * (float)SmoothStep(-0.05, 0.2, MoonDir.Y);
	Moon->SetIntensity(MoonLux * MoonK * (float)(1 - Cloud * 0.5));
	Moon->SetCastShadows(SunK <= 0.01f && MoonK > 0.2f);
	SkyLight->SetIntensity(1.f + Night * 1.5f);
	FLinearColor FogCol = FMath::Lerp(FLinearColor(0.45f, 0.5f, 0.6f), FLinearColor(0.012f, 0.014f, 0.022f), Night * 0.95f);
	if (Cloud > 0.6 || Rain > 0.1) { const float L = (FogCol.R + FogCol.G + FogCol.B) / 3; FogCol = FMath::Lerp(FogCol, FLinearColor(L, L * 1.02f, L * 1.06f), (float)FMath::Clamp(Rain + (Cloud - 0.6), 0.0, 0.8)); }
	Fog->SetFogInscatteringColor(FogCol);
	// (uFogDensity = 0.00105 + fogBoost * 0.009 + rain * 0.002: the clear-weather density scaled the same way)
	Fog->SetFogDensity(0.012f * (float)((0.00105 + FogBoost * 0.009 + Rain * 0.002) / 0.00105));

	if (UMaterialParameterCollection* C = ATGMaterials::Collection()) {
		if (UWorld* W = GetWorld()) if (UMaterialParameterCollectionInstance* I = W->GetParameterCollectionInstance(C)) {
			I->SetScalarParameterValue(TEXT("Night"), Night);
			I->SetScalarParameterValue(TEXT("StreetLights"), StreetLights);
			if (atg::Game* G = Game()) {
				I->SetScalarParameterValue(TEXT("SimTime"), (float)G->env.uTime); I->SetScalarParameterValue(TEXT("Wet"), (float)G->env.wet); I->SetScalarParameterValue(TEXT("Rain"), (float)G->env.rain);
				if (const APlayerController* PC = W->GetFirstPlayerController()) if (PC->PlayerCameraManager) {
					double Cx, Cy, Cz; ATG::FromUE(PC->PlayerCameraManager->GetCameraLocation(), Cx, Cy, Cz);
					I->SetVectorParameterValue(TEXT("CamPos"), FLinearColor((float)Cx, (float)Cy, (float)Cz, 0.f));
				}
				if (RainComp) RainComp->SetVisibility(G->env.rain > 0.02);
			}
		}
	}
}

void AATGWorld::UpdateLamps(float Dt) {
	LampTimer -= Dt;
	if (LampTimer <= 0 && bHasFocus) {
		LampTimer = 0.4;
		double Fx, Fy, Fz;
		ATG::FromUE(Focus, Fx, Fy, Fz);
		TArray<TPair<double, int32>> Near;
		for (int32 I = 0; I < LampPositions.Num(); I++) {
			const double D2 = FMath::Square(LampPositions[I].X - Fx) + FMath::Square(LampPositions[I].Z - Fz);
			if (D2 < 110.0 * 110.0) Near.Add({ D2, I });
		}
		Near.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
		for (int32 K = 0; K < Lamps.Num(); K++) {
			if (K < Near.Num()) {
				const FVector& P = LampPositions[Near[K].Value];
				Lamps[K]->SetWorldLocation(ATG::ToUE(P.X, P.Y, P.Z));
				Lamps[K]->SetVisibility(true);
			} else Lamps[K]->SetVisibility(false);
		}
	}
	for (UPointLightComponent* L : Lamps) L->SetIntensity(StreetLights * LampCandelas);
	// the billboards light up at night (emissiveIntensity 0.05 + night * 1.6); the signs brighten (0.55 + night * 2.2)
	if (BillboardMat) BillboardMat->SetVectorParameterValue(TEXT("Mode"), FLinearColor(1.f, 0.05f + Night * 1.6f, 0.5f, 0.f));
	for (UMaterialInstanceDynamic* M : SignMats) if (M) M->SetVectorParameterValue(TEXT("Mode"), FLinearColor(0.f, 0.55f + Night * 2.2f, 0.f, 0.f));
	// the landmarks: the pier lamps, the wheel turning (wheelA += dt * 0.05) with its gondolas and flickering neon,
	// the Vistawood letters glowing at night, the beacons blinking (on for 0.25 s in every 1.6 s)
	{
		const double T = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0;
		if (LandmarkLampMat) LandmarkLampMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.4f + Night * 5, 0.35f + Night * 4, 0.25f + Night * 2.5f));
		if (Wheel) {
			WheelA += Dt * 0.05;
			Wheel->SetWorldRotation(FRotator(FMath::RadiansToDegrees(WheelA), 0, 0));
			for (int32 K = 0; K < Gondolas.Num(); K++) {
				const double A = K / 16.0 * 2 * UE_DOUBLE_PI + WheelA;
				Gondolas[K]->SetWorldLocation(WheelHub + FVector(std::cos(A) * WheelR * 100, 0, std::sin(A) * WheelR * 100));
			}
		}
		const float K = 0.3f + Night * 3.5f;
		if (NeonMat) NeonMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(K * (0.8f + 0.2f * (float)std::sin(T * 3)), K, K * (0.8f + 0.2f * (float)std::cos(T * 2))));
		for (UMaterialInstanceDynamic* M : LetterMats) if (M) M->SetVectorParameterValue(TEXT("Mode"), FLinearColor(1.f, Night * 0.9f, 0.6f, 0.f));
		const float On = std::fmod(T, 1.6) < 0.25 ? 1.f : 0.05f;
		if (BeaconMat) BeaconMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(8 * On, 0.3f * On, 0.2f * On));
	}
	// the cargo ship's deck lamps (lampMat.color.setScalar(0.4 + night * 3))
	if (ShipLampMat) { const float V = 0.4f + Night * 3.f; ShipLampMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(V, V, V)); }
	// the shops' light, in the one nearest the camera (within 45 m)
	if (InteriorLight && Data) {
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (PC && PC->PlayerCameraManager) {
			double Cx, Cy, Cz;
			ATG::FromUE(PC->PlayerCameraManager->GetCameraLocation(), Cx, Cy, Cz);
			const atg::InteriorShell* Best = nullptr;
			double Bd = 45.0 * 45.0;
			for (const atg::InteriorShell& It : Data->Map.interiors) {
				const double D2 = FMath::Square(It.center.x - Cx) + FMath::Square(It.center.z - Cz);
				if (D2 < Bd) { Bd = D2; Best = &It; }
			}
			if (Best) {
				InteriorLight->SetWorldLocation(ATG::ToUE(Best->light.x, Best->light.y, Best->light.z));
				InteriorLight->SetIntensity(24.f * LampCandelas / 95.f);
			}
			InteriorLight->SetVisibility(Best != nullptr);
		}
	}
}
