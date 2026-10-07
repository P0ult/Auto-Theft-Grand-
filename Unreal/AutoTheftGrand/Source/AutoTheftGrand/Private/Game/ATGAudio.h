// Procedural audio (port of src/game/audio.js): synthesized SFX, vehicle engines, ambience and radio.
// Uses USoundWaveProcedural for runtime-generated sounds.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ATGCoords.h"
#include "ATGAudio.generated.h"

namespace atg { class Game; class IAudio; }

UCLASS()
class AATGAudio : public AActor {
	GENERATED_BODY()
public:
	AATGAudio();
	void Sync(atg::Game* G);
	atg::IAudio* GetInterface() { return AudioInterface; }

	// IAudio implementation
	void PlaySound(const FString& Name, float Vol, const FVector* Pos = nullptr);
	bool PlaySample(const FString& Name, float Vol);
	void StopSample(const FString& Name, float Fade);
	void Muffle(bool On);
	double Clock() const;

private:
	UPROPERTY() class UAudioComponent* AudioComponent;
	UPROPERTY() class USoundWaveProcedural* ProceduralSound;
	class atg::IAudio* AudioInterface = nullptr;
	bool bInit = false;

	void Init();
	void Update(float Dt, atg::Game* G);
	void UpdateListener(atg::Game* G);
	void UpdateVehicleAudio(float Dt, atg::Game* G);
	void UpdateAmbience(float Dt, atg::Game* G);
};