// Procedural audio (port of src/game/audio.js): synthesized SFX, vehicle engines, ambience and radio.
// Uses runtime synthesis with UGameplayStatics for playback.
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
	UPROPERTY() class UAudioComponent* VehicleEngineComponent;
	UPROPERTY() class UAudioComponent* AmbienceComponent;
	UPROPERTY() class USoundWave* VehicleEngineSound;
	UPROPERTY() class USoundWave* AmbienceSound;
	
	class atg::IAudio* AudioInterface = nullptr;
	bool bInit = false;

	// Audio state
	float MasterVolume = 1.0f;
	float MusicVolume = 0.6f;
	bool bMuffled = false;
	float MuffleTarget = 1.0f;
	
	// Vehicle audio state
	struct VehicleAudioState {
		float RPM = 800.0f;
		int32 Gear = 1;
		float EngineGain = 0.0f;
		float TireGain = 0.0f;
		float WindGain = 0.0f;
		float HornGain = 0.0f;
	};
	VehicleAudioState VehAudio;

	// Ambience state
	float BirdTimer = 0.0f;
	float CricketTimer = 0.0f;
	
	// Envelope
	struct Envelope {
		float Value = 0.0f;
		float Target = 0.0f;
		float Rate = 0.0f;
		void SetTarget(float T, float R) { Target = T; Rate = R; }
		float Update(float Dt) { Value += (Target - Value) * FMath::Min(1.0f, Rate * Dt); return Value; }
	};
	
	Envelope WorldFilterFreq;
	Envelope MusicGain;
	
	// Synthesis helpers
	void Init();
	void Update(float Dt, atg::Game* G);
	void UpdateListener(atg::Game* G);
	void UpdateVehicleAudio(float Dt, atg::Game* G);
	void UpdateAmbience(float Dt, atg::Game* G);
	void UpdateMuffle(float Dt);
	
	// Sound generation
	void SynthesizeAndPlay(const FString& Name, float Vol, const FVector* Pos);
	void GenerateTone(float Frequency, float Duration, float Volume, const FVector* Pos);
	void GenerateNoiseBurst(float Duration, float Volume, const FVector* Pos);
	void GenerateExplosionSound(float Duration, float Volume, const FVector* Pos);
	void PlayWastedStinger();
};