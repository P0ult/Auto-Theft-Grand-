#include "Game/ATGCharacter.h"
#include "Game/ATGCar.h"
#include "Game/ATGCoords.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshUtil.h"
#include "Game/ATGWorld.h"
#include "Gen/CityMap.h"
#include "Gen/Models.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace {
// player.js PLAYER_LOOK
const atg::HumanLook PlayerLook{ 0x8a5536, 0xf2f2f2, 0x2b3a55, 0xeeeeee, 0x111111 };
constexpr double HalfHeight = 0.9, HipsY = 0.98;
double Damp(double A, double B, double Lambda, double Dt) { return FMath::Lerp(A, B, 1 - FMath::Exp(-Lambda * Dt)); }
}

AATGCharacter::AATGCharacter() {
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(30.f, HalfHeight * 100);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	UCharacterMovementComponent* Mv = GetCharacterMovement();
	Mv->bOrientRotationToMovement = true;
	Mv->RotationRate = FRotator(0, 640, 0);
	Mv->MaxWalkSpeed = 430.f;
	Mv->MaxAcceleration = 2400.f;
	Mv->BrakingDecelerationWalking = 2400.f;
	Mv->GroundFriction = 10.f;
	Mv->JumpZVelocity = 560.f;
	Mv->AirControl = 0.3f;
	Mv->MaxStepHeight = 50.f;
	Mv->SetWalkableFloorAngle(50.f);
	Mv->bCanWalkOffLedgesWhenCrouching = true;
	// (the default skeletal mesh isn't used: the body is built from parts)
	if (GetMesh()) GetMesh()->SetVisibility(false);
	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(GetCapsuleComponent());
	BodyRoot->SetRelativeLocation(FVector(0, 0, -HalfHeight * 100));
}

void AATGCharacter::BeginPlay() {
	Super::BeginPlay();
	BuildBody();
}

void AATGCharacter::BuildBody() {
	const std::vector<atg::HumanPart> Human = atg::BuildHuman(PlayerLook);
	UMaterialInterface* Lit = ATGMaterials::Get(EATGMat::VertexLit);
	for (const atg::HumanPart& P : Human) {
		const FName Name(P.name);
		USceneComponent* Parent = P.parent[0] ? Joints.FindRef(FName(P.parent)).Get() : BodyRoot.Get();
		if (!Parent) Parent = BodyRoot;
		USceneComponent* J = NewObject<USceneComponent>(this, FName(*(FString(P.name) + TEXT("_Joint"))));
		J->SetupAttachment(Parent);
		J->SetRelativeLocation(ATG::LocalToUE(P.joint[0], P.joint[1], P.joint[2]));
		J->RegisterComponent();
		Joints.Add(Name, J);
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this, FName(*(FString(P.name) + TEXT("_Mesh"))));
		M->SetupAttachment(J);
		M->SetStaticMesh(ATGMesh::BuildStaticMesh(this, *(TEXT("Human_") + FString(P.name)), TArray<FATGPart>{ { &P.mesh, Lit } }, EATGAxes::Local));
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->RegisterComponent();
		Parts.Add(M);
	}
}

void AATGCharacter::GamePos(double& X, double& Y, double& Z) const {
	ATG::FromUE(GetActorLocation() - FVector(0, 0, HalfHeight * 100), X, Y, Z);
}
double AATGCharacter::Heading() const { return ATG::HeadingFromUE(GetActorRotation().Yaw); }

void AATGCharacter::SetMove(double Dx, double Dz, bool bSprint) {
	MoveX = Dx; MoveZ = Dz;
	bSprinting = bSprint && FMath::Sqrt(Dx * Dx + Dz * Dz) > 0.1;
}

void AATGCharacter::Tick(float Dt) {
	Super::Tick(Dt);
	if (!Car) {
		// sprinting uses stamina (player.js)
		if (bSprinting && Stamina > 0.05f) Stamina = FMath::Max(0.f, Stamina - Dt * 0.08f);
		else { bSprinting = false; Stamina = FMath::Min(1.f, Stamina + Dt * 0.15f); }
		GetCharacterMovement()->MaxWalkSpeed = bSprinting ? 720.f : 430.f;
		const double L = FMath::Sqrt(MoveX * MoveX + MoveZ * MoveZ);
		if (L > 0.01) AddMovementInput(ATG::DirToUE(MoveX, 0, MoveZ), (float)FMath::Min(1.0, L));
		// deep water: no swimming yet, so back to the safehouse after a few seconds
		double X, Y, Z;
		GamePos(X, Y, Z);
		AATGWorld* W = AATGWorld::Get(this);
		if (W && W->Map()) {
			const atg::CityMap* M = W->Map();
			const double Wl = M->WaterLevel(X, Z);
			DrownTime = (Y < Wl - 1.3 && Wl - M->GroundHeight(X, Z) > 1.4) ? DrownTime + Dt : 0;
		}
	}
	Animate(Dt);
}

void AATGCharacter::SetJoint(const TCHAR* Name, double PitchRad, double RollRad) {
	if (TObjectPtr<USceneComponent>* J = Joints.Find(FName(Name))) (*J)->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(PitchRad), 0, FMath::RadiansToDegrees(RollRad)));
}

