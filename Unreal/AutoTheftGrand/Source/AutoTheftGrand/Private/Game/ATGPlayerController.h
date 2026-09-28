// The player's controls and camera (ports of src/core/input.js, src/game/player.js and src/game/camera.js):
// keyboard, mouse and gamepad are read directly each frame; on foot the camera orbits and movement is
// relative to it, in a car it is the chase camera that swings back behind you.
//
//   W A S D / left stick     move, or throttle / brake / steer
//   mouse / right stick      look
//   Shift / L3               sprint                 Space / A     jump (on foot), handbrake (in a car: Space, RB, B)
//   F / Enter / Y            get in or out of a car C / R3        car camera distance
//   M / Tab / View           map                    V             spawn a car next to you
//   T                        skip an hour           Esc / Start   pause
#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "ATGPlayerController.generated.h"

class AATGCar;
class AATGCharacter;
class AATGWorld;

UCLASS()
class AATGCameraManager : public APlayerCameraManager {
	GENERATED_BODY()
protected:
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;
};

UCLASS()
class AATGPlayerController : public APlayerController {
	GENERATED_BODY()
public:
	AATGPlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float Dt) override;

	AATGCharacter* Body = nullptr; // the player's character (also while it sits in a car)
	AATGCar* CurrentCar() const;
	void ShowMessage(const FString& Text, float Seconds = 4.f) { Message = Text; MessageTime = Seconds; }

	// the camera the manager shows (Unreal space) and its vertical field of view (degrees)
	FVector CamLocation = FVector::ZeroVector;
	FRotator CamRotation = FRotator::ZeroRotator;
	float CamFovV = 62.f;

	// HUD state
	bool bMapOpen = false;
	FString Message;
	float MessageTime = 0.f;
	UPROPERTY(EditAnywhere, Category = "ATG") float MouseSensitivity = 1.f;

private:
	AATGWorld* World = nullptr;
	// camera.js rig (game axes: yaw is the heading from the pivot towards the camera)
	double Yaw = UE_DOUBLE_PI, Pitch = -0.15, Dist = 4.3, CurDist = 4.3;
	double VehYawOffset = 0, VehPitch = -0.12, LastLook = -10, Time = 0;
	int32 VehCam = 0;
	double Fov = 62;

	bool Down(const TArray<FKey>& Keys) const;
	bool Hit(const TArray<FKey>& Keys) const;
	void UpdateCamera(float Dt, double Dx, double Dy);
	void ToggleCar();
	void SpawnCarHere();
	void Respawn();
};
