#include "Game/ATGCar.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshUtil.h"
#include "Game/ATGWorld.h"
#include "Sim/Game.h"

#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace {
double SrgbToLinear(double C) { return C <= 0.04045 ? C / 12.92 : FMath::Pow((C + 0.055) / 1.055, 2.4); }
FLinearColor Hex(uint32 H) { return FLinearColor((float)SrgbToLinear(((H >> 16) & 255) / 255.0), (float)SrgbToLinear(((H >> 8) & 255) / 255.0), (float)SrgbToLinear((H & 255) / 255.0), 1.f); }

UMaterialInstanceDynamic* Std(UObject* Outer, const FLinearColor& Color, double Rough, double Metal, const FLinearColor& Emissive = FLinearColor::Black, bool bGlass = false, double Opacity = 1) {
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(bGlass ? EATGMat::Glass : EATGMat::Standard), Outer);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	M->SetVectorParameterValue(TEXT("Emissive"), Emissive);
	M->SetVectorParameterValue(TEXT("Surface"), FLinearColor((float)Rough, (float)Metal, (float)Opacity, 0.f));
	return M;
}
}

AATGCar::AATGCar() {
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
}

AATGCar* AATGCar::Spawn(UWorld* W, atg::Vehicle* V) {
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AATGCar* C = W->SpawnActor<AATGCar>(AATGCar::StaticClass(), ATG::ToUE(V->pos), FRotator::ZeroRotator, P);
	if (C) C->Build(V);
	return C;
}

void AATGCar::Build(atg::Vehicle* V) {
	Vehicle = atg::Ref<atg::Vehicle>(V);
	Model = V->model;
	AATGWorld* W = AATGWorld::Get(this);
	if (!Model || !W) return; // (bikes, boats and aircraft are drawn by their own views)
	const FATGVehicleMeshes& Meshes = W->VehicleMeshes(V->def);
	const atg::VehicleDef& D = V->def;
	PaintMat = Std(this, Hex(D.police ? (atg::IsSet(D.livery) ? (uint32)D.livery : 0xffffff) : V->color), 0.32, 0.55);
	UMaterialInstanceDynamic* Glass = Std(this, Hex(0x070a0d), 0.04, 0.3, FLinearColor::Black, true, 0.8);
	UMaterialInstanceDynamic* Trim = Std(this, FLinearColor::White, 0.55, 0.35);
	UMaterialInstanceDynamic* Chrome = Std(this, FLinearColor::White, 0.16, 0.95);
	UMaterialInstanceDynamic* WheelMat = Std(this, FLinearColor::White, 0.5, 0.45);
	HeadMat = Std(this, Hex(0xdddddd), 0.1, 0.8, Hex(0x222222));
	TailMat = Std(this, Hex(0x5a0000), 0.2, 0.3, FLinearColor(0.25f, 0, 0));
	BurntMat = Std(this, Hex(0x151210), 0.95, 0.2);
	auto Pivot = [&](const atg::Pt3& P, USceneComponent* Parent) {
		USceneComponent* S = NewObject<USceneComponent>(this);
		S->SetupAttachment(Parent);
		S->SetRelativeLocation(ATG::LocalToUE(P[0], P[1], P[2]));
		S->RegisterComponent();
		return S;
	};
	DoorPivot = Pivot(Model->doorHinge, Body);
	if (Model->hasHood) HoodPivot = Pivot(Model->hoodHinge, Body);
	if (Model->hasTrunk) TrunkPivot = Pivot(Model->trunkHinge, Body);
	for (const auto& R : Model->rearDoors) RearPivots.Add(Pivot(R.pivot, Body));
	for (int32 I = 0; I < (int32)Model->parts.size(); I++) {
		const atg::VPart& P = Model->parts[I];
		USceneComponent* Parent = Body;
		if (P.pivot == "door") Parent = DoorPivot;
		else if (P.pivot == "hood") Parent = HoodPivot;
		else if (P.pivot == "trunk") Parent = TrunkPivot;
		else if (P.pivot == "rear0" && RearPivots.Num() > 0) Parent = RearPivots[0];
		else if (P.pivot == "rear1" && RearPivots.Num() > 1) Parent = RearPivots[1];
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Parent);
		C->SetStaticMesh(Meshes.Parts[I]);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UMaterialInterface* M = Trim;
		switch (P.mat) {
		case atg::EVMat::Paint: M = PaintMat; break;
		case atg::EVMat::Glass: M = Glass; break;
		case atg::EVMat::Chrome: M = Chrome; break;
		case atg::EVMat::Head: M = HeadMat; break;
		case atg::EVMat::Tail: M = TailMat; break;
		case atg::EVMat::Lightbar: {
			UMaterialInstanceDynamic* L = Std(this, Hex(P.color), 0.2, 0, Hex(P.color) * 0.2f);
			if (P.name == "lightRed") RedMat = L; else BlueMat = L;
			M = L;
			break;
		}
		case atg::EVMat::TaxiSign: M = Std(this, Hex(0xffe066), 0.3, 0, FLinearColor(1.2f, 1.0f, 0.3f)); break;
		case atg::EVMat::Beacon: M = Std(this, Hex(0xffa31a), 0.3, 0, FLinearColor(1.6f, 0.7f, 0.05f)); break;
		default: break;
		}
		C->SetMaterial(0, M);
		if (P.mat == atg::EVMat::Glass || P.mat == atg::EVMat::Head || P.mat == atg::EVMat::Tail) C->SetCastShadow(false);
		C->RegisterComponent();
		Parts.Add(C);
	}
	for (const auto& Wh : Model->wheels) {
		USceneComponent* P = NewObject<USceneComponent>(this);
		P->SetupAttachment(Root);
		P->SetRelativeLocation(ATG::LocalToUE(Wh.x, Wh.y, Wh.z));
		P->RegisterComponent();
		USceneComponent* S = NewObject<USceneComponent>(this);
		S->SetupAttachment(P);
		S->RegisterComponent();
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
		M->SetupAttachment(S);
		M->SetStaticMesh(Meshes.Wheel);
		M->SetMaterial(0, WheelMat);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Wh.x < 0) M->SetRelativeRotation(ATG::LocalToUE(atg::M4::Compose(atg::V3(), atg::Quat::FromEuler(0, atg::kPi, 0))).GetRotation());
		M->RegisterComponent();
		WheelPivots.Add(P);
		WheelSpins.Add(S);
	}
	Sync(0);
}

