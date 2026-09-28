#include "Game/ATGCar.h"
#include "Game/ATGCharacter.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGWorld.h"
#include "Gen/CityMap.h"
#include "Gen/Models.h"

#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

TArray<AATGCar*> AATGCar::Cars;

namespace {
constexpr double G = 9.81;
double Sign(double V) { return V > 0 ? 1.0 : (V < 0 ? -1.0 : 0.0); }
double Damp(double A, double B, double Lambda, double Dt) { return FMath::Lerp(A, B, 1 - FMath::Exp(-Lambda * Dt)); }
double WrapAngle(double A) { A = FMath::Fmod(A + UE_DOUBLE_PI, 2 * UE_DOUBLE_PI); if (A < 0) A += 2 * UE_DOUBLE_PI; return A - UE_DOUBLE_PI; }
// the normalised tyre curve
double Sat(double X) {
	const double Ax = FMath::Abs(X);
	if (Ax < 1) return X * (1.5 - 0.5 * X * X);
	return Sign(X) * (1 - 0.12 * FMath::Min(1.0, (Ax - 1) / 2.5));
}
double SrgbToLinear(double C) { return C <= 0.04045 ? C / 12.92 : FMath::Pow((C + 0.055) / 1.055, 2.4); }

// 2D separating-axis test between a vehicle's box (axes f, r; half extents hx along r, hz along f) and
// another box (axes a1, a2; half extents bhx, bhz): the push-out normal for the vehicle, the depth and a
// contact point (collision.js satOBB)
bool SatObb(double Cx, double Cz, double Fx, double Fz, double Rx, double Rz, double Hx, double Hz,
	double Bx, double Bz, double A1x, double A1z, double Bhx, double Bhz, double A2x, double A2z,
	double& Nx, double& Nz, double& Depth, double& Px, double& Pz) {
	const double Dx = Bx - Cx, Dz = Bz - Cz;
	const double Axes[4][2] = { { A1x, A1z }, { A2x, A2z }, { Rx, Rz }, { Fx, Fz } };
	double MinOv = 1e30;
	Nx = 0; Nz = 0;
	for (const auto& Ax : Axes) {
		const double Ra = FMath::Abs(Rx * Ax[0] + Rz * Ax[1]) * Hx + FMath::Abs(Fx * Ax[0] + Fz * Ax[1]) * Hz;
		const double Rb = FMath::Abs(A1x * Ax[0] + A1z * Ax[1]) * Bhx + FMath::Abs(A2x * Ax[0] + A2z * Ax[1]) * Bhz;
		const double Dist = Dx * Ax[0] + Dz * Ax[1];
		const double Ov = Ra + Rb - FMath::Abs(Dist);
		if (Ov <= 0) return false;
		if (Ov < MinOv) { MinOv = Ov; const double S = Dist > 0 ? -1 : 1; Nx = Ax[0] * S; Nz = Ax[1] * S; }
	}
	double Best = -1e30;
	Px = Cx; Pz = Cz;
	const int Corners[4][2] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
	for (const auto& C : Corners) {
		const double Qx = Cx + Rx * Hx * C[0] + Fx * Hz * C[1], Qz = Cz + Rz * Hx * C[0] + Fz * Hz * C[1];
		const double D = -(Qx * Nx + Qz * Nz);
		if (D > Best) { Best = D; Px = Qx; Pz = Qz; }
	}
	const double L1 = FMath::Clamp((Px - Bx) * A1x + (Pz - Bz) * A1z, -Bhx, Bhx), L2 = FMath::Clamp((Px - Bx) * A2x + (Pz - Bz) * A2z, -Bhz, Bhz);
	Px = Bx + A1x * L1 + A2x * L2; Pz = Bz + A1z * L1 + A2z * L2;
	Depth = MinOv;
	return true;
}
}

