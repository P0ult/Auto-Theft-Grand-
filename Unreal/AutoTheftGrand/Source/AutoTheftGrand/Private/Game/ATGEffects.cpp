#include "Game/ATGEffects.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGWorld.h"
#include "Gen/VehicleDefs.h"
#include "Gen/VehicleModels.h"
#include "Game/ATGMeshUtil.h"
#include "Gen/MeshBuf.h"
#include "Sim/Combat.h"
#include "Sim/Effects.h"
#include "Sim/Game.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace {
// camera-facing quads and flat quads for one mesh section: uv0 the corner, uv1 = (r, g), uv2 = (b, a),
// uv3 = (kind, light) as ATGMaterials' effects code expects
struct FQuads {
	TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> U0, U1, U2, U3;
	void Vert(const atg::V3& P, float U, float Vv, const float C[3], float A, float Kind, float Light) {
		V.Add(ATG::ToUE(P)); N.Add(FVector::UpVector);
		U0.Add(FVector2D(U, Vv)); U1.Add(FVector2D(C[0], C[1])); U2.Add(FVector2D(C[2], A)); U3.Add(FVector2D(Kind, Light));
	}
	void Quad(const atg::V3& P0, const atg::V3& P1, const atg::V3& P2, const atg::V3& P3, const float C0[3], const float C1[3], float A, float Kind, float Light) {
		const int32 B = V.Num();
		Vert(P0, 0, 0, C0, A, Kind, Light); Vert(P1, 1, 0, C0, A, Kind, Light); Vert(P2, 1, 1, C1, A, Kind, Light); Vert(P3, 0, 1, C1, A, Kind, Light);
		T.Append({ B, B + 1, B + 2, B, B + 2, B + 3 });
	}
	void Flush(UProceduralMeshComponent* M, int32 Section, UMaterialInterface* Mat) {
		if (V.Num() == 0) { M->ClearMeshSection(Section); return; }
		M->CreateMeshSection(Section, V, T, N, U0, U1, U2, U3, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		M->SetMaterial(Section, Mat);
	}
};

UMaterialInstanceDynamic* Std(UObject* Outer, const FLinearColor& Color, double Rough, double Metal, bool bGlass = false, double Opacity = 1) {
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(bGlass ? EATGMat::Glass : EATGMat::Standard), Outer);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	M->SetVectorParameterValue(TEXT("Emissive"), FLinearColor::Black);
	M->SetVectorParameterValue(TEXT("Surface"), FLinearColor((float)Rough, (float)Metal, (float)Opacity, 0.f));
	return M;
}
FLinearColor Hex(uint32 H) { return FLinearColor(FColor((H >> 16) & 255, (H >> 8) & 255, H & 255)); }
}

AATGEffects::AATGEffects() {
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Particles"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->bUseAsyncCooking = true;
}

void AATGEffects::Init() {
	bInit = true;
	for (int32 I = 0; I < 3; I++) {
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(Root);
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(0.f);
		L->SetCastShadows(false);
		L->SetVisibility(false);
		L->RegisterComponent();
		Lights.Add(L);
	}
}

