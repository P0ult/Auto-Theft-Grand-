#include "Game/ATGPlayerController.h"
#include "Game/ATGCar.h"
#include "Game/ATGCharacter.h"
#include "Game/ATGCoords.h"
#include "Game/ATGWorld.h"
#include "Gen/Models.h"

#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"

namespace {
double Damp(double A, double B, double Lambda, double Dt) { return FMath::Lerp(A, B, 1 - FMath::Exp(-Lambda * Dt)); }
double WrapAngle(double A) { A = FMath::Fmod(A + UE_DOUBLE_PI, 2 * UE_DOUBLE_PI); if (A < 0) A += 2 * UE_DOUBLE_PI; return A - UE_DOUBLE_PI; }
double DampAngle(double A, double B, double Lambda, double Dt) { return A + WrapAngle(B - A) * (1 - FMath::Exp(-Lambda * Dt)); }

// (built on first use: EKeys' statics live in another module, so they can't be copied during static init)
#define ATG_KEYS(Name, ...) const TArray<FKey>& Name() { static const TArray<FKey> K = { __VA_ARGS__ }; return K; }
ATG_KEYS(KForward, EKeys::W, EKeys::Up)
ATG_KEYS(KBack, EKeys::S, EKeys::Down)
ATG_KEYS(KLeft, EKeys::A, EKeys::Left)
ATG_KEYS(KRight, EKeys::D, EKeys::Right)
ATG_KEYS(KSprint, EKeys::LeftShift, EKeys::RightShift, EKeys::Gamepad_LeftThumbstick)
ATG_KEYS(KJump, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom)
ATG_KEYS(KHandbrake, EKeys::SpaceBar, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_FaceButton_Right)
ATG_KEYS(KEnter, EKeys::F, EKeys::Enter, EKeys::Gamepad_FaceButton_Top)
ATG_KEYS(KCamera, EKeys::C, EKeys::Gamepad_RightThumbstick)
ATG_KEYS(KMap, EKeys::M, EKeys::Tab, EKeys::Gamepad_Special_Left)
ATG_KEYS(KPause, EKeys::Escape, EKeys::Gamepad_Special_Right)
#undef ATG_KEYS
}

// ==================================================================== camera manager
void AATGCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) {
	AATGPlayerController* PC = Cast<AATGPlayerController>(GetOwningPlayerController());
	if (!PC) { Super::UpdateViewTarget(OutVT, DeltaTime); return; }
	OutVT.POV.Location = PC->CamLocation;
	OutVT.POV.Rotation = PC->CamRotation;
	// the browser game's field of view is vertical; Unreal's is horizontal
	int32 W = 16, H = 9;
	PC->GetViewportSize(W, H);
	const double Aspect = H > 0 ? (double)W / H : 16.0 / 9.0;
	OutVT.POV.FOV = (float)FMath::RadiansToDegrees(2 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(PC->CamFovV) / 2) * Aspect));
}

// ==================================================================== controller
AATGPlayerController::AATGPlayerController() {
	PlayerCameraManagerClass = AATGCameraManager::StaticClass();
	PrimaryActorTick.bTickEvenWhenPaused = true;
	bShouldPerformFullTickWhenPaused = true;
	bShowMouseCursor = false;
}

void AATGPlayerController::BeginPlay() {
	Super::BeginPlay();
	SetInputMode(FInputModeGameOnly());
	World = AATGWorld::Get(this);
}

AATGCar* AATGPlayerController::CurrentCar() const { return Body ? Body->Car : nullptr; }

bool AATGPlayerController::Down(const TArray<FKey>& Keys) const {
	for (const FKey& K : Keys) if (IsInputKeyDown(K)) return true;
	return false;
}
bool AATGPlayerController::Hit(const TArray<FKey>& Keys) const {
	for (const FKey& K : Keys) if (WasInputKeyJustPressed(K)) return true;
	return false;
}

