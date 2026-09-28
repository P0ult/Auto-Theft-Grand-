// Starts the game: spawns the world, puts the player at the safehouse once it is built, and keeps parked
// cars round the player (vehicles.js _streamParked).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ATGGameMode.generated.h"

class AATGCar;
class AATGWorld;

UCLASS()
class AATGGameMode : public AGameModeBase {
	GENERATED_BODY()
public:
	AATGGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float Dt) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	UPROPERTY(EditAnywhere, Category = "ATG") int32 MaxParked = 26;

private:
	UPROPERTY(Transient) TObjectPtr<AATGWorld> World = nullptr;
	TMap<int32, TWeakObjectPtr<AATGCar>> Parked; // parking spot -> car
	TSet<int32> Consumed;
	double StreamTimer = 0, WaitForGround = 0;
	void SpawnPlayers();
	void StreamParked();
};