AATGCar::AATGCar() {
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessAI = EAutoPossessAI::Disabled;
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	RootComponent = Pivot;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetupAttachment(Pivot);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_Vehicle);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Box->SetCanEverAffectNavigation(false);
	Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
	Body->SetupAttachment(Pivot);
	Paint = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Paint"));
	Paint->SetupAttachment(Body);
	Paint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trim = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Trim"));
	Trim->SetupAttachment(Body);
	Trim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadLight"));
	HeadLight->SetupAttachment(Body);
	HeadLight->SetIntensityUnits(ELightUnits::Candelas);
	HeadLight->SetIntensity(0.f);
	HeadLight->SetAttenuationRadius(6000.f);
	HeadLight->SetOuterConeAngle(38.f);
	HeadLight->SetInnerConeAngle(18.f);
	HeadLight->SetCastShadows(false);
	HeadLight->SetLightColor(FLinearColor(1.f, 0.93f, 0.8f));
	HeadLight->SetVisibility(false);
}

AATGCar* AATGCar::SpawnCar(UWorld* W, const FString& InType, double InX, double InZ, double InYaw, uint32 Color, bool bInParked) {
	AATGWorld* Wd = AATGWorld::Get(W);
	if (!W || !Wd || !atg::FindCar(TCHAR_TO_UTF8(*InType))) return nullptr;
	const double Gy = Wd->GroundHeight(InX, InZ);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AATGCar* C = W->SpawnActor<AATGCar>(AATGCar::StaticClass(), ATG::ToUE(InX, Gy, InZ), FRotator(0, ATG::HeadingYaw(InYaw), 0), P);
	if (!C) return nullptr;
	C->World = Wd;
	C->X = InX; C->Y = Gy; C->Z = InZ; C->Yaw = InYaw;
	C->bParked = bInParked;
	C->Setup(InType, Color);
	return C;
}

void AATGCar::Setup(const FString& InType, uint32 Color) {
	Type = InType;
	Def = atg::FindCar(TCHAR_TO_UTF8(*InType));
	const atg::CarDef& D = *Def;
	Mass = D.mass;
	Inertia = D.mass * (D.L * D.L + D.W * D.W) / 12;
	A = D.wheelbase * 0.5; B = D.wheelbase * 0.5;
	const FATGCarMeshes& M = World->CarMeshes(InType);
	Paint->SetStaticMesh(M.Paint);
	Trim->SetStaticMesh(M.Trim);
	if (!Color && !D.colors.empty()) Color = D.colors[FMath::RandRange(0, (int32)D.colors.size() - 1)];
	PaintMat = UMaterialInstanceDynamic::Create(ATGMaterials::Get(EATGMat::VertexLit), this);
	PaintMat->SetVectorParameterValue(TEXT("Tint"), FLinearColor((float)SrgbToLinear(((Color >> 16) & 255) / 255.0), (float)SrgbToLinear(((Color >> 8) & 255) / 255.0), (float)SrgbToLinear((Color & 255) / 255.0)));
	Paint->SetMaterial(0, PaintMat);
	// the model's own measurements (seat, door, centre of gravity)
	const atg::CarModel Model = atg::BuildCarModel(D);
	SeatX = Model.seat[0]; SeatY = Model.seat[1]; SeatZ = Model.seat[2];
	DoorX = Model.door[0]; DoorZ = Model.door[1];
	CgH = Model.cgH;
	Box->SetBoxExtent(FVector(D.L * 50, D.W * 50, D.H * 50));
	Box->SetRelativeLocation(FVector(0, 0, D.H * 50));
	for (int32 I = 0; I < 4; I++) {
		const bool bFront = I < 2, bLeft = (I % 2) == 0;
		USceneComponent* P = NewObject<USceneComponent>(this);
		P->SetupAttachment(Pivot);
		P->SetRelativeLocation(ATG::LocalToUE((bLeft ? 1 : -1) * D.track / 2, D.wheelR, (bFront ? 1 : -1) * D.wheelbase / 2));
		P->RegisterComponent();
		UStaticMeshComponent* Wh = NewObject<UStaticMeshComponent>(this);
		Wh->SetupAttachment(P);
		Wh->SetStaticMesh(M.Wheel);
		Wh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wh->RegisterComponent();
		WheelPivots.Add(P);
		Wheels.Add(Wh);
	}
	HeadLight->SetRelativeLocationAndRotation(ATG::LocalToUE(0, 0.75, D.L / 2 + 0.1), FRotator(-7, 0, 0));
	UpdateVisual(0);
	Cars.Add(this);
}

