// How an aircraft or the tank is drawn (aircraftmodels.js and the _updateVisual methods in aircraft.js): the body
// and trim on the body group, props and rotors spinning with a blur disc once they are fast, the gear tucking up
// into the fuselage, the canopy or hatch on its hinge, afterburner flames, nav lights and the strobe, the
// Warhawk's chin gun following the crosshair, and the tank's turret, gun, recoil and road wheels.
#include "Game/ATGCar.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGWorld.h"
#include "Sim/Aircraft.h"
#include "Sim/Game.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace {
double AirSrgb(double C) { return C <= 0.04045 ? C / 12.92 : FMath::Pow((C + 0.055) / 1.055, 2.4); }
FLinearColor AirHex(uint32 H) { return FLinearColor((float)AirSrgb(((H >> 16) & 255) / 255.0), (float)AirSrgb(((H >> 8) & 255) / 255.0), (float)AirSrgb((H & 255) / 255.0), 1.f); }

UMaterialInstanceDynamic* AirStd(UObject* Outer, const FLinearColor& Color, double Rough, double Metal, const FLinearColor& Emissive = FLinearColor::Black, bool bGlass = false, double Opacity = 1) {
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ATGMaterials::Get(bGlass ? EATGMat::Glass : EATGMat::Standard), Outer);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	M->SetVectorParameterValue(TEXT("Emissive"), Emissive);
	M->SetVectorParameterValue(TEXT("Surface"), FLinearColor((float)Rough, (float)Metal, (float)Opacity, 0.f));
	return M;
}

atg::M4 AirAt(const atg::Pt3& P, const atg::Quat& Q = atg::Quat()) { return atg::M4::Compose(atg::V3(P[0], P[1], P[2]), Q); }
}

