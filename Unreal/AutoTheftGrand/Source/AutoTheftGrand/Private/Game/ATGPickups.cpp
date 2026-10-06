#include "Game/ATGPickups.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshUtil.h"
#include "Game/ATGWorld.h"
#include "Gen/MeshBuf.h"
#include "Sim/Game.h"
#include "Sim/Pickups.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace {
FLinearColor Lin(uint32 H) { return FLinearColor(FColor((H >> 16) & 255, (H >> 8) & 255, H & 255)); }
// pickups.js mat(c, e): MeshStandardMaterial({ color: c, emissive: c * e, roughness: 0.4, metalness: 0.2 })
UMaterialInstanceDynamic* Mat(UObject* Outer, uint32 C, double E) {
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Standard), Outer);
	M->SetVectorParameterValue(TEXT("Color"), Lin(C));
	M->SetVectorParameterValue(TEXT("Emissive"), Lin(C) * (float)E);
	M->SetVectorParameterValue(TEXT("Surface"), FLinearColor(0.4f, 0.2f, 1.f, 0.f));
	return M;
}
}

AATGPickups::AATGPickups() {
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

UStaticMeshComponent* AATGPickups::NewMesh() {
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Root);
	C->SetMobility(EComponentMobility::Movable);
	C->SetUsingAbsoluteLocation(true); C->SetUsingAbsoluteRotation(true); C->SetUsingAbsoluteScale(true);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	return C;
}

UStaticMesh* AATGPickups::PickupMesh(const FString& Kind, const FString& Weapon, float& Scale) {
	Scale = 1;
	if (Kind == TEXT("weapon")) {
		Scale = 1.6f;
		if (AATGWorld* W = AATGWorld::Get(this)) if (UStaticMesh* M = W->WeaponMesh(Weapon)) return M;
	}
	const FString Key = Kind == TEXT("weapon") ? TEXT("weaponbox") : Kind;
	if (TObjectPtr<UStaticMesh>* M = Meshes.Find(Key)) return *M;
	using atg::Mat4;
	using namespace atg::Geo;
	atg::MeshBuf B;
	B.Color(1, 1, 1);
	uint32 C = 0x777777; double E = 0.6;
	if (Kind == TEXT("money")) { C = 0x3a9d23; E = 0.5; for (int I = 0; I < 3; I++) B.Add(Box(0.28, 0.05, 0.14), Mat4::Compose(0, I * 0.055, 0, 0, I * 0.3, 0)); }
	else if (Kind == TEXT("health")) { C = 0xff2a2a; E = 1; B.Add(Box(0.5, 0.16, 0.16), Mat4::Identity()); B.Add(Box(0.16, 0.5, 0.16), Mat4::Identity()); }
	else if (Kind == TEXT("armor")) { C = 0x2a6cff; E = 0.8; B.Add(Box(0.45, 0.55, 0.18), Mat4::Identity()); }
	else if (Kind == TEXT("package")) { C = 0xe8c170; E = 0.6; B.Add(Box(0.4, 0.4, 0.4), Mat4::Identity()); }
	else B.Add(Box(0.3, 0.1, 0.1), Mat4::Identity()); // (a weapon without a model)
	UStaticMesh* M = ATGMesh::BuildStaticMesh(this, *(TEXT("Pickup_") + Key), TArray<FATGPart>{ { &B, Mat(this, C, E) } }, EATGAxes::Local);
	Meshes.Add(Key, M);
	return M;
}

// CircleGeometry(0.6, 20) lying flat, additive, colour x 1.5 at half opacity
UStaticMesh* AATGPickups::GlowMesh(uint32 Color) {
	const FString Key = FString::Printf(TEXT("glow_%06x"), Color);
	if (TObjectPtr<UStaticMesh>* M = Meshes.Find(Key)) return *M;
	const FLinearColor L = Lin(Color) * 1.5f;
	atg::MeshBuf B;
	B.Color(L.R, L.G, L.B); B.Glow(0.5); B.Signal(2); // (FxAdd: alpha in the glow slot, the flat texture)
	B.Add(atg::Geo::Circle(0.6, 20), atg::Mat4::Compose(0, 0.05, 0, -atg::kPi / 2, 0, 0));
	UStaticMesh* M = ATGMesh::BuildStaticMesh(this, *Key, TArray<FATGPart>{ { &B, ATGMaterials::Get(EATGMat::FxAdd) } }, EATGAxes::Local);
	Meshes.Add(Key, M);
	return M;
}

// ConeGeometry(0.35, 0.7, 4), unlit, colour x 2.5 (an emissive standard material on black)
UStaticMesh* AATGPickups::ArrowMesh(uint32 Color) {
	const FString Key = FString::Printf(TEXT("arrow_%06x"), Color);
	if (TObjectPtr<UStaticMesh>* M = Meshes.Find(Key)) return *M;
	atg::MeshBuf B;
	B.Color(1, 1, 1);
	B.Add(atg::Geo::Cone(0.35, 0.7, 4), atg::Mat4::Identity());
	UMaterialInstanceDynamic* Mt = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::Standard), this);
	Mt->SetVectorParameterValue(TEXT("Color"), FLinearColor::Black);
	Mt->SetVectorParameterValue(TEXT("Emissive"), Lin(Color) * 2.5f);
	Mt->SetVectorParameterValue(TEXT("Surface"), FLinearColor(1.f, 0.f, 1.f, 0.f));
	UStaticMesh* M = ATGMesh::BuildStaticMesh(this, *Key, TArray<FATGPart>{ { &B, Mt } }, EATGAxes::Local);
	Meshes.Add(Key, M);
	return M;
}