void AATGCar::EndPlay(const EEndPlayReason::Type Reason) {
	Cars.Remove(this);
	Super::EndPlay(Reason);
}

double AATGCar::HalfW() const { return Def ? Def->W / 2 : 1; }
double AATGCar::HalfL() const { return Def ? Def->L / 2 : 2; }
double AATGCar::CamDist() const { return Def && Def->camDist > 0 ? Def->camDist : 7.5; }
double AATGCar::CamHeight() const { return Def && Def->camHeight > 0 ? Def->camHeight : 1.6; }

FVector AATGCar::DoorWorld() const {
	const double S = FMath::Sin(Yaw), C = FMath::Cos(Yaw);
	return FVector(X + DoorX * C + DoorZ * S, Y, Z - DoorX * S + DoorZ * C);
}
FVector AATGCar::SeatLocal() const { return FVector(SeatX, SeatY, SeatZ); }

void AATGCar::PutIn(AATGCharacter* Who) {
	Driver = Who;
	bParked = false;
}

void AATGCar::TakeOut(AATGCharacter* Who) {
	if (Driver == Who) Driver = nullptr;
	Throttle = 0; Brake = 0; Steer = 0; bHandbrake = false;
}

void AATGCar::Damage(double Amount) {
	Health = FMath::Max(0.0, Health - Amount);
}

// ------------------------------------------------------------------ simulation (vehicle.js update)
void AATGCar::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	if (!Def || !World) return;
	const double Dt = FMath::Min((double)DeltaSeconds, 1.0 / 20);
	Time += Dt;
	const bool bWrecked = Health <= 0 || bSunk;
	// a car nobody has touched sleeps
	if (!Driver && bParked && SpeedAbs() < 0.01 && !bAirborne && Time > 0.5) return;
	if (bWrecked || !Driver) { Throttle = 0; Steer *= 0.9f; if (!Driver) { Brake = 0; bHandbrake = true; } }
	// steering
	const double Spd = FMath::Abs(Speed());
	const double MaxSteer = Def->steer / (1 + Spd / 20);
	double Target = Steer * MaxSteer;
	// mild self-aligning / countersteer assist when sliding and no input
	if (FMath::Abs(Steer) < 0.05 && Spd > 5 && Driver) {
		const double VelYaw = FMath::Atan2(VelX, VelZ);
		const double Slip = WrapAngle(VelYaw - Yaw);
		if (Speed() > 0) Target = FMath::Clamp(Slip * 0.6, -MaxSteer, MaxSteer);
	}
	SteerAngle = Damp(SteerAngle, Target, Steer == 0 ? 10 : 7, Dt);
	// handbrake grip dynamics
	if (bHandbrake) RearGrip = FMath::Max(0.36, RearGrip - Dt * 4);
	else RearGrip = FMath::Min(1.0, RearGrip + Dt * 1.1);
	const int32 Steps = Dt > 1.0 / 45 ? 3 : 2;
	for (int32 I = 0; I < Steps; I++) Step(Dt / Steps);
	AfterPhysics(Dt);
	UpdateVisual(Dt);
	// back to sleep once it has come to rest with nobody in it
	if (!Driver && SpeedAbs() < 0.05 && FMath::Abs(R) < 0.05 && !bAirborne) { Idle += Dt; if (Idle > 2) bParked = true; }
	else Idle = 0;
}