void AATGCar::Sync(float Dt) {
	atg::Vehicle* V = Vehicle.get();
	if (!V || !Model) return;
	SetActorTransform(ATG::ToUE(V->groupMatrix()));
	const double Y = FMath::Clamp(V->bodyY, -0.15, 0.15) + (V->def.hydraulics ? FMath::Clamp(V->hydraulic, -0.1, 0.5) : 0) - (V->flat ? 0.07 : 0);
	Body->SetRelativeTransform(ATG::LocalToUE(atg::M4::Compose(atg::V3(0, Y, 0), atg::Quat::FromEuler(V->bodyPitch, 0, V->bodyRoll))));
	auto Hinge = [](USceneComponent* C, const atg::Pt3& P, const atg::Quat& Q) { if (C) C->SetRelativeTransform(ATG::LocalToUE(atg::M4::Compose(atg::V3(P[0], P[1], P[2]), Q))); };
	Hinge(DoorPivot, Model->doorHinge, atg::Quat::FromEuler(0, V->doorOpen * V->layout.doorMax, 0));
	if (Model->hasHood) Hinge(HoodPivot, Model->hoodHinge, atg::Quat::FromEuler(V->hoodAngle, 0, 0));
	if (Model->hasTrunk) Hinge(TrunkPivot, Model->trunkHinge, atg::Quat::FromEuler(V->trunkAngle, 0, 0));
	for (int32 I = 0; I < RearPivots.Num() && I < (int32)Model->rearDoors.size(); I++) {
		const auto& R = Model->rearDoors[I];
		Hinge(RearPivots[I], R.pivot, atg::Quat::FromEuler(0, -R.side * V->rearDoorOpen[I] * 1.9, 0));
	}
	for (int32 I = 0; I < WheelPivots.Num(); I++) {
		const auto& Wh = Model->wheels[I];
		WheelSpins[I]->SetRelativeRotation(ATG::LocalToUE(atg::M4::Compose(atg::V3(), atg::Quat::FromEuler(V->wheelRot, 0, 0))).GetRotation());
		if (Wh.front) WheelPivots[I]->SetRelativeTransform(ATG::LocalToUE(atg::M4::Compose(atg::V3(Wh.x, Wh.y, Wh.z), atg::Quat::FromEuler(0, V->steerAngle, 0))));
	}
	// torn-off panels, broken glass
	if ((int32)V->detached.size() != Detached || V->glassBroken != bGlassBroken) {
		Detached = (int32)V->detached.size();
		bGlassBroken = V->glassBroken;
		for (int32 I = 0; I < Parts.Num(); I++) {
			const atg::VPart& P = Model->parts[I];
			bool bShow = !V->detached.count(P.name);
			if (P.name == "doorGlass" && V->detached.count("door")) bShow = false;
			if (P.mat == atg::EVMat::Glass && V->glassBroken) bShow = false;
			Parts[I]->SetVisibility(bShow);
		}
	}
	if (V->dentVersion != DentVersion) ApplyDents();
	// burnt out
	if (V->exploded && !bBurnt) {
		bBurnt = true;
		for (int32 I = 0; I < Parts.Num(); I++) if (Model->parts[I].mat == atg::EVMat::Paint) Parts[I]->SetMaterial(0, BurntMat);
		HeadMat->SetVectorParameterValue(TEXT("Emissive"), FLinearColor::Black);
	}
	// lights
	if (!bBurnt) {
		if (V->lightsOn != bLights) {
			bLights = V->lightsOn;
			HeadMat->SetVectorParameterValue(TEXT("Color"), bLights ? FLinearColor::White : Hex(0xdddddd));
			HeadMat->SetVectorParameterValue(TEXT("Emissive"), bLights ? FLinearColor(6.f, 5.6f, 4.6f) : Hex(0x222222));
		}
		const int32 Tail = V->isWrecked() ? 0 : V->braking ? 2 : V->lightsOn ? 1 : 0;
		if (Tail != TailState) {
			TailState = Tail;
			static const uint32 Cols[3] = { 0x5a0000, 0x8a0000, 0xaa0000 };
			static const FLinearColor Ems[3] = { FLinearColor(0.25f, 0, 0), FLinearColor(1.6f, 0.05f, 0.03f), FLinearColor(5.f, 0.1f, 0.05f) };
			TailMat->SetVectorParameterValue(TEXT("Color"), Hex(Cols[Tail]));
			TailMat->SetVectorParameterValue(TEXT("Emissive"), Ems[Tail]);
		}
	}
	if (RedMat && BlueMat) {
		const double T = V->game.time * 7 + V->sirenPhase;
		const bool OnR = V->sirenOn && FMath::Sin(T) > 0, OnB = V->sirenOn && FMath::Sin(T) <= 0;
		RedMat->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(OnR ? 9.f : 0.15f, OnR ? 0.3f : 0.f, 0.f));
		BlueMat->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.f, OnB ? 0.6f : 0.f, OnB ? 12.f : 0.2f));
	}
}