// a walk / run cycle from the ground speed; a seated pose in cars; legs tucked in the air
void AATGCharacter::Animate(float Dt) {
	if (Car) {
		SetJoint(TEXT("hips"), 0); SetJoint(TEXT("torso"), -0.08); SetJoint(TEXT("head"), 0.05);
		for (const TCHAR* S : { TEXT("L"), TEXT("R") }) {
			const FString Sd(S);
			SetJoint(*(TEXT("thigh") + Sd), 1.45, Sd == TEXT("L") ? 0.08 : -0.08);
			SetJoint(*(TEXT("shin") + Sd), -1.35);
			SetJoint(*(TEXT("arm") + Sd), 0.95, Sd == TEXT("L") ? -0.12 : 0.12);
			SetJoint(*(TEXT("fore") + Sd), 0.55);
		}
		return;
	}
	const FVector V = GetVelocity();
	const double Speed = FVector(V.X, V.Y, 0).Size() / 100.0;
	const bool bFalling = GetCharacterMovement()->IsFalling();
	Air = Damp(Air, bFalling ? 1.0 : 0.0, 10, Dt);
	const double Run = FMath::Clamp((Speed - 4.5) / 2.5, 0.0, 1.0);
	const double Stride = FMath::Lerp(1.5, 2.5, Run);
	Phase = FMath::Fmod(Phase + Speed / Stride * 2 * UE_DOUBLE_PI * Dt, 2 * UE_DOUBLE_PI);
	Amp = Damp(Amp, FMath::Clamp(Speed / 4.3, 0.0, 1.0) * FMath::Lerp(0.45, 0.8, Run), 10, Dt);
	const double Sw = FMath::Sin(Phase) * Amp * (1 - Air);
	const double KneeL = -FMath::Max(0.0, FMath::Sin(Phase + 1.3)) * Amp * 1.4, KneeR = -FMath::Max(0.0, FMath::Sin(Phase + 1.3 + UE_DOUBLE_PI)) * Amp * 1.4;
	SetJoint(TEXT("thighL"), Sw + Air * 0.55);
	SetJoint(TEXT("thighR"), -Sw + Air * 0.35);
	SetJoint(TEXT("shinL"), KneeL * (1 - Air) - Air * 0.9);
	SetJoint(TEXT("shinR"), KneeR * (1 - Air) - Air * 0.6);
	SetJoint(TEXT("armL"), -Sw * 0.8 + Air * 0.4, -0.06 - Air * 0.3);
	SetJoint(TEXT("armR"), Sw * 0.8 + Air * 0.4, 0.06 + Air * 0.3);
	SetJoint(TEXT("foreL"), 0.15 + Run * 1.1 + FMath::Max(0.0, -Sw) * 0.3);
	SetJoint(TEXT("foreR"), 0.15 + Run * 1.1 + FMath::Max(0.0, Sw) * 0.3);
	SetJoint(TEXT("torso"), -Run * 0.18 - Amp * 0.04, Sw * 0.05);
	SetJoint(TEXT("head"), Run * 0.1);
	// the hips bob twice a stride
	BodyRoot->SetRelativeLocation(FVector(0, 0, -HalfHeight * 100 + FMath::Abs(FMath::Sin(Phase)) * Amp * 3.5 * (1 - Air) - Amp * 2));
}

// ------------------------------------------------------------------ cars
AATGCar* AATGCharacter::FindCarToEnter() const {
	double X, Y, Z;
	GamePos(X, Y, Z);
	AATGCar* Best = nullptr;
	double BestD = 1e9;
	for (AATGCar* C : AATGCar::All()) {
		if (!C->Def || C->Driver || C->bSunk) continue;
		const double D = FMath::Sqrt(FMath::Square(C->X - X) + FMath::Square(C->Z - Z));
		if (D < FMath::Max(C->HalfL(), C->HalfW()) + 1.8 && FMath::Abs(C->Y - Y) < 2.5 && D < BestD) { BestD = D; Best = C; }
	}
	return Best;
}

void AATGCharacter::EnterCar(AATGCar* Which) {
	if (!Which || Car) return;
	Car = Which;
	Car->PutIn(this);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
	AttachToComponent(Car->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	// hips just above the seat, facing forward
	const FVector S = Car->SeatLocal();
	SetActorRelativeLocation(ATG::LocalToUE(S.X, S.Y + 0.1 - HipsY + HalfHeight, S.Z));
	SetActorRelativeRotation(FRotator::ZeroRotator);
	BodyRoot->SetRelativeLocation(FVector(0, 0, -HalfHeight * 100));
}

void AATGCharacter::ExitCar() {
	if (!Car) return;
	AATGCar* Was = Car;
	Car = nullptr;
	Was->TakeOut(this);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	const FVector Door = Was->DoorWorld();
	SetActorLocationAndRotation(ATG::ToUE(Door.X, Was->Y + HalfHeight + 0.15, Door.Z), FRotator(0, ATG::HeadingYaw(Was->Yaw), 0));
	SetActorEnableCollision(true);
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	GetCharacterMovement()->Velocity = ATG::DirToUE(Was->VelX * 0.5, 0, Was->VelZ * 0.5) * ATG::M;
}