void AATGPickups::Sync(atg::Game* G) {
	atg::Pickups* P = G ? G->pickupsSys : nullptr;
	if (!P) return;
	for (auto& KV : Pickups) KV.Value.bSeen = false;
	for (auto& KV : Markers) KV.Value.bSeen = false;
	// pickups
	for (const auto& Sp : P->list) {
		const atg::Pickup* Pk = Sp.get();
		const FString Key = FString(UTF8_TO_TCHAR(Pk->kind.c_str())) + TEXT(":") + UTF8_TO_TCHAR(Pk->data.weapon.c_str());
		FATGPickupView& V = Pickups.FindOrAdd((uint64)(UPTRINT)Pk);
		V.bSeen = true;
		float Scale = 1;
		if (!V.Mesh || V.Key != Key) {
			if (!V.Mesh) { V.Mesh = NewMesh(); V.Glow = NewMesh(); V.Glow->SetCastShadow(false); }
			V.Key = Key;
			V.Mesh->SetStaticMesh(PickupMesh(UTF8_TO_TCHAR(Pk->kind.c_str()), UTF8_TO_TCHAR(Pk->data.weapon.c_str()), Scale));
			V.Mesh->SetRelativeScale3D(FVector(Scale));
			V.Glow->SetStaticMesh(GlowMesh(Pk->glow));
		}
		const bool bShow = Pk->visible && !Pk->removed;
		V.Mesh->SetVisibility(bShow); V.Glow->SetVisibility(bShow);
		if (!bShow) continue;
		Scale = (float)V.Mesh->GetRelativeScale3D().X;
		V.Mesh->SetWorldTransform(ATG::ToUE(atg::M4::Compose(Pk->pos + atg::V3(0, Pk->bob, 0), atg::Quat::FromEuler(0, Pk->spin, 0), atg::V3(Scale, Scale, Scale))));
		V.Glow->SetWorldTransform(ATG::ToUE(atg::M4::Compose(Pk->pos, atg::Quat())));
	}
	// markers: the cylinder's glow is the browser game's shader evaluated at each ring of vertices
	for (const auto& Sm : P->markers) {
		const atg::Marker* Mk = Sm.get();
		FATGMarkerView& V = Markers.FindOrAdd((uint64)(UPTRINT)Mk);
		V.bSeen = true;
		if (!V.Cyl) {
			V.Cyl = NewObject<UProceduralMeshComponent>(this);
			V.Cyl->SetupAttachment(Root);
			V.Cyl->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			V.Cyl->SetCastShadow(false);
			V.Cyl->RegisterComponent();
			if (Mk->arrow) { V.Arrow = NewMesh(); V.Arrow->SetStaticMesh(ArrowMesh(Mk->color)); }
		}
		const bool bShow = Mk->visible && !Mk->removed;
		V.Cyl->SetVisibility(bShow);
		if (V.Arrow) V.Arrow->SetVisibility(bShow);
		if (!bShow) continue;
		// CylinderGeometry(r, r, h, 32, 1, open): uv.y is 1 at the top, 0 at the bottom
		const int Seg = 32, Rings = 12;
		const FLinearColor C = Lin(Mk->color) * 1.6f;
		TArray<FVector> Vs; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> U0, U1, U2, U3;
		for (int R = 0; R <= Rings; R++) {
			const double Vv = (double)R / Rings;
			const double A = std::pow(1 - Vv, 1.6) * (0.55 + 0.25 * std::sin(Mk->time * 3 + Vv * 10));
			for (int S = 0; S <= Seg; S++) {
				const double Th = (double)S / Seg * atg::kTau;
				const atg::V3 Pt(Mk->pos.x + std::sin(Th) * Mk->radius, Mk->pos.y + Vv * Mk->height, Mk->pos.z + std::cos(Th) * Mk->radius);
				Vs.Add(ATG::ToUE(Pt)); N.Add(FVector::UpVector);
				U0.Add(FVector2D((double)S / Seg, Vv)); U1.Add(FVector2D(C.R, C.G)); U2.Add(FVector2D(C.B, (float)A)); U3.Add(FVector2D(2, 1));
			}
		}
		for (int R = 0; R < Rings; R++) for (int S = 0; S < Seg; S++) {
			const int32 A = R * (Seg + 1) + S, B = A + Seg + 1;
			T.Append({ A, B, A + 1, B, B + 1, A + 1 });
		}
		V.Cyl->CreateMeshSection(0, Vs, T, N, U0, U1, U2, U3, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		V.Cyl->SetMaterial(0, ATGMaterials::Get(EATGMat::FxAdd));
		if (V.Arrow) V.Arrow->SetWorldTransform(ATG::ToUE(atg::M4::Compose(Mk->pos + atg::V3(0, Mk->arrowY, 0), atg::Quat::FromEuler(atg::kPi, Mk->arrowSpin, 0))));
	}
	// gone from the simulation
	for (auto It = Pickups.CreateIterator(); It; ++It) if (!It.Value().bSeen) {
		if (It.Value().Mesh) It.Value().Mesh->DestroyComponent();
		if (It.Value().Glow) It.Value().Glow->DestroyComponent();
		It.RemoveCurrent();
	}
	for (auto It = Markers.CreateIterator(); It; ++It) if (!It.Value().bSeen) {
		if (It.Value().Cyl) It.Value().Cyl->DestroyComponent();
		if (It.Value().Arrow) It.Value().Arrow->DestroyComponent();
		It.RemoveCurrent();
	}
}