// dents: the painted panels' vertices near each impact pushed in towards the middle of the car
// (vehicle.js dent), drawn from then on as procedural meshes
void AATGCar::ApplyDents() {
	atg::Vehicle* V = Vehicle.get();
	DentVersion = V->dentVersion;
	const double Cy = V->def.H * 0.45;
	for (int32 I = 0; I < Parts.Num(); I++) {
		const atg::VPart& P = Model->parts[I];
		if (P.mat != atg::EVMat::Paint) continue;
		atg::V3 Off(0, 0, 0);
		if (P.pivot == "door") Off = atg::V3(Model->doorHinge[0], Model->doorHinge[1], Model->doorHinge[2]);
		else if (P.pivot == "hood") Off = atg::V3(Model->hoodHinge[0], Model->hoodHinge[1], Model->hoodHinge[2]);
		else if (P.pivot == "trunk") Off = atg::V3(Model->trunkHinge[0], Model->trunkHinge[1], Model->trunkHinge[2]);
		else if (P.pivot == "rear0" && Model->rearDoors.size() > 0) Off = atg::V3(Model->rearDoors[0].pivot[0], Model->rearDoors[0].pivot[1], Model->rearDoors[0].pivot[2]);
		else if (P.pivot == "rear1" && Model->rearDoors.size() > 1) Off = atg::V3(Model->rearDoors[1].pivot[0], Model->rearDoors[1].pivot[1], Model->rearDoors[1].pivot[2]);
		atg::MeshBuf M = P.mesh;
		bool bChanged = false;
		for (const atg::Vehicle::DentRec& Dt : V->dents) {
			const double Rad = 0.9, Amt = FMath::Min(0.16, Dt.strength * 0.006);
			const atg::V3 Lp = Dt.local - Off, Cp = atg::V3(0, Cy, 0) - Off;
			for (size_t K = 0; K < M.Count(); K++) {
				const double X = M.P[K * 3], Y = M.P[K * 3 + 1], Z = M.P[K * 3 + 2];
				const double Dd = atg::Hypot3(X - Lp.x, Y - Lp.y, Z - Lp.z);
				if (Dd >= Rad) continue;
				const double Kk = (1 - Dd / Rad) * Amt;
				const double Dx = Cp.x - X, Dy = Cp.y - Y, Dz = Cp.z - Z;
				double L = atg::Hypot3(Dx, Dy, Dz); if (L == 0) L = 1;
				M.P[K * 3] = (float)(X + Dx / L * Kk); M.P[K * 3 + 1] = (float)(Y + Dy / L * Kk * 0.4); M.P[K * 3 + 2] = (float)(Z + Dz / L * Kk);
				bChanged = true;
			}
		}
		if (!bChanged) continue;
		// smooth normals again (three.js computeVertexNormals)
		std::vector<double> Acc(M.Count() * 3, 0.0);
		for (size_t T = 0; T + 2 < M.I.size(); T += 3) {
			const uint32 A = M.I[T], B = M.I[T + 1], C = M.I[T + 2];
			const atg::V3 Pa(M.P[A * 3], M.P[A * 3 + 1], M.P[A * 3 + 2]), Pb(M.P[B * 3], M.P[B * 3 + 1], M.P[B * 3 + 2]), Pc(M.P[C * 3], M.P[C * 3 + 1], M.P[C * 3 + 2]);
			const atg::V3 N = (Pb - Pa).cross(Pc - Pa);
			for (uint32 Q : { A, B, C }) { Acc[Q * 3] += N.x; Acc[Q * 3 + 1] += N.y; Acc[Q * 3 + 2] += N.z; }
		}
		for (size_t K = 0; K < M.Count(); K++) {
			const atg::V3 N = atg::V3(Acc[K * 3], Acc[K * 3 + 1], Acc[K * 3 + 2]).normalized();
			if (N.lengthSq() > 0) { M.N[K * 3] = (float)N.x; M.N[K * 3 + 1] = (float)N.y; M.N[K * 3 + 2] = (float)N.z; }
		}
		UPrimitiveComponent* Old = Parts[I];
		UProceduralMeshComponent* Pm = Cast<UProceduralMeshComponent>(Old);
		if (!Pm) {
			Pm = NewObject<UProceduralMeshComponent>(this);
			Pm->SetupAttachment(Old->GetAttachParent());
			Pm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pm->RegisterComponent();
			Pm->SetVisibility(Old->IsVisible());
			Old->DestroyComponent();
			Parts[I] = Pm;
		}
		ATGMesh::ToSection(Pm, 0, M, false, EATGAxes::Local);
		Pm->SetMaterial(0, bBurnt ? BurntMat.Get() : PaintMat.Get());
	}
}