void AATGCar::Step(double H) {
	const atg::CarDef& D = *Def;
	const double M = Mass;
	const double S = FMath::Sin(Yaw), C = FMath::Cos(Yaw);
	const double Fx = S, Fz = C, Rx = -C, Rz = S;
	const double Vx = VelX, Vz = VelZ;
	const double VLong = Vx * Fx + Vz * Fz;
	const double VLat = Vx * Rx + Vz * Rz;
	const double Wb = A + B;
	const double Mu = D.grip * SurfaceGrip;

	if (bAirborne) {
		VelX -= Vx * 0.02 * H; VelZ -= Vz * 0.02 * H;
		R *= 1 - 0.3 * H;
		Yaw += R * H;
		X += VelX * H; Z += VelZ * H;
		Vy -= G * H;
		Y += Vy * H;
		return;
	}
	// loads (longitudinal weight transfer)
	const double Hcg = 0.55;
	double Nf = M * G * B / Wb - M * AxLong * Hcg / Wb;
	double Nr = M * G * A / Wb + M * AxLong * Hcg / Wb;
	Nf = FMath::Max(Nf, M * G * 0.15); Nr = FMath::Max(Nr, M * G * 0.15);
	// front tyre kinematics
	const double Dl = SteerAngle;
	const double Cd = FMath::Cos(Dl), Sd = FMath::Sin(Dl);
	const double VFlat = VLat - R * A;
	const double FLong = VLong * Cd - VFlat * Sd;
	const double FLat = VFlat * Cd + VLong * Sd;
	const double AlphaF = FMath::Atan2(FLat, FMath::Max(FMath::Abs(FLong), 1.6));
	const double VRlat = VLat + R * B;
	const double AlphaR = FMath::Atan2(VRlat, FMath::Max(FMath::Abs(VLong), 1.6));
	const double Peak = 0.11;
	// longitudinal forces: engine (forward / reverse) and brakes
	const double Thr = Health <= 0 ? 0 : Throttle;
	const double Brk = Brake;
	double DriveF = 0;
	if (Thr > 0 && VLong > -0.8) { const double K = FMath::Clamp(VLong / D.top, 0.0, 1.0); DriveF = Thr * D.force * (1 - 0.92 * K * K); }
	if (Brk > 0 && VLong < 0.8 && VLong > -12) DriveF = -Brk * D.force * 0.55;
	double BrakeF = 0;
	if (Brk > 0 && VLong > 0.8) BrakeF = Brk * D.brake;
	if (Thr > 0 && VLong < -0.8) BrakeF = Thr * D.brake * 0.8;
	double RearDrive = 0, FrontDrive = 0;
	if (D.drive == atg::EDrive::RWD) RearDrive = DriveF;
	else if (D.drive == atg::EDrive::FWD) FrontDrive = DriveF;
	else { RearDrive = DriveF * 0.6; FrontDrive = DriveF * 0.4; }
	const double BSign = FMath::Abs(VLong) > 0.3 ? Sign(VLong) : 0;
	double FxF = FrontDrive - BSign * BrakeF * 0.6;
	double FxR = RearDrive - BSign * BrakeF * 0.4;
	// the handbrake locks the rear
	const double MuR = Mu * RearGrip;
	if (bHandbrake) {
		FxR = -BSign * Mu * Nr * 0.75;
		if (FMath::Abs(VLong) < 0.5) { VelX *= 1 - 3 * H; VelZ *= 1 - 3 * H; }
	}
	// lateral forces from the tyre curve
	double FyF = -Mu * Nf * Sat(AlphaF / Peak);
	double FyR = -MuR * Nr * Sat(AlphaR / (Peak * (bHandbrake ? 1.4 : 1)));
	// traction circles
	const double CapF = Mu * Nf, CapR = MuR * Nr;
	const double MF = FMath::Sqrt(FxF * FxF + FyF * FyF);
	if (MF > CapF) { FxF *= CapF / MF; FyF *= CapF / MF; }
	const double MR = FMath::Sqrt(FxR * FxR + FyR * FyR);
	Wheelspin = 0;
	if (MR > CapR) {
		const double K = CapR / MR;
		if (FMath::Abs(RearDrive) > CapR * 0.9) Wheelspin = FMath::Clamp((FMath::Abs(RearDrive) - CapR * 0.9) / CapR, 0.0, 1.0);
		FxR *= K; FyR *= K;
	}
	// world forces (the front wheels' forward / right vectors are turned by the steering)
	const double Wfx = Fx * Cd - Rx * Sd, Wfz = Fz * Cd - Rz * Sd;
	const double Wrx = Rx * Cd + Fx * Sd, Wrz = Rz * Cd + Fz * Sd;
	double FX = FxF * Wfx + FyF * Wrx + FxR * Fx + FyR * Rx;
	double FZ = FxF * Wfz + FyF * Wrz + FxR * Fz + FyR * Rz;
	// drag and rolling resistance
	const double Sp = FMath::Sqrt(Vx * Vx + Vz * Vz);
	const double Cdrag = 0.08 * D.force / (D.top * D.top);
	FX -= Vx * Sp * Cdrag + Vx * 18 * (M / 1500);
	FZ -= Vz * Sp * Cdrag + Vz * 18 * (M / 1500);
	// low speed lateral stick (parking)
	if (FMath::Abs(VLong) < 3 && !bHandbrake) {
		const double K = (1 - FMath::Abs(VLong) / 3) * M * 4;
		FX -= VLat * Rx * K; FZ -= VLat * Rz * K;
	}
	// torque
	auto Cross2 = [](double Ux, double Uz, double Wx, double Wz) { return Uz * Wx - Ux * Wz; };
	double Tau = Cross2(Fx * A, Fz * A, FxF * Wfx + FyF * Wrx, FxF * Wfz + FyF * Wrz) + Cross2(-Fx * B, -Fz * B, FxR * Fx + FyR * Rx, FxR * Fz + FyR * Rz);
	Tau -= R * Inertia * (Sp < 4 ? 3.5 : 0.4);
	// integrate
	const double Ax = FX / M, Az = FZ / M;
	VelX += Ax * H; VelZ += Az * H;
	R += Tau / Inertia * H;
	AxLong = Damp(AxLong, Ax * Fx + Az * Fz, 12, H);
	AyLat = Damp(AyLat, Ax * Rx + Az * Rz, 12, H);
	if (Thr == 0 && Brk == 0 && FMath::Sqrt(VelX * VelX + VelZ * VelZ) < 0.25) { VelX *= 0.8; VelZ *= 0.8; R *= 0.8; }
	Yaw += R * H;
	X += VelX * H;
	Z += VelZ * H;
	SlipRear = FMath::Abs(VRlat);
}

