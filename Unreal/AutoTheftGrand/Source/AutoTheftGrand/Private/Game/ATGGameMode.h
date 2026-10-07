// Starts the game: spawns the world (which generates everything and creates the simulation), then every
// frame feeds the simulation the player's input, advances it, and keeps an actor drawing each of its
// vehicles and people.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ATGGameMode.generated.h"

class AATGCar;
class AATGEffects;
class AATGPerson;
class AATGWorld;
class USpotLightComponent;

UCLASS()
class AATGGameMode : public AGameModeBase {
	GENERATED_BODY()
public:
	AATGGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float Dt) override;

	// fixed steps for tests (ATG.Step): the game advances only when asked
	bool bManual = false;
	void StepFrames(int32 Frames, double Dt);

private:
	UPROPERTY(Transient) TObjectPtr<AATGWorld> World = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<AATGCar>> Cars;
	UPROPERTY(Transient) TArray<TObjectPtr<AATGPerson>> People;
	UPROPERTY(Transient) TObjectPtr<USpotLightComponent> Headlight;
	UPROPERTY(Transient) TObjectPtr<AATGEffects> Effects = nullptr;
	UPROPERTY(Transient) TObjectPtr<class AATGPoliceView> PoliceView = nullptr;
	UPROPERTY(Transient) TObjectPtr<class AATGPickups> PickupsView = nullptr;
	UPROPERTY(Transient) TObjectPtr<class AATGAudio> Audio = nullptr;
	UPROPERTY(Transient) TObjectPtr<class AATGAnimals> AnimalsView = nullptr;
	bool bStarted = false;
	void StartGame();
	void Frame(double Dt);
	void SyncViews(float Dt);
};