void AATGEffects::Sync(atg::Game* G) {
	if (!G) return;
	atg::Effects* Fx = dynamic_cast<atg::Effects*>(G->effects);
	if (!Fx) return;
	if (!bInit) Init();
	UMaterialInterface* MAlpha = ATGMaterials::Get(EATGMat::FxAlpha);
	UMaterialInterface* MAdd = ATGMaterials::Get(EATGMat::FxAdd);
	// billboard axes from the camera (game axes)
	const atg::V3 Right = G->rig.camQuat.rotate(atg::V3(1, 0, 0)), Up = G->rig.camQuat.rotate(atg::V3(0, 1, 0));
	// the smoke and decal light (effects.js lightColor: ambient plus some sun; a grey stand-in for the sky colours)
	const float Light = (float)FMath::Clamp(0.18 + 1.1 * G->env.dayF * (1 - 0.4 * G->env.cloudCover), 0.12, 1.4);
	auto Pool = [&](const atg::ParticlePool& P, int32 Section, UMaterialInterface* Mat, float Kind, float L) {
		FQuads Q;
		for (const atg::Particle& p : P.parts) {
			if (p.a <= 0.003f) continue;
			const double S = p.size * 0.5, C = std::cos(p.rot), Sn = std::sin(p.rot);
			// (the plane's corners turned by the particle's rotation in the view plane)
			auto Corner = [&](double X, double Y) { const double Rx = X * C - Y * Sn, Ry = X * Sn + Y * C; return atg::V3(p.x, p.y, p.z) + Right * (Rx * S * 2) + Up * (Ry * S * 2); };
			Q.Quad(Corner(-0.5, -0.5), Corner(0.5, -0.5), Corner(0.5, 0.5), Corner(-0.5, 0.5), p.col, p.col, p.a, Kind, L);
		}
		Q.Flush(Mesh, Section, Mat);
	};
	Pool(Fx->alphaPool, 0, MAlpha, 0, Light + 0.03f);
	Pool(Fx->addPool, 1, MAdd, 1, 1);
	Pool(Fx->dotAlpha, 2, MAlpha, 1, Light + 0.05f);
	// decals: flat squares on their surfaces
	{
		FQuads Q;
		static const float White[3] = { 1, 1, 1 };
		for (const atg::Effects::Decal& D : Fx->decals) {
			if (!D.live || D.alpha <= 0) continue;
			const atg::V3 Ax = D.q.rotate(atg::V3(1, 0, 0)) * (D.size * 0.5), Ay = D.q.rotate(atg::V3(0, 1, 0)) * (D.size * 0.5);
			Q.Quad(D.pos - Ax - Ay, D.pos + Ax - Ay, D.pos + Ax + Ay, D.pos - Ax + Ay, White, White, (float)D.alpha, 3.f + D.tile, Light);
		}
		Q.Flush(Mesh, 3, MAlpha);
	}
	if (Fx->skidVersion != SkidVersion) {
		SkidVersion = Fx->skidVersion;
		FQuads Q;
		static const float Dark[3] = { 0.02f, 0.02f, 0.02f };
		for (const atg::Effects::Skid& S : Fx->skids) if (S.alpha > 0) Q.Quad(S.a0, S.a1, S.b1, S.b0, Dark, Dark, (float)S.alpha * 0.75f, 2, 1);
		Q.Flush(Mesh, 4, MAlpha);
	}
	// boat wakes: white on the water, fading over 9 s (the browser game's shader breaks them up with noise and
	// softens the edges; here each quad is a flat white at the trail's average opacity)
	{
		FQuads Q;
		const float White[3] = { 0.95f, 0.95f, 0.95f };
		for (const atg::Effects::Wake& K : Fx->wakes) {
			const double A = K.strength * FMath::Clamp(1 - (Fx->wakeTime - K.born) / 9.0, 0.0, 1.0);
			if (A <= 0.005) continue;
			Q.Quad(K.a0, K.a1, K.b1, K.b0, White, White, (float)(A * 0.5 * 0.45), 2, Light + 0.1f);
		}
		Q.Flush(Mesh, 6, MAlpha);
	}
	// tracers: a short glowing streak moving from the muzzle to where the shot went
	{
		FQuads Q;
		const atg::V3 Cam = G->rig.camPos;
		for (const atg::Effects::Tracer& T : Fx->tracers) {
			if (!T.live) continue;
			const double Life = 0.07, F = FMath::Max(0.0, 1 - T.t / Life);
			if (F <= 0) continue;
			const double S = FMath::Min(1.0, T.t / Life * 1.4);
			const atg::V3 A = T.a.lerp(T.b, FMath::Max(0.0, S - 0.35)), B = T.a.lerp(T.b, S);
			atg::V3 Side = (B - A).cross(Cam - A);
			if (Side.lengthSq() < 1e-9) continue;
			Side = Side.normalized() * 0.02;
			const float C0[3] = { (float)(2.5 * F), (float)(1.8 * F), (float)(0.8 * F) }, C1[3] = { (float)(3 * F), (float)(2.4 * F), (float)(1.2 * F) };
			Q.Quad(A - Side, A + Side, B + Side, B - Side, C0, C1, 1, 2, 1);
		}
		Q.Flush(Mesh, 5, MAdd);
	}
	// the flash lights
	for (int32 I = 0; I < 3 && I < Lights.Num(); I++) {
		const atg::Effects::Flash& F = Fx->lights[I];
		UPointLightComponent* L = Lights[I];
		const bool bOn = F.intensity > 0;
		L->SetVisibility(bOn);
		if (!bOn) continue;
		L->SetWorldLocation(ATG::ToUE(F.pos));
		L->SetLightColor(Hex(F.color));
		L->SetIntensity((float)(F.intensity * 40.0));
		L->SetAttenuationRadius((float)(F.range * 100.0));
	}
	// debris: props and panels as meshes
	AATGWorld* W = AATGWorld::Get(this);
	struct FPiece { UStaticMesh* M; FTransform T; UMaterialInterface* Mat; };
	TArray<FPiece> Pieces;
	if (W) for (const atg::Effects::Debris& D : Fx->debris) {
		if (D.prop >= 0) {
			if (UStaticMesh* M = W->PropMesh(D.prop)) Pieces.Add({ M, ATG::WorldToUE(D.matrix()), nullptr });
			continue;
		}
		const atg::VehicleDef* Def = atg::FindVehicle(D.vehicleType);
		if (!Def || !Def->kind.empty() || !Def->bike.empty()) continue;
		const atg::VehicleModel& Model = atg::BuildVehicleModel(*Def);
		const FATGVehicleMeshes& VM = W->VehicleMeshes(*Def);
		for (int32 I = 0; I < (int32)Model.parts.size() && I < VM.Parts.Num(); I++) {
			const atg::VPart& P = Model.parts[I];
			if (P.name != D.part && !(D.part == "door" && P.name == "doorGlass")) continue;
			UMaterialInterface* Mat = nullptr;
			if (P.mat == atg::EVMat::Paint) {
				const uint32 Col = Def->police ? 0xffffff : D.color;
				TObjectPtr<UMaterialInstanceDynamic>& Pm = PaintMats.FindOrAdd(Col);
				if (!Pm) Pm = Std(this, Hex(Col), 0.32, 0.55);
				Mat = Pm;
			} else if (P.mat == atg::EVMat::Glass) { if (!GlassMat) GlassMat = Std(this, Hex(0x070a0d), 0.04, 0.3, true, 0.8); Mat = GlassMat; }
			else { if (!TrimMat) TrimMat = Std(this, FLinearColor::White, 0.55, 0.35); Mat = TrimMat; }
			Pieces.Add({ VM.Parts[I], ATG::ToUE(D.matrix()), Mat });
		}
	}
	while (Debris.Num() < Pieces.Num()) {
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Root);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetMobility(EComponentMobility::Movable);
		C->RegisterComponent();
		Debris.Add(C);
	}
	for (int32 I = 0; I < Debris.Num(); I++) {
		UStaticMeshComponent* C = Debris[I];
		const bool bOn = I < Pieces.Num();
		C->SetVisibility(bOn);
		if (!bOn) continue;
		if (C->GetStaticMesh() != Pieces[I].M) { C->SetStaticMesh(Pieces[I].M); C->EmptyOverrideMaterials(); }
		if (Pieces[I].Mat && C->GetMaterial(0) != Pieces[I].Mat) C->SetMaterial(0, Pieces[I].Mat);
		C->SetWorldTransform(Pieces[I].T);
	}
	// projectiles
	atg::Combat* Cb = dynamic_cast<atg::Combat*>(G->combat);
	const int32 NShots = Cb ? (int32)Cb->projectiles.size() : 0;
	while (Shots.Num() < NShots) {
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Root);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetMobility(EComponentMobility::Movable);
		C->RegisterComponent();
		Shots.Add(C);
	}
	for (int32 I = 0; I < Shots.Num(); I++) {
		UStaticMeshComponent* C = Shots[I];
		C->SetVisibility(I < NShots);
		if (I >= NShots) continue;
		const atg::Combat::Projectile& P = Cb->projectiles[I];
		UStaticMesh* M = P.type == "molotov" && W ? W->WeaponMesh(TEXT("molotov")) : ShotMesh(P.type == "grenade" ? TEXT("grenade") : FString(UTF8_TO_TCHAR(P.kind.c_str())));
		if (C->GetStaticMesh() != M) C->SetStaticMesh(M);
		atg::Quat Q;
		if (P.type == "rocket") { const atg::V3 D = P.vel.normalized(); Q = atg::Quat::FromUnitVectors(atg::V3(0, 0, 1), D); }
		else Q = atg::Quat::FromEuler(P.spin, 0, 0);
		const double Sc = P.scale;
		C->SetWorldTransform(ATG::ToUE(atg::M4::Compose(P.pos, Q, atg::V3(Sc, Sc, Sc))));
	}
}