// the highest drivable surface under a point of the car (its own frame) not far above YRef
double AATGCar::Surface(double Lx, double Lz, double YRef) const {
	const double S = FMath::Sin(Yaw), C = FMath::Cos(Yaw);
	const double Wx = X + Lx * C + Lz * S, Wz = Z - Lx * S + Lz * C;
	TArray<FHitResult> Hits;
	FCollisionQueryParams P(SCENE_QUERY_STAT(ATGCarGround), true, this);
	GetWorld()->LineTraceMultiByObjectType(Hits, ATG::ToUE(Wx, YRef + 1.4, Wz), ATG::ToUE(Wx, YRef - 60, Wz), FCollisionObjectQueryParams(ECC_WorldStatic), P);
	for (const FHitResult& H : Hits) {
		const UPrimitiveComponent* Comp = H.GetComponent();
		if (Comp && Comp->ComponentHasTag(TEXT("ATGCircles"))) continue;
		return H.ImpactPoint.Z / ATG::M;
	}
	// (outside the streamed collision) the generator's own ground
	return World->GroundHeight(Wx, Wz);
}

void AATGCar::AfterPhysics(double Dt) {
	const atg::CarDef& D = *Def;
	const atg::CityMap* Map = World->Map();
	const double S = FMath::Sin(Yaw), C = FMath::Cos(Yaw);
	const double Hw = D.track / 2, Zf = D.wheelbase / 2, Zr = -D.wheelbase / 2;
	const double YRef = Y + (bAirborne ? 0.6 : 1.3);
	const double H0 = Surface(Hw, Zf, YRef), H1 = Surface(-Hw, Zf, YRef), H2 = Surface(Hw, Zr, YRef), H3 = Surface(-Hw, Zr, YRef);
	const double HF = (H0 + H1) / 2, HR = (H2 + H3) / 2, HL = (H0 + H2) / 2, HRt = (H1 + H3) / 2;
	const double TargetY = (HF + HR) / 2;
	SurfaceGrip = (Map && Map->IsOnRoad(X, Z)) || FMath::Abs(TargetY - 0.15) < 0.01 ? 1 : 0.85;
	// cliffs and steep ground block
	const bool bFwdBlock = (HF - Y > 0.9 && Speed() > 0) || (HR - Y > 0.9 && Speed() < 0);
	if (bFwdBlock && !bAirborne) {
		const double Vl = Speed();
		VelX -= S * Vl * 1.3; VelZ -= C * Vl * 1.3;
		X -= S * Sign(Vl) * 0.1; Z -= C * Sign(Vl) * 0.1;
		if (FMath::Abs(Vl) > 8) Damage(FMath::Abs(Vl) * 8);
	}
	if (bAirborne) {
		if (Y <= TargetY) {
			Y = TargetY;
			const double Impact = -Vy;
			bAirborne = false;
			BodyYV -= Impact * 0.35;
			if (Impact > 9) Damage((Impact - 9) * 25);
			Vy = 0;
		}
	} else {
		const double Dy = TargetY - Y;
		if (Dy < -0.4) {
			// the ground dropped away: launch with the previous vertical speed
			bAirborne = true;
			Vy = FMath::Max(GroundVy, -2.0);
		} else {
			if (FMath::Abs(Dy) < 0.25) GroundVy = Damp(GroundVy, Dy / FMath::Max(Dt, 1e-3), 20, Dt);
			else { GroundVy = 0; BodyYV += Dy > 0 ? 1.5 : -0.5; }
			Y = TargetY;
		}
	}
	const double TgtPitch = FMath::Atan2(HF - HR, D.wheelbase);
	const double TgtRoll = FMath::Atan2(HL - HRt, D.track);
	if (!bAirborne) { GroundPitch = Damp(GroundPitch, TgtPitch, 12, Dt); GroundRoll = Damp(GroundRoll, TgtRoll, 12, Dt); }
	else GroundPitch = Damp(GroundPitch, FMath::Clamp(Vy * 0.03, -0.4, 0.3), 1.5, Dt);

	// ---- walls: rays from the middle of the car out to its outline, at bumper and window height
	const double Hx = D.W / 2, Hz = D.L / 2;
	const double Fx = S, Fz = C, Rx = C, Rz = -S; // (collision.js: r = (cos, -sin) is the car's left here)
	struct FContact { double Nx, Nz, Depth, Px, Pz; };
	TArray<FContact> Contacts;
	{
		const double Pts[12][2] = { { Hx, Hz }, { -Hx, Hz }, { Hx, -Hz }, { -Hx, -Hz }, { 0, Hz }, { 0, -Hz }, { Hx, 0 }, { -Hx, 0 }, { Hx, Hz / 2 }, { -Hx, Hz / 2 }, { Hx, -Hz / 2 }, { -Hx, -Hz / 2 } };
		FCollisionQueryParams P(SCENE_QUERY_STAT(ATGCarWalls), true, this);
		const FCollisionObjectQueryParams O(ECC_WorldStatic);
		for (const double Hgt : { 0.5, 1.0 }) {
			const FVector From = ATG::ToUE(X, Y + Hgt, Z);
			for (const auto& Pt : Pts) {
				const double Ex = X + Rx * Pt[0] + Fx * Pt[1], Ez = Z + Rz * Pt[0] + Fz * Pt[1];
				TArray<FHitResult> Hits;
				GetWorld()->LineTraceMultiByObjectType(Hits, From, ATG::ToUE(Ex, Y + Hgt, Ez), O, P);
				for (const FHitResult& Hit : Hits) {
					const UPrimitiveComponent* Comp = Hit.GetComponent();
					if (Comp && Comp->ComponentHasTag(TEXT("ATGCircles"))) continue;
					if (FMath::Abs(Hit.ImpactNormal.Z) > 0.6) break; // ground-ish: not a wall
					double Nx = Hit.ImpactNormal.X, Nz = Hit.ImpactNormal.Y;
					const double Nl = FMath::Sqrt(Nx * Nx + Nz * Nz);
					if (Nl < 1e-3) break;
					Nx /= Nl; Nz /= Nl;
					const double Hx2 = Hit.ImpactPoint.X / ATG::M, Hz2 = Hit.ImpactPoint.Y / ATG::M;
					const double Depth = (Ex - Hx2) * -Nx + (Ez - Hz2) * -Nz;
					if (Depth > 0) {
						// one contact per wall (the deepest)
						bool bMerged = false;
						for (FContact& Ct : Contacts) if (Ct.Nx * Nx + Ct.Nz * Nz > 0.9) { if (Depth > Ct.Depth) Ct = { Nx, Nz, Depth, Hx2, Hz2 }; bMerged = true; break; }
						if (!bMerged) Contacts.Add({ Nx, Nz, Depth, Hx2, Hz2 });
					}
					break;
				}
			}
		}
	}
	for (const FContact& Ct : Contacts) ResolveContact(Ct.Nx, Ct.Nz, FMath::Min(Ct.Depth, 1.0), Ct.Px, Ct.Pz, false);

	// ---- street furniture, trunks and rocks (circles)
	TArray<int32> Near;
	World->QueryCircles(X, Z, FMath::Max(Hx, Hz) + 2, Near);
	for (int32 I : Near) {
		FATGCircle& O = World->Circle(I);
		if (O.bBroken || Y > O.Top || Y + 1.5 < O.Y0) continue;
		const double Lx = O.X - X, Lz = O.Z - Z;
		const double Lr = Lx * Rx + Lz * Rz, Lf = Lx * Fx + Lz * Fz;
		const double Qr = FMath::Clamp(Lr, -Hx, Hx), Qf = FMath::Clamp(Lf, -Hz, Hz);
		const double Wx = X + Rx * Qr + Fx * Qf, Wz = Z + Rz * Qr + Fz * Qf;
		double Dx = Wx - O.X, Dz = Wz - O.Z;
		const double D2 = Dx * Dx + Dz * Dz;
		if (D2 >= O.R * O.R) continue;
		const double Dd = FMath::Sqrt(D2) > 0 ? FMath::Sqrt(D2) : 1e-4;
		if (D2 < 1e-8) { Dx = X - O.X; Dz = Z - O.Z; }
		const double Dl = FMath::Max(FMath::Sqrt(Dx * Dx + Dz * Dz), 1e-6);
		if (O.bBreakable && SpeedAbs() > 2.5) {
			// smash through it
			World->BreakProp(I);
			VelX *= 0.9; VelZ *= 0.9;
			Damage(SpeedAbs() * 1.5);
			continue;
		}
		ResolveContact(Dx / Dl, Dz / Dl, O.R - Dd, Wx, Wz, false);
	}

	// ---- other cars (oriented boxes)
	for (AATGCar* Other : Cars) {
		if (Other == this || !Other->Def) continue;
		const double Ox = Other->X, Oz = Other->Z;
		if (FMath::Square(Ox - X) + FMath::Square(Oz - Z) > 100 || FMath::Abs(Other->Y - Y) > 2) continue;
		const double Os = FMath::Sin(Other->Yaw), Oc = FMath::Cos(Other->Yaw);
		double Nx, Nz, Depth, Px, Pz;
		if (!SatObb(X, Z, Fx, Fz, Rx, Rz, Hx, Hz, Ox, Oz, Oc, -Os, Other->HalfW(), Other->HalfL(), Os, Oc, Nx, Nz, Depth, Px, Pz)) continue;
		// share the push-out by mass, and exchange momentum along the normal
		const double Mt = Mass + Other->Mass;
		X += Nx * Depth * Other->Mass / Mt; Z += Nz * Depth * Other->Mass / Mt;
		Other->X -= Nx * Depth * Mass / Mt; Other->Z -= Nz * Depth * Mass / Mt;
		const double Vn = (VelX - Other->VelX) * Nx + (VelZ - Other->VelZ) * Nz;
		if (Vn >= 0) continue;
		const double J = -(1 + 0.2) * Vn / (1 / Mass + 1 / Other->Mass);
		VelX += J * Nx / Mass; VelZ += J * Nz / Mass;
		Other->VelX -= J * Nx / Other->Mass; Other->VelZ -= J * Nz / Other->Mass;
		const double Rn = (Pz - Z) * Nx - (Px - X) * Nz;
		R += Rn * J / Inertia * 0.5;
		Other->bParked = false;
		const double Impact = -Vn;
		if (Impact > 3) { Damage((Impact - 3) * 10); Other->Damage((Impact - 3) * 10); }
	}

	// ---- water
	if (Map && !bSunk) {
		const double Wl = Map->WaterLevel(X, Z);
		if (Wl - Map->GroundHeight(X, Z) > 1.0 && Y < Wl - 0.3) bSunk = true;
	}
	if (bSunk) {
		VelX *= 1 - Dt * 1.5; VelZ *= 1 - Dt * 1.5;
		Y = FMath::Max(Map ? Map->GroundHeight(X, Z) : Y, Y - Dt * 0.6);
		bAirborne = false;
	}
	WheelRot += Speed() * Dt / D.wheelR;
}