void AATGPlayerController::PlayerTick(float Dt) {
	Super::PlayerTick(Dt);
	if (!World) World = AATGWorld::Get(this);
	MessageTime = FMath::Max(0.f, MessageTime - Dt);
	if (Hit(KPause())) SetPause(!IsPaused());
	if (IsPaused()) return;
	if (Hit(KMap())) bMapOpen = !bMapOpen;
	if (!Body || !World || !World->IsReady()) return;
	Time += Dt;

	// look (input.js: 0.0022 rad per pixel, sticks at 2.6 rad/s in cars, 3.3 on foot; Dy is positive
	// looking down, as in the browser)
	AATGCar* Car = CurrentCar();
	float Mx = 0, My = 0;
	GetInputMouseDelta(Mx, My);
	double Dx = Mx * 0.0022 * MouseSensitivity, Dy = -My * 0.0022 * MouseSensitivity;
	const double Rx = GetInputAnalogKeyState(EKeys::Gamepad_RightX), Ry = GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	const double Rate = (Car ? 2.6 : 3.3) * MouseSensitivity * Dt;
	if (FMath::Abs(Rx) > 0.12) Dx += Rx * Rate;
	if (FMath::Abs(Ry) > 0.12) Dy -= Ry * Rate * 0.7;

	// move
	double MvX = (Down(KRight()) ? 1 : 0) - (Down(KLeft()) ? 1 : 0), MvY = (Down(KForward()) ? 1 : 0) - (Down(KBack()) ? 1 : 0);
	const double Lx = GetInputAnalogKeyState(EKeys::Gamepad_LeftX), Ly = GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	if (FMath::Abs(Lx) > 0.15) MvX = Lx;
	if (FMath::Abs(Ly) > 0.15) MvY = Ly;

	if (Hit(KEnter())) ToggleCar();
	if (WasInputKeyJustPressed(EKeys::V)) SpawnCarHere();
	if (WasInputKeyJustPressed(EKeys::T)) { World->Hours = FMath::Fmod(World->Hours + 1.f, 24.f); ShowMessage(FString::Printf(TEXT("Time: %s"), *World->TimeString()), 1.5f); }
	if (Hit(KCamera())) VehCam++;
	Car = CurrentCar();

	if (Car) {
		// input.js: throttle / brake from keys or triggers, steer left positive
		Car->Throttle = FMath::Max(Down(KForward()) ? 1.f : 0.f, GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis));
		Car->Brake = FMath::Max(Down(KBack()) ? 1.f : 0.f, GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis));
		float St = (Down(KLeft()) ? 1.f : 0.f) - (Down(KRight()) ? 1.f : 0.f);
		if (FMath::Abs(Lx) > 0.05) St = (float)-Lx;
		Car->Steer = St;
		Car->bHandbrake = Down(KHandbrake());
		World->SetFocus(ATG::ToUE(Car->X, Car->Y, Car->Z));
	} else {
		// player.js: movement relative to the camera's heading
		const double CamYaw = Yaw + UE_DOUBLE_PI;
		double Mdx = FMath::Sin(CamYaw) * MvY - FMath::Cos(CamYaw) * MvX;
		double Mdz = FMath::Cos(CamYaw) * MvY + FMath::Sin(CamYaw) * MvX;
		const double L = FMath::Sqrt(Mdx * Mdx + Mdz * Mdz);
		if (L > 1) { Mdx /= L; Mdz /= L; }
		Body->SetMove(Mdx, Mdz, Down(KSprint()));
		if (Hit(KJump())) Body->DoJump();
		World->SetFocus(Body->GetActorLocation());
		if (Body->DrownTime > 3) { Respawn(); ShowMessage(TEXT("You can't swim yet. Back to the safehouse."), 4.f); }
	}
	UpdateCamera(Dt, Dx, Dy);
}

void AATGPlayerController::ToggleCar() {
	if (AATGCar* Car = CurrentCar()) {
		if (FMath::Abs(Car->Speed()) > 8) return; // (too fast to jump out)
		Body->ExitCar();
		Possess(Body);
		return;
	}
	if (AATGCar* Near = Body->FindCarToEnter()) {
		Body->EnterCar(Near);
		Possess(Near);
		VehYawOffset = 0;
	}
}

void AATGPlayerController::SpawnCarHere() {
	if (CurrentCar()) return;
	double X, Y, Z;
	Body->GamePos(X, Y, Z);
	const double H = Body->Heading();
	const std::vector<atg::CarDef>& Defs = atg::CarDefs();
	const atg::CarDef& D = Defs[FMath::RandRange(0, (int32)Defs.size() - 1)];
	const double Ahead = 4 + D.L / 2;
	if (AATGCar::SpawnCar(GetWorld(), FString(D.id), X + FMath::Sin(H) * Ahead, Z + FMath::Cos(H) * Ahead, H + UE_DOUBLE_PI / 2, 0, true)) ShowMessage(FString::Printf(TEXT("%s"), UTF8_TO_TCHAR(D.name)), 2.f);
}

void AATGPlayerController::Respawn() {
	double X, Y, Z, Hd;
	World->PlayerStart(X, Y, Z, Hd);
	Body->DrownTime = 0;
	Body->SetActorLocationAndRotation(ATG::ToUE(X, Y + 1.0, Z), FRotator(0, ATG::HeadingYaw(Hd), 0));
	Body->GetCharacterMovement()->StopMovementImmediately();
}

