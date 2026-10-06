// Turns Unreal's keyboard, mouse and gamepad into the simulation's input (src/core/input.js names keys by
// their KeyboardEvent codes and pad buttons in the standard layout), and shows the simulation's camera.
// The controls themselves (what each key does) are the simulation's: see Sim/Input.cpp.
#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "ATGPlayerController.generated.h"

class AATGWorld;
namespace atg { class Game; }

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

	// copy this frame's keys, mouse and pad into the simulation's input
	void FeedInput(atg::Game& G, double Dt);
	void ShowMessage(const FString& Text, float Seconds = 4.f) { Message = Text; MessageTime = Seconds; }

	// HUD state
	bool bMapOpen = false;
	FString Message;
	float MessageTime = 0.f;
	UPROPERTY(EditAnywhere, Category = "ATG") float MouseSensitivity = 1.f;

	// keys held by test scripts (ATG.Press / ATG.Release), on top of the real ones
	TSet<FString> ScriptKeys;
	// (test scripts: mouse movement in browser pixels, added to the next simulation frame)
	void ScriptMouse(double Dx, double Dy) { ScriptDx += Dx; ScriptDy += Dy; }
	double ScriptDx = 0, ScriptDy = 0;

private:
	TSet<FString> Held;   // keys the simulation has been told are down
	double MouseDx = 0, MouseDy = 0, Wheel = 0;
};
