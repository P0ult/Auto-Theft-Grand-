#include "Game/ATGWorld.h"
#include "AutoTheftGrand.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshUtil.h"
#include "Gen/MapImage.h"
#include "Gen/Models.h"
#include "Gen/WorldMeshes.h"

#include "Async/Async.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "ProceduralMeshComponent.h"

// Everything the worker thread makes: the generated map and the (plain C++) geometry
struct FATGWorldData {
	atg::CityMap Map;
	atg::RoadMeshes Roads;
	atg::MeshBuf Street, Blocks, Lots, Pads, Pools, Pier, Lake, Container, Sleeper;
	std::map<std::pair<int, int>, atg::CityChunk> City;
	std::map<std::string, atg::PropTemplate> PropDefs;
	std::vector<atg::PropInstance> Props;
	std::map<std::string, atg::VegTemplate> VegDefs;
	std::map<std::pair<int, int>, atg::MeshBuf> Boxes;        // walls, fences, containers (collision only)
	std::map<std::pair<int, int>, atg::MeshBuf> VegColliders; // trunks and rocks (collision only)
	struct FTerrain { int I, J; atg::MeshBuf Mesh; };
	std::vector<FTerrain> Terrain;
	std::vector<atg::MeshBuf> Sea;
	atg::MapImages MapImg;
};

