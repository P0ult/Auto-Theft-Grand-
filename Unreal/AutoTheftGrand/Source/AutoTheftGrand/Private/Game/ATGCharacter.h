// The player on foot: Unreal's character movement for walking, running and jumping, with the browser
// game's speeds, and a segmented procedural body (Models.cpp BuildHuman) swung by a simple walk cycle.
// Getting into a car seats the body in it and hands control to the car.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ATGCharacter.generated.h"

class AATGCar;
class UStaticMeshComponent;

UCLASS()
class AATGCharacter : public ACharacter {
	GENERATED_BODY()
public:
	AATGCharacter();
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;

	// movement input in world space (game metres: x, z), 0..1 length; sprint held
	void SetMove(double Dx, double Dz, bool bSprint);
	void DoJump() { if (!Car) Jump(); }

	// cars
	AATGCar* Car = nullptr;
	AATGCar* FindCarToEnter() const;
	void EnterCar(AATGCar* Which);
	void ExitCar();

	// game coordinates of the feet, and heading
	void GamePos(double& X, double& Y, double& Z) const;
	double Heading() const;
	bool IsSprinting() const { return bSprinting; }
	float Stamina = 1.f;
	double DrownTime = 0;

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> BodyRoot;
	UPROPERTY(VisibleAnywhere) TMap<FName, TObjectPtr<USceneComponent>> Joints;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	void BuildBody();
	void Animate(float Dt);
	void SetJoint(const TCHAR* Name, double PitchRad, double RollRad = 0);

	double MoveX = 0, MoveZ = 0;
	bool bSprinting = false;
	double Phase = 0, Amp = 0, Air = 0;
};
