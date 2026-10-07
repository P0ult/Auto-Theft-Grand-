// Procedural audio (port of src/game/audio.js): synthesized SFX, vehicle engines, ambience and radio.
// Uses USoundWaveProcedural with QueueAudio for runtime synthesis.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundWaveProcedural.h"
#include "ATGCoords.h"
#include "ATGAudio.generated.h"

namespace atg { class Game; class IAudio; }

UCLASS()
class AATGAudio : public AActor {
	GENERATED_BODY()
public:
	AATGAudio(const FObjectInitializer& ObjectInitializer);
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
	UPROPERTY() class USoundWaveProcedural* EngineSoundWave;
	UPROPERTY() class USoundWaveProcedural* AmbienceSoundWave;
	UPROPERTY() class USoundWaveProcedural* SFXSoundWave;
	
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
	void UpdateSFX(float Dt);
	void PumpAudio();
	
	// Procedural sound generation (queue-based)
	void SynthesizeAndPlay(const FString& Name, float Vol, const FVector* Pos);
	void GenerateTone(float Frequency, float Duration, float Volume, const FVector* Pos);
	void GenerateNoiseBurst(float Duration, float Volume, const FVector* Pos);
	void GenerateExplosionSound(float Duration, float Volume, const FVector* Pos);
	void PlayWastedStinger();
	
	// Engine synthesis state
	double EnginePhase = 0.0;
	double EngineNoisePhase = 0.0;
	
	// Ambience synthesis state
	double CityPhase = 0.0;
	double WavePhase = 0.0;
	double RainPhase = 0.0;
	
	// SFX synthesis - queued for game-thread generation
	struct SFXInstance {
		FString Name;
		float Duration;
		float Volume;
		FVector Pos;
		double Elapsed = 0.0;
		double Phase = 0.0;
		float Frequency = 440.0f;
		bool bActive = false;
	};
	TArray<SFXInstance> ActiveSFX;
	
	// Audio buffers for queueing
	TArray<uint8> EngineAudioBuffer;
	TArray<uint8> AmbienceAudioBuffer;
	TArray<uint8> SFXAudioBuffer;
	
	// Generate audio data for queueing (called from game thread)
	void GenerateEngineAudioData(float Dt);
	void GenerateAmbienceAudioData(float Dt);
	void GenerateSFXAudioData(float Dt);
};