void AATGCar::BuildAir(atg::Vehicle* V, const atg::AircraftModel& M) {
	Air = &M;
	AATGWorld* W = AATGWorld::Get(this);
	const FString Key = FString::Printf(TEXT("Air_%s_%06x"), UTF8_TO_TCHAR(V->type.c_str()), V->color & 0xffffff);
	// (aircraftmodels.js: matteMaterial or bodyMaterial(white), M.trim, M.glass, airMats)
	UMaterialInstanceDynamic* BodyMat = M.matte ? AirStd(this, FLinearColor::White, 0.6, 0.3) : AirStd(this, FLinearColor::White, 0.32, 0.55);
	UMaterialInstanceDynamic* Matte = AirStd(this, FLinearColor::White, 0.6, 0.3);
	UMaterialInstanceDynamic* Trim = AirStd(this, FLinearColor::White, 0.55, 0.35);
	UMaterialInstanceDynamic* Glass = AirStd(this, AirHex(0x070a0d), 0.04, 0.3, FLinearColor::Black, true, 0.8);
	BurntMat = AirStd(this, AirHex(0x151210), 0.95, 0.2);
	auto Comp = [&](USceneComponent* Parent, const atg::M4& At) {
		USceneComponent* S = NewObject<USceneComponent>(this);
		S->SetupAttachment(Parent);
		S->SetRelativeTransform(ATG::LocalToUE(At));
		S->RegisterComponent();
		return S;
	};
	auto Part = [&](const FString& Name, const atg::MeshBuf& G, USceneComponent* Parent, UMaterialInterface* Mat, bool bBurn = true, bool bShadow = true, const atg::M4& At = atg::M4()) -> UStaticMeshComponent* {
		UStaticMesh* Mesh = W->LocalMesh(Key + TEXT("_") + Name, G);
		if (!Mesh) return nullptr;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Parent);
		C->SetStaticMesh(Mesh);
		C->SetMaterial(0, Mat);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadow);
		C->SetRelativeTransform(ATG::LocalToUE(At));
		C->RegisterComponent();
		if (bBurn) Parts.Add(C); else HideWhenBurnt.Add(C);
		return C;
	};
	// the blur disc: MeshBasicMaterial 0x1a1a1a at 0.18 opacity, both sides (FxAlpha: flat texture, lit 1)
	auto Disc = [&](double R, int32 Segs, const atg::Mat4& Turn) {
		atg::MeshBuf B;
		const FLinearColor L = AirHex(0x1a1a1a);
		B.Color(L.R, L.G, L.B); B.Glow(0.18); B.Signal(2); B.Rough(1);
		B.Add(atg::Geo::Circle(R, Segs), Turn);
		return B;
	};
	UMaterialInterface* FxAlpha = ATGMaterials::Get(EATGMat::FxAlpha);
	UMaterialInterface* FxAdd = ATGMaterials::Get(EATGMat::FxAdd);
	// the body group: the hull, trim, gear
	if (M.parts.size() > 0) Part(TEXT("Body"), M.parts[0], Body, BodyMat);
	if (M.parts.size() > 1) Part(TEXT("Trim"), M.parts[1], Body, Trim);
	if (M.hasGear) { GearComp = Comp(Body, atg::M4()); Part(TEXT("Gear"), M.gear, GearComp, Trim); }
	// props: blades and a disc on a spinning hub
	for (int32 I = 0; I < (int32)M.props.size(); I++) {
		const auto& Pr = M.props[I];
		USceneComponent* Hub = Comp(Body, AirAt(Pr.pos));
		PropComps.Add(Hub);
		Blades.Add(Part(FString::Printf(TEXT("Prop%d"), I), Pr.blade, Hub, Trim));
		Discs.Add(Part(FString::Printf(TEXT("PropDisc%d"), I), Disc(Pr.disc, Pr.disc > 1.5 ? 28 : 24, atg::Mat4::Identity()), Hub, FxAlpha, false, false));
	}
	// rotors: the main rotor turns about y (its disc lies flat), the tail rotor about x (its disc faces sideways)
	if (M.hasRotor) {
		RotorComp = Comp(Body, AirAt(M.rotor.pos));
		Blades.Add(Part(TEXT("Rotor"), M.rotor.blade, RotorComp, Trim));
		Discs.Add(Part(TEXT("RotorDisc"), Disc(M.rotor.disc, 40, atg::Mat4::Compose(0, 0, 0, -atg::kPi / 2, 0, 0)), RotorComp, FxAlpha, false, false));
		if (M.rotor.hasHub) Part(TEXT("Hub"), M.rotor.hub, RotorComp, Trim);
		TailComp = Comp(Body, AirAt(M.tailRotor.pos));
		Blades.Add(Part(TEXT("Tail"), M.tailRotor.blade, TailComp, Trim));
		Discs.Add(Part(TEXT("TailDisc"), Disc(M.tailRotor.disc, 24, atg::Mat4::Compose(0, 0, 0, 0, atg::kPi / 2, 0)), TailComp, FxAlpha, false, false));
	}
	// the door: a canopy (axis x) on the body group or the tank's hatch (axis z) on the turret
	if (M.tank) {
		Turret = Comp(Body, AirAt(M.turretAt));
		Part(TEXT("Turret"), M.turret, Turret, Matte);
		GunPivot = Comp(Turret, AirAt(M.gunPivot));
		TankGun = Comp(GunPivot, atg::M4());
		Part(TEXT("Gun"), M.tankGun, TankGun, Matte);
		AirDoor = Comp(Turret, AirAt(M.doorPivot));
		Part(TEXT("Hatch"), M.hatch, AirDoor, Matte);
		// road wheels: the left set, then the right
		for (int32 S = 0; S < (int32)M.wheelSets.size(); S++) {
			for (const atg::Pt3& P : M.wheelSets[S]) {
				USceneComponent* Spin = Comp(Body, AirAt(P));
				Part(TEXT("Wheel"), M.roadWheel, Spin, Trim);
				WheelSpins.Add(Spin);
			}
			if (S == 0) LeftWheels = WheelSpins.Num();
		}
	} else if (M.doorAxis) AirDoor = Comp(Body, AirAt(M.doorPivot));
	if (M.hasGlass) {
		USceneComponent* Parent = M.glassInDoor && AirDoor ? AirDoor : Body;
		Part(TEXT("Glass"), M.glass, Parent, Glass, false, false, AirAt(M.glassAt));
	}
	// afterburners: additive cones (flame 3.2, 1.3, 0.45 and core 2.4, 2.6, 4.2 at 0.9 opacity)
	for (int32 I = 0; I < (int32)M.flames.size(); I++) {
		const auto& F = M.flames[I];
		atg::MeshBuf B;
		if (F.core) B.Color(2.4, 2.6, 4.2); else B.Color(3.2, 1.3, 0.45);
		B.Glow(0.9); B.Signal(2);
		B.Add(F.mesh, atg::Mat4::Identity());
		USceneComponent* At = Comp(Body, AirAt(F.pos));
		UStaticMeshComponent* C = Part(FString::Printf(TEXT("Flame%d"), I), B, At, FxAdd, false, false);
		if (C) { C->SetVisibility(false); Flames.Add(C); }
	}
	// nav lights: red, green and the strobe (all strobes share one material and flash together)
	for (int32 I = 0; I < (int32)M.lights.size(); I++) {
		const auto& L = M.lights[I];
		atg::MeshBuf B;
		B.Color(1, 1, 1);
		B.Add(atg::Geo::Sphere(L.r, 8, 6), atg::Mat4::Identity());
		UMaterialInstanceDynamic* Mat;
		if (L.kind == 2) { if (!StrobeMat) StrobeMat = AirStd(this, AirHex(0x111111), 0.3, 0); Mat = StrobeMat; }
		else Mat = AirStd(this, AirHex(0x111111), 0.3, 0, L.kind == 0 ? FLinearColor(6.f, 0.2f, 0.1f) : FLinearColor(0.2f, 6.f, 0.6f));
		Part(FString::Printf(TEXT("Light%d_%d"), I, (int32)(L.r * 100)), B, Body, Mat, true, false, AirAt(L.pos));
	}
	// the Warhawk's chin gun
	if (M.hasGun) { ChinGun = Comp(Body, AirAt(M.gunAt)); Part(TEXT("ChinGun"), M.gun, ChinGun, Trim); }
	SyncAir(V);
}

