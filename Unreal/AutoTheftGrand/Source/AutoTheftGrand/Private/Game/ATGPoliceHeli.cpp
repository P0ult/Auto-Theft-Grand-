#include "Game/ATGPoliceHeli.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshUtil.h"
#include "Gen/MeshBuf.h"
#include "Sim/Game.h"
#include "Sim/Police.h"

#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace {
UMaterialInstanceDynamic* Std(UObject* Outer, uint32 Hex, double Rough, double Metal) {
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Standard), Outer);
	M->SetVectorParameterValue(TEXT("Color"), FLinearColor(FColor((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255)));
	M->SetVectorParameterValue(TEXT("Emissive"), FLinearColor::Black);
	M->SetVectorParameterValue(TEXT("Surface"), FLinearColor((float)Rough, (float)Metal, 1.f, 0.f));
	return M;
}
}

AATGPoliceHeli::AATGPoliceHeli() {
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);
	auto Part = [&](const TCHAR* Name) {
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Root);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return C;
	};
	Body = Part(TEXT("Body"));
	Rotor = Part(TEXT("Rotor"));
	TailRotor = Part(TEXT("TailRotor"));
	// the searchlight's cone: rebuilt in world space each frame, toward the target
	Cone = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Cone"));
	Cone->SetupAttachment(Root);
	Cone->SetUsingAbsoluteLocation(true); Cone->SetUsingAbsoluteRotation(true); Cone->SetUsingAbsoluteScale(true);
	Cone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cone->SetCastShadow(false);
	// THREE.SpotLight(0xfff4d8, 0, 90, 0.2, 0.4, 1)
	Spot = CreateDefaultSubobject<USpotLightComponent>(TEXT("Spot"));
	Spot->SetupAttachment(Root);
	Spot->SetMobility(EComponentMobility::Movable);
	Spot->SetUsingAbsoluteRotation(true);
	Spot->SetIntensityUnits(ELightUnits::Candelas);
	Spot->SetIntensity(0.f);
	Spot->SetAttenuationRadius(9000.f);
	Spot->SetOuterConeAngle(FMath::RadiansToDegrees(0.2f));
	Spot->SetInnerConeAngle(FMath::RadiansToDegrees(0.2f * (1 - 0.4f)));
	Spot->SetLightColor(FLinearColor(FColor(0xff, 0xf4, 0xd8)));
	Spot->SetCastShadows(false);
}

void AATGPoliceHeli::Build() {
	bBuilt = true;
	using atg::Mat4;
	using namespace atg::Geo;
	atg::MeshBuf Navy, White, Glass;
	// (white vertices: the colour is each material's)
	Navy.Color(1, 1, 1); White.Color(1, 1, 1); Glass.Color(1, 1, 1);
	Navy.Add(Sphere(1.4, 16, 12), Mat4::Compose(0, 0, 0.3, 0, 0, 0, 1, 0.95, 1.6));
	Glass.Add(Sphere(1.1, 12, 10), Mat4::Compose(0, 0.15, 1.7, 0, 0, 0, 1, 0.85, 0.9));
	White.Add(Cylinder(0.25, 0.4, 5.5, 8), Mat4::Compose(0, 0.3, -3.6, atg::kPi / 2, 0, 0));
	Navy.Add(Box(0.1, 1.2, 0.8), Mat4::Compose(0, 0.8, -6.2));
	for (int S = -1; S <= 1; S += 2) White.Add(Box(0.1, 0.1, 3), Mat4::Compose(S * 0.9, -1.35, 0.3));
	UMaterialInstanceDynamic* NavyMat = Std(this, 0x1b2a4a, 0.4, 0.5);
	UMaterialInstanceDynamic* WhiteMat = Std(this, 0xeeeeee, 0.4, 0.3);
	UMaterialInstanceDynamic* GlassMat = Std(this, 0x111820, 0.05, 0.6);
	Body->SetStaticMesh(ATGMesh::BuildStaticMesh(this, TEXT("HeliBody"), TArray<FATGPart>{ { &Navy, NavyMat }, { &White, WhiteMat }, { &Glass, GlassMat } }, EATGAxes::Local));
	atg::MeshBuf Blades;
	Blades.Color(1, 1, 1);
	Blades.Add(Box(11, 0.05, 0.35), Mat4::Identity());
	Blades.Add(Box(11, 0.05, 0.35), Mat4::Compose(0, 0, 0, 0, atg::kPi / 2, 0));
	Rotor->SetStaticMesh(ATGMesh::BuildStaticMesh(this, TEXT("HeliRotor"), TArray<FATGPart>{ { &Blades, NavyMat } }, EATGAxes::Local));
	atg::MeshBuf Tail;
	Tail.Color(1, 1, 1);
	Tail.Add(Box(0.05, 1.6, 0.18), Mat4::Identity());
	TailRotor->SetStaticMesh(ATGMesh::BuildStaticMesh(this, TEXT("HeliTailRotor"), TArray<FATGPart>{ { &Tail, NavyMat } }, EATGAxes::Local));
	for (int32 I = 0; I < Body->GetNumMaterials(); I++) Body->SetMaterial(I, I == 0 ? NavyMat : I == 1 ? WhiteMat : GlassMat);
	Rotor->SetMaterial(0, NavyMat);
	TailRotor->SetMaterial(0, NavyMat);
}