namespace {
constexpr double COLLIDER_CH = 400;   // collision chunk size (m)
constexpr double CIRCLE_CELL = 24;    // circle lookup grid (m)
constexpr int32 PROP_CHUNKS_X = 64;   // prop collision chunk key stride

int64 CellKey(int64 I, int64 J) { return I * 1000003 + J; }

double SrgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
FLinearColor HexLinear(uint32 Hex) {
	return FLinearColor((float)SrgbToLinear(((Hex >> 16) & 255) / 255.0), (float)SrgbToLinear(((Hex >> 8) & 255) / 255.0), (float)SrgbToLinear((Hex & 255) / 255.0), 1.f);
}
double SmoothStep(double A, double B, double X) { const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0); return T * T * (3 - 2 * T); }
FString Str(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

// a hexagonal prism standing at (x, z): the collision stand-in for a post, a trunk or a rock
void Prism(atg::MeshBuf& G, double X, double Z, double R, double Y0, double Y1) {
	if (R <= 0) return;
	const int N = 6;
	uint32_t Bot[N], Top[N];
	for (int K = 0; K < N; K++) {
		const double A = (double)K / N * 2 * UE_DOUBLE_PI, C = std::cos(A), S = std::sin(A);
		Bot[K] = G.V(X + C * R, Y0, Z + S * R, C, 0, S);
		Top[K] = G.V(X + C * R, Y1, Z + S * R, C, 0, S);
	}
	for (int K = 0; K < N; K++) G.QuadAuto(Bot[K], Bot[(K + 1) % N], Top[(K + 1) % N], Top[K]);
	const uint32_t Cn = G.V(X, Y1, Z, 0, 1, 0);
	for (int K = 0; K < N; K++) G.Tri(Cn, Top[(K + 1) % N], Top[K]);
}

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

// Santa Luz pier: a plain timber deck that follows the ground function (the pier's lamps, railings and the
// Ferris wheel come with the landmarks)
atg::MeshBuf BuildPier(const atg::CityMap& Map) {
	using namespace atg;
	MeshBuf G;
	auto It = Map.landmarks.find("pier");
	if (It == Map.landmarks.end()) return G;
	const auto& V = It->second.vals;
	const double X0 = V.at("x0"), X1 = V.at("x1"), Z0 = V.at("z0"), Z1 = V.at("z1"), Xm = (X0 + X1) / 2;
	G.ColorHex(0x8a6a4a); G.Rough(0.85);
	const int N = (int)std::ceil((Z1 - Z0) / 4);
	for (int K = 0; K < N; K++) {
		const double Za = Z0 + (Z1 - Z0) * K / N, Zb = Z0 + (Z1 - Z0) * (K + 1) / N;
		const double Ya = Map.GroundHeight(Xm, Za + 0.01), Yb = Map.GroundHeight(Xm, Zb - 0.01);
		const double Up[3] = { 0, 1, 0 }, L[3] = { -1, 0, 0 }, R[3] = { 1, 0, 0 };
		{ const double A[3] = { X0, Ya, Za }, B[3] = { X0, Yb, Zb }, C[3] = { X1, Yb, Zb }, D[3] = { X1, Ya, Za }; G.Quad(A, B, C, D, Up); }
		{ const double A[3] = { X0, -3, Za }, B[3] = { X0, -3, Zb }, C[3] = { X0, Yb, Zb }, D[3] = { X0, Ya, Za }; G.Quad(A, B, C, D, L); }
		{ const double A[3] = { X1, -3, Zb }, B[3] = { X1, -3, Za }, C[3] = { X1, Ya, Za }, D[3] = { X1, Yb, Zb }; G.Quad(A, B, C, D, R); }
	}
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
	D->Pier = BuildPier(M);
	Stage = 4;
	D->PropDefs = BuildPropTemplates();
	D->Props = PlaceProps(M);
	D->VegDefs = BuildVegTemplates();
	D->Container = ContainerGeo();
	{ MeshBuf S; S.ColorHex(0x4d443c); S.Rough(0.95); S.Add(Geo::Box(2.6, 0.16, 0.26), Mat4::Identity()); D->Sleeper = std::move(S); }
	// walls: the map's colliders, except the walk-in shops' shells (their interiors come later: for now
	// the shops are solid)
	{
		size_t Shells = 0;
		for (const InteriorShell& It : M.interiors) Shells += It.colliders.size();
		const size_t N = M.colliders.size() - (std::min)(Shells, M.colliders.size());
		auto Chunk = [&](double X, double Z) -> MeshBuf& { return D->Boxes[{ (int)std::floor(X / COLLIDER_CH), (int)std::floor(Z / COLLIDER_CH) }]; };
		for (size_t I = 0; I < N; I++) {
			const Collider& C = M.colliders[I];
			if (C.oriented) {
				MeshBuf& G = Chunk(C.cx, C.cz);
				const size_t S = G.Count();
				G.Box(C.cx - C.hx, C.minY, C.cz - C.hz, C.cx + C.hx, C.maxY, C.cz + C.hz, true, true, true);
				G.RotateFrom(S, C.cx, C.cz, C.yaw);
			} else {
				Chunk((C.minX + C.maxX) / 2, (C.minZ + C.maxZ) / 2).Box(C.minX, C.minY, C.minZ, C.maxX, C.maxY, C.maxZ, true, true, true);
			}
		}
		for (const InteriorShell& It : M.interiors) {
			const Building& B = M.buildings[It.building];
			Chunk((B.x0 + B.x1) / 2, (B.z0 + B.z1) / 2).Box(B.x0, B.y0 - 0.2, B.z0, B.x1, B.y1 + (B.roof == "gable" ? 2.5 : 0), B.z1, true, true, true);
		}
	}
	// trunks and rocks
	{
		const std::pair<const char*, const std::vector<float>*> Lists[] = { { "pine", &M.vegetation.pine }, { "oak", &M.vegetation.oak }, { "cactus", &M.vegetation.cactus },
			{ "rock", &M.vegetation.rock }, { "deadtree", &M.vegetation.deadtree }, { "palm", &M.vegetation.palm } };
		const std::map<std::string, std::pair<double, double>> K = { { "pine", { 0.45, 12 } }, { "oak", { 0.4, 6 } }, { "cactus", { 0.4, 5 } }, { "rock", { 1.3, 1.4 } }, { "deadtree", { 0.3, 4 } }, { "palm", { 0.35, 9 } } };
		for (const auto& L : Lists) {
			const auto Kr = K.at(L.first);
			const bool Rock = std::string(L.first) == "rock";
			const std::vector<float>& A = *L.second;
			for (size_t I = 0; I + 4 < A.size(); I += 5) {
				const double Sc = A[I + 4];
				Prism(D->VegColliders[{ (int)std::floor(A[I] / COLLIDER_CH), (int)std::floor(A[I + 2] / COLLIDER_CH) }], A[I], A[I + 2], Kr.first * (Rock ? Sc : atg::Min(1.3, Sc)), A[I + 1] - 1, A[I + 1] + Kr.second * Sc);
			}
		}
	}
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

	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(Root);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetAtmosphereSunLight(true);
	Moon->SetAtmosphereSunLightIndex(1);
	Moon->SetIntensity(0.f);
	Moon->SetLightColor(FLinearColor(0.55f, 0.65f, 1.f));
	Moon->SetCastShadows(false);

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

// ------------------------------------------------------------------ building the components
UProceduralMeshComponent* AATGWorld::NewMeshComponent(const TCHAR* Name, bool bCollision) {
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this, MakeUniqueObjectName(this, UProceduralMeshComponent::StaticClass(), FName(Name)));
	C->SetupAttachment(Root);
	C->SetMobility(EComponentMobility::Static);
	C->bUseAsyncCooking = true;
	C->bUseComplexAsSimpleCollision = true;
	if (bCollision) C->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	else C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
		if (!D->Pier.Empty()) {
			UProceduralMeshComponent* P = NewMeshComponent(TEXT("Pier"), true);
			ATGMesh::ToSection(P, 0, D->Pier, true);
			P->SetMaterial(0, MLit);
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
	// ---- collision: walls, trunks and rocks (hidden)
	Steps.Add([this, D]() {
		auto Hidden = [this](const TCHAR* Name, const MeshBuf& B, bool bCamera) {
			UProceduralMeshComponent* C = NewMeshComponent(Name, true);
			ATGMesh::ToCollisionSection(C, 0, B);
			C->SetVisibility(false);
			C->SetCastShadow(false);
			if (!bCamera) C->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			return C;
		};
		for (const auto& B : D->Boxes) Hidden(TEXT("Walls"), B.second, true);
		for (const auto& B : D->VegColliders) { UProceduralMeshComponent* C = Hidden(TEXT("Trunks"), B.second, false); C->ComponentTags.Add(TEXT("ATGCircles")); }
		D->Boxes.clear(); D->VegColliders.clear();
	});
	// ---- props: one instanced mesh per kind, plus their circles and collision chunks
	Steps.Add([this, D, MLit, MFrond]() {
		TMap<FString, int32> MeshOf;
		TArray<TArray<FTransform>> Xf;
		TArray<TArray<float>> Phase;
		TMap<int32, int32> ChunkIndex;
		for (const PropInstance& P : D->Props) {
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
			const bool bMulti = P.type == "boothbar" || P.type == "sandbags";
			FATGCircle Ci;
			Ci.X = P.x; Ci.Z = P.z; Ci.R = bMulti ? 1.4 : Def->second.r; Ci.Y0 = P.y - 0.5; Ci.Top = P.y + Def->second.h;
			Ci.bBreakable = Def->second.breakable; Ci.Mesh = *M; Ci.Instance = Inst;
			const int32 Key = (int32)std::floor(P.x / COLLIDER_CH) + 32 + ((int32)std::floor(P.z / COLLIDER_CH) + 32) * PROP_CHUNKS_X;
			int32* Chunk = ChunkIndex.Find(Key);
			if (!Chunk) { Chunk = &ChunkIndex.Add(Key, PropColliderCircles.Num()); PropColliderCircles.AddDefaulted(); }
			Ci.Chunk = *Chunk;
			PropColliderCircles[*Chunk].Add(Circles.Add(Ci));
		}
		for (int32 M = 0; M < PropMeshes.Num(); M++) {
			PropMeshes[M]->AddInstances(Xf[M], false, true);
			for (int32 I = 0; I < Xf[M].Num(); I++) {
				const float Cd[4] = { Phase[M][I], 1.f, 1.f, 1.f };
				PropMeshes[M]->SetCustomData(I, TArrayView<const float>(Cd, 4), false);
			}
			PropMeshes[M]->MarkRenderStateDirty();
		}
		PropColliders.SetNum(PropColliderCircles.Num());
		for (int32 K = 0; K < PropColliderCircles.Num(); K++) BuildPropColliderChunk(K);
		// the lamp heads, for the few real lights that follow the player at night
		for (const PropInstance& P : D->Props) if (P.type == "streetlight") LampPositions.Add(FVector(P.x + std::sin(P.rot) * 3.1, P.y + 7.6, P.z + std::cos(P.rot) * 3.1));
	});
	// ---- vegetation: near and far models as two levels of detail
	Steps.Add([this, D, MLit, MFrond]() {
		const Vegetation& V = D->Map.vegetation;
		const std::pair<const char*, const std::vector<float>*> Lists[] = { { "pine", &V.pine }, { "oak", &V.oak }, { "bush", &V.bush }, { "cactus", &V.cactus },
			{ "rock", &V.rock }, { "deadtree", &V.deadtree }, { "palm", &V.palm } };
		const std::map<std::string, double> CircleR = { { "pine", 0.45 }, { "oak", 0.4 }, { "cactus", 0.4 }, { "rock", 1.3 }, { "deadtree", 0.3 }, { "palm", 0.35 } };
		const std::map<std::string, double> CircleH = { { "pine", 12 }, { "oak", 6 }, { "cactus", 5 }, { "rock", 1.4 }, { "deadtree", 4 }, { "palm", 9 } };
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
			const bool Rock = std::string(L.first) == "rock";
			auto R = CircleR.find(L.first);
			for (size_t I = 0; I + 4 < A.size(); I += 5) {
				Xf.Add(FTransform(FRotator(0, ATG::MeshYaw(A[I + 3]), 0), ATG::ToUE(A[I], A[I + 1], A[I + 2]), FVector(A[I + 4])));
				if (R != CircleR.end()) {
					FATGCircle Ci;
					Ci.X = A[I]; Ci.Z = A[I + 2]; Ci.R = R->second * (Rock ? A[I + 4] : atg::Min(1.3, (double)A[I + 4]));
					Ci.Y0 = A[I + 1] - 1; Ci.Top = A[I + 1] + CircleH.at(L.first) * A[I + 4];
					Circles.Add(Ci);
				}
			}
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
		for (int32 I = 0; I < Circles.Num(); I++) {
			const FATGCircle& Ci = Circles[I];
			const int64 X0 = (int64)FMath::FloorToDouble((Ci.X - Ci.R) / CIRCLE_CELL), X1 = (int64)FMath::FloorToDouble((Ci.X + Ci.R) / CIRCLE_CELL);
			const int64 Z0 = (int64)FMath::FloorToDouble((Ci.Z - Ci.R) / CIRCLE_CELL), Z1 = (int64)FMath::FloorToDouble((Ci.Z + Ci.R) / CIRCLE_CELL);
			for (int64 X = X0; X <= X1; X++) for (int64 Z = Z0; Z <= Z1; Z++) CircleCells.FindOrAdd(CellKey(X, Z)).Add(I);
		}
		if (!bHasFocus) {
			double X, Y, Z, Yaw;
			PlayerStart(X, Y, Z, Yaw);
			Focus = ATG::ToUE(X, Y, Z);
		}
		UpdateTerrain(true);
	});
}

void AATGWorld::BuildPropColliderChunk(int32 Chunk) {
	atg::MeshBuf G;
	for (int32 I : PropColliderCircles[Chunk]) {
		const FATGCircle& C = Circles[I];
		if (!C.bBroken) Prism(G, C.X, C.Z, C.R, C.Y0, C.Top);
	}
	UProceduralMeshComponent* Comp = PropColliders[Chunk];
	if (!Comp) {
		Comp = NewMeshComponent(TEXT("PropColliders"), true);
		PropColliders[Chunk] = Comp;
		Comp->SetVisibility(false);
		Comp->SetCastShadow(false);
		Comp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Comp->ComponentTags.Add(TEXT("ATGCircles"));
	}
	Comp->ClearAllMeshSections();
	if (!G.Empty()) ATGMesh::ToCollisionSection(Comp, 0, G);
}

void AATGWorld::QueryCircles(double X, double Z, double R, TArray<int32>& Out) const {
	Out.Reset();
	const int64 X0 = (int64)FMath::FloorToDouble((X - R) / CIRCLE_CELL), X1 = (int64)FMath::FloorToDouble((X + R) / CIRCLE_CELL);
	const int64 Z0 = (int64)FMath::FloorToDouble((Z - R) / CIRCLE_CELL), Z1 = (int64)FMath::FloorToDouble((Z + R) / CIRCLE_CELL);
	for (int64 CX = X0; CX <= X1; CX++) for (int64 CZ = Z0; CZ <= Z1; CZ++) {
		if (const TArray<int32>* A = CircleCells.Find(CellKey(CX, CZ))) for (int32 I : *A) Out.AddUnique(I);
	}
}

void AATGWorld::BreakProp(int32 CircleIndex) {
	FATGCircle& C = Circles[CircleIndex];
	if (C.bBroken || !C.bBreakable) return;
	C.bBroken = true;
	if (PropMeshes.IsValidIndex(C.Mesh)) {
		FTransform Gone(FRotator::ZeroRotator, ATG::ToUE(C.X, -200, C.Z), FVector(0.001));
		PropMeshes[C.Mesh]->UpdateInstanceTransform(C.Instance, Gone, true, true, true);
	}
	if (C.Chunk >= 0) DirtyPropChunks.Add(C.Chunk);
}

const FATGCarMeshes& AATGWorld::CarMeshes(const FString& Id) {
	if (const FATGCarMeshes* M = CarCache.Find(Id)) return *M;
	FATGCarMeshes& M = CarCache.Add(Id);
	const atg::CarDef* Def = atg::FindCar(TCHAR_TO_UTF8(*Id));
	if (!Def) return M;
	const atg::CarModel Model = atg::BuildCarModel(*Def);
	UMaterialInterface* Lit = ATGMaterials::Get(EATGMat::VertexLit);
	M.Paint = ATGMesh::BuildStaticMesh(this, *(TEXT("Car_") + Id + TEXT("_Paint")), TArray<FATGPart>{ { &Model.paint, Lit } }, EATGAxes::Local);
	M.Trim = ATGMesh::BuildStaticMesh(this, *(TEXT("Car_") + Id + TEXT("_Trim")), TArray<FATGPart>{ { &Model.trim, Lit } }, EATGAxes::Local);
	M.Wheel = ATGMesh::BuildStaticMesh(this, *(TEXT("Car_") + Id + TEXT("_Wheel")), TArray<FATGPart>{ { &Model.wheel, Lit } }, EATGAxes::Local);
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
		DirtyTimer -= Dt;
		if (DirtyTimer <= 0 && DirtyPropChunks.Num()) {
			DirtyTimer = 0.5;
			for (int32 K : DirtyPropChunks) BuildPropColliderChunk(K);
			DirtyPropChunks.Empty();
		}
		UpdateLamps(Dt);
	}
	UpdateSky(Dt);
}

// ------------------------------------------------------------------ sky, sun and moon (environment.js)
void AATGWorld::UpdateSky(float Dt) {
	Hours = FMath::Fmod(Hours + Dt * TimeScale / 60.f, 24.f);
	if (Hours < 0) Hours += 24.f;
	const double Theta = (Hours - 6.0) / 24.0 * 2 * UE_DOUBLE_PI, Tilt = 0.45;
	FVector SunDir(FMath::Cos(Theta), FMath::Sin(Theta) * FMath::Cos(Tilt) + 0.2, FMath::Sin(Theta) * FMath::Sin(Tilt) + 0.08); // game axes
	SunDir.Normalize();
	FVector MoonDir(-FMath::Cos(Theta) * 0.9, -FMath::Sin(Theta) * FMath::Cos(Tilt) * 0.95 + 0.12, -FMath::Sin(Theta) * FMath::Sin(Tilt) - 0.2);
	MoonDir.Normalize();
	const double SunY = SunDir.Y;
	Night = (float)(1 - SmoothStep(-0.18, 0.06, SunY));
	StreetLights = (float)(1 - SmoothStep(0.02, 0.15, SunY));

	// directional lights shine along -direction (from the sky down)
	const FVector SunUE = ATG::DirToUE(SunDir.X, SunDir.Y, SunDir.Z);
	Sun->SetWorldRotation((-SunUE).Rotation());
	const float SunK = (float)SmoothStep(-0.12, 0.02, SunY);
	Sun->SetIntensity(SunLux * SunK);
	Sun->SetCastShadows(SunK > 0.01f);
	if (MoonDir.Y < 0.12) { MoonDir.Y = 0.12; MoonDir.Normalize(); }
	const FVector MoonUE = ATG::DirToUE(MoonDir.X, MoonDir.Y, MoonDir.Z);
	Moon->SetWorldRotation((-MoonUE).Rotation());
	const float MoonK = Night * (float)SmoothStep(-0.05, 0.2, MoonDir.Y);
	Moon->SetIntensity(MoonLux * MoonK);
	Moon->SetCastShadows(SunK <= 0.01f && MoonK > 0.2f);
	SkyLight->SetIntensity(1.f + Night * 1.5f);
	Fog->SetFogInscatteringColor(FMath::Lerp(FLinearColor(0.45f, 0.5f, 0.6f), FLinearColor(0.012f, 0.014f, 0.022f), Night * 0.95f));

	if (UMaterialParameterCollection* C = ATGMaterials::Collection()) {
		if (UWorld* W = GetWorld()) if (UMaterialParameterCollectionInstance* I = W->GetParameterCollectionInstance(C)) {
			I->SetScalarParameterValue(TEXT("Night"), Night);
			I->SetScalarParameterValue(TEXT("StreetLights"), StreetLights);
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
}