// combat.js's projectile meshes, facing +z (the Local axes): rocket, missile, shell, grenade
UStaticMesh* AATGEffects::ShotMesh(const FString& Kind) {
	if (TObjectPtr<UStaticMesh>* M = ShotMeshes.Find(Kind)) return *M;
	atg::MeshBuf B;
	if (Kind == TEXT("grenade")) { B.ColorHex(0x3b4a2a); B.Rough(0.7); B.Add(atg::Geo::Sphere(0.06, 8, 6), atg::Mat4::Identity()); }
	else if (Kind == TEXT("missile")) { B.ColorHex(0xe8e8e2); B.Rough(0.4); B.Add(atg::Geo::Cylinder(0.1, 0.1, 2.4, 8), atg::Mat4::Compose(0, 0, 0, atg::kPi / 2, 0, 0)); }
	else if (Kind == TEXT("shell")) { B.Color(1, 0.65, 0.3); B.Glow(4); B.Add(atg::Geo::Cylinder(0.07, 0.07, 0.9, 6), atg::Mat4::Compose(0, 0, 0, atg::kPi / 2, 0, 0)); }
	else { B.ColorHex(0x3e4a2f); B.Rough(0.6); B.Add(atg::Geo::Cylinder(0.05, 0.05, 0.6, 8), atg::Mat4::Compose(0, 0, 0, atg::kPi / 2, 0, 0)); }
	UStaticMesh* M = ATGMesh::BuildStaticMesh(this, *(TEXT("Shot_") + Kind), TArray<FATGPart>{ { &B, ATGMaterials::Get(EATGMat::VertexLit) } }, EATGAxes::Local);
	ShotMeshes.Add(Kind, M);
	return M;
}