void AATGCar::SyncAir(atg::Vehicle* V) {
	const atg::AircraftModel& M = *Air;
	SetActorTransform(ATG::ToUE(V->groupMatrix()));
	auto Turn = [](USceneComponent* C, const atg::Pt3& P, const atg::Quat& Q) { if (C) C->SetRelativeTransform(ATG::LocalToUE(AirAt(P, Q))); };
	const bool bEx = V->exploded;
	if (atg::Tank* T = dynamic_cast<atg::Tank*>(V)) {
		Body->SetRelativeTransform(ATG::LocalToUE(atg::M4::Compose(atg::V3(), atg::Quat::FromEuler(T->bodyPitch, 0, T->bodyRoll))));
		for (int32 I = 0; I < WheelSpins.Num(); I++) {
			const atg::Pt3& P = I < LeftWheels ? M.wheelSets[0][I] : M.wheelSets[1][I - LeftWheels];
			Turn(WheelSpins[I], P, atg::Quat::FromEuler(I < LeftWheels ? T->wheelL : T->wheelR, 0, 0));
		}
		Turn(Turret, { M.turretAt[0], T->turretY, M.turretAt[2] }, atg::Quat::FromEuler(0, T->turretYaw + T->turretTurn, T->turretTilt));
		Turn(GunPivot, M.gunPivot, atg::Quat::FromEuler(-T->gunPitch, 0, 0));
		Turn(TankGun, { 0, 0, -T->recoil * 0.55 }, atg::Quat());
		Turn(AirDoor, M.doorPivot, atg::Quat::FromEuler(0, 0, T->doorOpen * M.doorMax));
	} else if (atg::AirVehicle* A = dynamic_cast<atg::AirVehicle*>(V)) {
		if (AirDoor) Turn(AirDoor, M.doorPivot, atg::Quat::FromEuler(A->doorAngle(), 0, 0));
		if (StrobeMat) { const float S = (float)A->strobe(); StrobeMat->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(S, S, S)); }
		int32 B = 0;
		if (atg::Plane* P = dynamic_cast<atg::Plane*>(A)) {
			for (int32 I = 0; I < PropComps.Num(); I++, B++) {
				const auto& Pr = M.props[I];
				Turn(PropComps[I], Pr.pos, atg::Quat::FromEuler(0, 0, P->propAngle * Pr.rate));
				if (Blades[B]) Blades[B]->SetVisibility(P->spool < 0.55 || bEx);
				if (Discs[B]) Discs[B]->SetVisibility(P->spool > 0.25 && !bEx);
			}
			// afterburners (flame, core, flame, core: the cores are a little shorter)
			const double Ab = bEx ? 0 : FMath::Clamp((P->spool - 0.55) / 0.45, 0.0, 1.0);
			for (int32 I = 0; I < Flames.Num(); I++) {
				Flames[I]->SetVisibility(Ab > 0.02);
				if (Ab > 0.02) Flames[I]->SetRelativeScale3D(FVector(P->flameScale(I), 0.8 + 0.2 * Ab, 0.8 + 0.2 * Ab));
			}
			if (GearComp) { GearComp->SetVisibility(P->gearK > 0.25, true); Turn(GearComp, { 0, (1 - P->gearK) * 0.9, 0 }, atg::Quat()); }
		} else if (atg::Heli* H = dynamic_cast<atg::Heli*>(A)) {
			Turn(RotorComp, M.rotor.pos, atg::Quat::FromEuler(0, H->rotorAngle, 0));
			Turn(TailComp, M.tailRotor.pos, atg::Quat::FromEuler(H->tailAngle, 0, 0));
			B = PropComps.Num();
			if (Blades.IsValidIndex(B) && Blades[B]) Blades[B]->SetVisibility(H->spool < 0.6 || bEx);
			if (Discs.IsValidIndex(B) && Discs[B]) Discs[B]->SetVisibility(H->spool > 0.3 && !bEx);
			if (Blades.IsValidIndex(B + 1) && Blades[B + 1]) Blades[B + 1]->SetVisibility(H->spool < 0.5 || bEx);
			if (Discs.IsValidIndex(B + 1) && Discs[B + 1]) Discs[B + 1]->SetVisibility(H->spool > 0.3 && !bEx);
			if (ChinGun) Turn(ChinGun, M.gunAt, atg::Quat::FromEuler(-H->gunPitch, H->gunYaw, 0, "YXZ"));
		}
	}
	// burnt out: the paint and trim go black, glass, discs and flames vanish
	if (bEx && !bBurnt) {
		bBurnt = true;
		for (UPrimitiveComponent* C : Parts) if (C) C->SetMaterial(0, BurntMat);
		for (UPrimitiveComponent* C : HideWhenBurnt) if (C) C->SetVisibility(false);
		for (UPrimitiveComponent* C : Discs) if (C) C->SetVisibility(false);
	}
}