// ------------------------------------------------------------------ camera.js
void AATGPlayerController::UpdateCamera(float Dt, double Dx, double Dy) {
	const bool bMoved = FMath::Abs(Dx) + FMath::Abs(Dy) > 0.0005;
	if (bMoved) LastLook = Time;
	FVector Pivot; // game axes
	double Side = 0, FovBase = 64;
	if (AATGCar* Car = CurrentCar()) {
		// chase camera
		const double VYaw = Car->Yaw, Speed = Car->Speed();
		if (bMoved) { VehYawOffset = WrapAngle(VehYawOffset - Dx); VehPitch = FMath::Clamp(VehPitch - Dy, -0.9, 0.35); }
		else if (Time - LastLook > 1.2 && FMath::Abs(Speed) > 2) {
			VehYawOffset = DampAngle(VehYawOffset, 0, 2.5, Dt);
			VehPitch = Damp(VehPitch, -0.12, 2, Dt);
		}
		// follow the direction of travel a little when drifting
		const double VelYaw = Car->SpeedAbs() > 3 ? FMath::Atan2(Car->VelX, Car->VelZ) : VYaw;
		const double Drift = WrapAngle(VelYaw - VYaw);
		const double BaseYaw = VYaw + FMath::Clamp(Drift, -0.6, 0.6) * 0.45;
		const double TargetYaw = BaseYaw + VehYawOffset + UE_DOUBLE_PI; // the camera sits behind
		Yaw = DampAngle(Yaw, TargetYaw, bMoved ? 30 : 6, Dt);
		Pitch = Damp(Pitch, VehPitch, 8, Dt);
		const double Size = Car->CamDist();
		const double Dists[3] = { Size, Size * 1.45, Size * 0.6 };
		Dist = Dists[VehCam % 3];
		Pivot = FVector(Car->X, Car->Y + Car->CamHeight(), Car->Z);
		FovBase = 64 + FMath::Clamp(FMath::Abs(Speed) / 45, 0.0, 1.0) * 14;
	} else {
		// orbit on foot
		Yaw = WrapAngle(Yaw - Dx);
		Pitch = FMath::Clamp(Pitch - Dy, -1.35, 0.9);
		double X, Y, Z;
		Body->GamePos(X, Y, Z);
		Pivot = FVector(X, Y + 1.62, Z);
		Dist = 4.3;
		Side = 0.35;
		FovBase = 64 + (Body->IsSprinting() ? 4 : 0);
	}
	// where the camera wants to be
	const double Cp = FMath::Cos(Pitch), Sp = FMath::Sin(Pitch);
	const FVector D(FMath::Sin(Yaw) * Cp, -Sp, FMath::Cos(Yaw) * Cp); // from the pivot towards the camera
	const double Rxv = -FMath::Cos(Yaw), Rzv = FMath::Sin(Yaw);
	FVector Piv = Pivot;
	Piv.X -= Rxv * Side; Piv.Z -= Rzv * Side;
	if (!CurrentCar()) Piv.Y += 0.05;
	// pull in in front of walls
	double Want = Dist;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ATGCamera), false);
	Q.AddIgnoredActor(Body);
	if (AATGCar* Car = CurrentCar()) Q.AddIgnoredActor(Car);
	FHitResult HitR;
	const FVector From = ATG::ToUE(Piv.X, Piv.Y, Piv.Z), To = ATG::ToUE(Piv.X + D.X * (Want + 0.3), Piv.Y + D.Y * (Want + 0.3), Piv.Z + D.Z * (Want + 0.3));
	if (GetWorld()->SweepSingleByChannel(HitR, From, To, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(12.f), Q)) Want = FMath::Max(0.35, HitR.Time * (Want + 0.3) - 0.3);
	CurDist = Want < CurDist ? Want : Damp(CurDist, Want, 4, Dt);
	FVector Pos = Piv + D * CurDist;
	const double Gh = World->GroundHeight(Pos.X, Pos.Z);
	if (Pos.Y < Gh + 0.25) Pos.Y = Gh + 0.25;
	// look at a point ahead of the pivot
	const FVector Look = Piv - D * 10;
	CamLocation = ATG::ToUE(Pos.X, Pos.Y, Pos.Z);
	CamRotation = (ATG::ToUE(Look.X, Look.Y, Look.Z) - CamLocation).Rotation();
	Fov = Damp(Fov, FovBase, 6, Dt);
	CamFovV = (float)Fov;
}