// push out of a wall and bounce off it (vehicle.js _resolveStatic)
void AATGCar::ResolveContact(double Nx, double Nz, double Depth, double Px, double Pz, bool bSoft) {
	X += Nx * Depth;
	Z += Nz * Depth;
	const double Qx = Px - X, Qz = Pz - Z;
	const double Vpx = VelX + R * Qz, Vpz = VelZ - R * Qx;
	const double Vn = Vpx * Nx + Vpz * Nz;
	if (Vn >= 0) return;
	const double E = bSoft ? 0.05 : 0.18;
	const double Rn = Qz * Nx - Qx * Nz;
	const double J = -(1 + E) * Vn / (1 / Mass + Rn * Rn / Inertia);
	VelX += J * Nx / Mass;
	VelZ += J * Nz / Mass;
	R += Rn * J / Inertia;
	// friction along the wall
	const double Tx = -Nz, Tz = Nx;
	const double Vt = Vpx * Tx + Vpz * Tz;
	const double Jt = FMath::Clamp(-Vt * Mass * 0.25, -J * 0.4, J * 0.4);
	VelX += Jt * Tx / Mass; VelZ += Jt * Tz / Mass;
	const double Impact = -Vn;
	if (Impact > 3) Damage((Impact - 3) * 14);
}