void AATGPoliceHeli::Sync(atg::Game* G) {
	const atg::PoliceHeli* H = G && G->policeSys ? G->policeSys->heli.get() : nullptr;
	SetActorHiddenInGame(!H);
	if (!H) { Spot->SetIntensity(0.f); Cone->ClearAllMeshSections(); return; }
	if (!bBuilt) Build();
	// group.rotation (pitch, yaw, 0, 'YXZ'); spinning while it comes down
	SetActorTransform(ATG::ToUE(atg::M4::Compose(H->pos, atg::Quat::FromEuler(H->pitch, H->yaw + H->spin, 0, "YXZ"))));
	Rotor->SetRelativeTransform(ATG::LocalToUE(atg::M4::Compose(atg::V3(0, 1.45, 0.3), atg::Quat::FromEuler(0, H->rotor, 0))));
	TailRotor->SetRelativeTransform(ATG::LocalToUE(atg::M4::Compose(atg::V3(0.2, 0.8, -6.3), atg::Quat::FromEuler(H->tailRotor, 0, 0))));
	// the searchlight: ConeGeometry(4, 30, 20, 1, open) hung from the apex, stretched to reach the target
	const atg::V3 D = H->lightAt - H->pos;
	const double L = D.length();
	Spot->SetIntensity((float)(H->spotIntensity * 50.0)); // (the same scale as the player's headlight)
	if (L > 1e-3) Spot->SetWorldRotation(ATG::DirToUE(D).Rotation());
	if (!H->coneOn || L < 1e-3 || H->coneOpacity <= 0) { Cone->ClearAllMeshSections(); return; }
	const atg::V3 F = D * (1 / L);
	atg::V3 Up = std::fabs(F.y) < 0.99 ? atg::V3(0, 1, 0) : atg::V3(1, 0, 0);
	const atg::V3 U = F.cross(Up).normalized(), W = F.cross(U).normalized();
	TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> U0, U1, U2, U3;
	const FLinearColor C(FColor(0xff, 0xf4, 0xd0));
	auto Vert = [&](const atg::V3& P, float Uu, float Vv) {
		V.Add(ATG::ToUE(P)); N.Add(FVector::UpVector); U0.Add(FVector2D(Uu, Vv));
		U1.Add(FVector2D(C.R, C.G)); U2.Add(FVector2D(C.B, (float)H->coneOpacity)); U3.Add(FVector2D(2, 1)); // (flat)
	};
	const int Seg = 20;
	for (int I = 0; I < Seg; I++) {
		const double A0 = I * atg::kTau / Seg, A1 = (I + 1) * atg::kTau / Seg;
		const atg::V3 B0 = H->pos + F * L + (U * std::cos(A0) + W * std::sin(A0)) * 4;
		const atg::V3 B1 = H->pos + F * L + (U * std::cos(A1) + W * std::sin(A1)) * 4;
		const int32 B = V.Num();
		Vert(H->pos, 0.5f, 0); Vert(B0, 0, 1); Vert(B1, 1, 1);
		T.Append({ B, B + 1, B + 2 });
	}
	Cone->CreateMeshSection(0, V, T, N, U0, U1, U2, U3, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	Cone->SetMaterial(0, ATGMaterials::Get(EATGMat::FxAdd));
}