// ------------------------------------------------------------------ pose (vehicle.js _updateVisual)
void AATGCar::UpdateVisual(double Dt) {
	SetActorLocationAndRotation(ATG::ToUE(X, Y, Z), FRotator(FMath::RadiansToDegrees(GroundPitch), ATG::HeadingYaw(Yaw), FMath::RadiansToDegrees(GroundRoll)));
	// body on its springs
	const double Tp = FMath::Clamp(AxLong * 0.008, -0.07, 0.07);
	const double Tr = FMath::Clamp(-AyLat * 0.011, -0.09, 0.09);
	const double K = 90, Cdamp = 11;
	BodyPitchV += ((Tp - BodyPitch) * K - BodyPitchV * Cdamp) * Dt;
	BodyRollV += ((Tr - BodyRoll) * K - BodyRollV * Cdamp) * Dt;
	BodyYV += ((-BodyY) * 120 - BodyYV * 9) * Dt;
	BodyPitch += BodyPitchV * Dt;
	BodyRoll += BodyRollV * Dt;
	BodyY += BodyYV * Dt;
	Body->SetRelativeLocationAndRotation(FVector(0, 0, FMath::Clamp(BodyY, -0.15, 0.15) * ATG::M), FRotator(-FMath::RadiansToDegrees(BodyPitch), 0, FMath::RadiansToDegrees(BodyRoll)));
	// wheels: spin, and the front ones steer
	for (int32 I = 0; I < Wheels.Num(); I++) {
		Wheels[I]->SetRelativeRotation(FRotator(-FMath::RadiansToDegrees(FMath::Fmod(WheelRot, 2 * UE_DOUBLE_PI)), 0, 0));
		if (I < 2) WheelPivots[I]->SetRelativeRotation(FRotator(0, -FMath::RadiansToDegrees(SteerAngle), 0));
	}
	// headlights once it gets dark
	const bool bOn = Driver && World && World->Night > 0.4f && Health > 0 && !bSunk;
	HeadLight->SetVisibility(bOn);
	HeadLight->SetIntensity(bOn ? 2500.f : 0.f);
}
