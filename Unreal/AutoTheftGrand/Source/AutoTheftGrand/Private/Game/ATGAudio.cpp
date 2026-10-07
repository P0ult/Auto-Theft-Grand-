// Procedural audio (port of src/game/audio.js): synthesized SFX, vehicle engines, ambience and radio.
// Uses USoundWaveProcedural with QueueAudio for runtime synthesis.
#include "Game/ATGAudio.h"
#include "Sim/Game.h"
#include "Sim/Vehicle.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"

namespace atg {

// IAudio implementation that forwards to AATGAudio actor
class AudioImpl : public IAudio {
public:
	AudioImpl(AATGAudio* Owner) : owner(Owner) {}
	~AudioImpl() override = default;

	void play(const std::string& name, double vol = 1) override {
		if (!owner) return;
		owner->PlaySound(UTF8_TO_TCHAR(name.c_str()), (float)vol, nullptr);
	}
	void playAt(const std::string& name, const V3& pos, double vol = 1) override {
		if (!owner) return;
		FVector p = ATG::ToUE(pos);
		owner->PlaySound(UTF8_TO_TCHAR(name.c_str()), (float)vol, &p);
	}
	bool playSample(const std::string& name, double vol = 1) override {
		if (!owner) return false;
		return owner->PlaySample(UTF8_TO_TCHAR(name.c_str()), (float)vol);
	}
	void stopSample(const std::string& name, double fade = 0) override {
		if (!owner) return;
		owner->StopSample(UTF8_TO_TCHAR(name.c_str()), (float)fade);
	}
	void muffle(bool on) override {
		if (!owner) return;
		owner->Muffle(on);
	}
	double clock() const override {
		if (!owner) return -1;
		return owner->Clock();
	}

private:
	AATGAudio* owner = nullptr;
};

} // namespace atg

AATGAudio::AATGAudio(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	
	AudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("AudioComponent"));
	RootComponent = AudioComponent;
	AudioComponent->SetupAttachment(RootComponent);
	AudioComponent->bAutoActivate = true;
	
	VehicleEngineComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("VehicleEngineComponent"));
	VehicleEngineComponent->SetupAttachment(RootComponent);
	VehicleEngineComponent->bAutoActivate = true;
	VehicleEngineComponent->SetVolumeMultiplier(0.0f);

	AmbienceComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("AmbienceComponent"));
	AmbienceComponent->SetupAttachment(RootComponent);
	AmbienceComponent->bAutoActivate = true;
	AmbienceComponent->SetVolumeMultiplier(0.0f);
}

void AATGAudio::Init() {
	if (bInit) return;
	bInit = true;

	// Initialize envelopes
	WorldFilterFreq.SetTarget(20000.0f, 5.0f);
	MusicGain.SetTarget(0.6f * 0.55f, 2.0f);

	// AudioComponent volume
	AudioComponent->SetVolumeMultiplier(MasterVolume);

	// Create procedural sound waves
	EngineSoundWave = NewObject<USoundWaveProcedural>(this, TEXT("EngineSoundWave"));
	EngineSoundWave->NumChannels = 1;
	EngineSoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
	EngineSoundWave->SoundGroup = ESoundGroup::SOUNDGROUP_Default;
	EngineSoundWave->bLooping = true;
	EngineSoundWave->SampleByteSize = 2; // 16-bit
	
	AmbienceSoundWave = NewObject<USoundWaveProcedural>(this, TEXT("AmbienceSoundWave"));
	AmbienceSoundWave->NumChannels = 2;
	AmbienceSoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
	AmbienceSoundWave->SoundGroup = ESoundGroup::SOUNDGROUP_Default;
	AmbienceSoundWave->bLooping = true;
	AmbienceSoundWave->SampleByteSize = 2;
	
	SFXSoundWave = NewObject<USoundWaveProcedural>(this, TEXT("SFXSoundWave"));
	SFXSoundWave->NumChannels = 2;
	SFXSoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
	SFXSoundWave->SoundGroup = ESoundGroup::SOUNDGROUP_Default;
	SFXSoundWave->bLooping = true;
	SFXSoundWave->SampleByteSize = 2;

	// Pre-allocate audio buffers (enough for ~200ms at 48kHz)
	const int32 BufferSamples = 9600;
	EngineAudioBuffer.SetNum(BufferSamples * 1 * 2); // 1 channel, 16-bit
	AmbienceAudioBuffer.SetNum(BufferSamples * 2 * 2); // 2 channels, 16-bit
	SFXAudioBuffer.SetNum(BufferSamples * 2 * 2); // 2 channels, 16-bit

	// Start playing procedural sounds
	if (VehicleEngineComponent && EngineSoundWave) {
		VehicleEngineComponent->SetSound(EngineSoundWave);
		VehicleEngineComponent->Play();
	}
	if (AmbienceComponent && AmbienceSoundWave) {
		AmbienceComponent->SetSound(AmbienceSoundWave);
		AmbienceComponent->Play();
	}
	if (AudioComponent && SFXSoundWave) {
		AudioComponent->SetSound(SFXSoundWave);
		AudioComponent->Play();
	}
}

void AATGAudio::Sync(atg::Game* G) {
	if (!G) return;

	if (!bInit) Init();

	if (!AudioInterface) {
		AudioInterface = new atg::AudioImpl(this);
		G->audio = AudioInterface;
	}

	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	Update(Dt, G);
	PumpAudio();
}

void AATGAudio::Update(float Dt, atg::Game* G) {
	if (!G) return;
	UpdateListener(G);
	UpdateVehicleAudio(Dt, G);
	UpdateAmbience(Dt, G);
	UpdateMuffle(Dt);
	UpdateSFX(Dt);
}

void AATGAudio::PumpAudio() {
	// Generate and queue audio data
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	GenerateEngineAudioData(Dt);
	GenerateAmbienceAudioData(Dt);
	GenerateSFXAudioData(Dt);
}

void AATGAudio::UpdateListener(atg::Game* G) {
	// Listener handled by UE automatically for local player
}

void AATGAudio::UpdateMuffle(float Dt) {
	WorldFilterFreq.Update(Dt);
	MusicGain.Update(Dt);
}

void AATGAudio::UpdateVehicleAudio(float Dt, atg::Game* G) {
	if (!G || !G->player) return;
	
	atg::Vehicle* V = G->player->vehicle;
	bool bFlying = V && V->def.kind != "";
	bool bDriving = V && !bFlying && !V->def.pedal && !V->def.board && !V->isWrecked();
	
	if (bDriving) {
		float Sp = FMath::Abs(V->speed());
		float Top = V->def.top > 0 ? V->def.top : 50.0f;
		
		// Fake gearbox
		const float Gears[] = { 0, 0.18f, 0.34f, 0.52f, 0.72f, 1.01f };
		float Ratio = Top > 0 ? Sp / Top : 0;
		int32 Gear = 1;
		while (Gear < 5 && Ratio > Gears[Gear]) Gear++;
		
		float Lo = Gears[Gear - 1], Hi = Gears[Gear];
		float TargetRPM = 900.0f + ((Ratio - Lo) / FMath::Max(0.001f, Hi - Lo)) * 5200.0f;
		
		if (V->wheelspin > 0.1f || (V->input.throttle > 0.5f && Sp < 3.0f)) {
			TargetRPM = FMath::Max(TargetRPM, 3500.0f + V->input.throttle * 2500.0f);
		}
		if (V->airborne) {
			TargetRPM = FMath::Max(TargetRPM, 5000.0f * V->input.throttle + 1500.0f);
		}
		
		VehAudio.RPM += (TargetRPM - VehAudio.RPM) * FMath::Min(1.0f, Dt * 8.0f);
		VehAudio.Gear = Gear;
		
		float Heavy = V->def.mass > 2000.0f ? 0.7f : 1.0f;
		float BaseFreq = (VehAudio.RPM / 60.0f) * 0.5f * Heavy;
		
		if (V->def.kind == "boat") {
			BaseFreq *= (V->def.boat == "jetski" ? 1.4f : 0.6f);
		} else if (!V->def.bike.empty()) {
			BaseFreq *= 1.75f;
		} else if (V->def.body == "super") {
			BaseFreq *= 1.3f;
		} else if (V->def.body == "muscle") {
			BaseFreq *= 0.8f;
		}
		
		VehAudio.EngineGain = 0.13f + V->input.throttle * 0.12f;
		
		// Tire noise
		float Skid = V->skid ? FMath::Clamp(V->slipRear / 8.0f + V->wheelspin, 0.2f, 1.0f) : 0.0f;
		VehAudio.TireGain = Skid * 0.18f;
		
		// Wind noise
		VehAudio.WindGain = FMath::Clamp(Sp / 50.0f, 0.0f, 1.0f) * 0.12f;
		
		// Horn
		VehAudio.HornGain = V->horn ? 0.08f : 0.0f;
	} 
	else if (V && !V->def.bike.empty() && !V->airborne) {
		// Skateboard/bicycle - urethane on concrete
		VehAudio.EngineGain = 0.0f;
		VehAudio.TireGain = FMath::Clamp(V->speedAbs() / V->def.top, 0.0f, 1.0f) * 0.1f;
		VehAudio.WindGain = 0.0f;
	} 
	else {
		VehAudio.EngineGain = 0.0f;
		VehAudio.TireGain = 0.0f;
		VehAudio.WindGain = bFlying && V && !V->isWrecked() ? FMath::Clamp(V->vel.length() / 120.0f, 0.0f, 1.0f) * 0.16f : 0.0f;
	}
	
	if (VehicleEngineComponent) {
		VehicleEngineComponent->SetVolumeMultiplier(VehAudio.EngineGain * MasterVolume);
	}
}

void AATGAudio::UpdateAmbience(float Dt, atg::Game* G) {
	if (!G) return;
	
	BirdTimer -= Dt;
	CricketTimer -= Dt;
	
	const atg::V3& CamPos = G->rig.camPos;
	float Night = G->env.night;
	
	// City ambience
	float CityGain = (Night < 0.5f ? 0.22f : 0.08f) * (1.0f - Night * 0.4f);
	
	// Waves near beach
	float BeachDist = FMath::Max(0.0f, CamPos.z - 560.0f);
	float WaveGain = FMath::Clamp(BeachDist / 120.0f, 0.0f, 1.0f) * 0.35f;
	
	// Rain
	float RainGain = G->env.rain * 0.18f;
	
	// Birds (daytime)
	if (Night < 0.3f && BirdTimer <= 0.0f) {
		BirdTimer = FMath::FRandRange(1.5f, 5.0f);
		GenerateTone(4200.0f, 0.1f, 0.04f, nullptr);
	}
	
	// Crickets (night)
	if (Night > 0.6f && CricketTimer <= 0.0f) {
		CricketTimer = FMath::FRandRange(0.8f, 2.5f);
		GenerateTone(2200.0f, 0.1f, 0.04f, nullptr);
	}
	
	if (AmbienceComponent) {
		AmbienceComponent->SetVolumeMultiplier((CityGain + WaveGain + RainGain) * MasterVolume);
	}
}

void AATGAudio::UpdateSFX(float Dt) {
	for (int32 i = ActiveSFX.Num() - 1; i >= 0; --i) {
		SFXInstance& SFX = ActiveSFX[i];
		SFX.Elapsed += Dt;
		if (SFX.Elapsed >= SFX.Duration) {
			ActiveSFX.RemoveAt(i);
		}
	}
}

void AATGAudio::SynthesizeAndPlay(const FString& Name, float Vol, const FVector* Pos) {
	if (Name == "pistol") {
		GenerateTone(900.0f, 0.18f, Vol * 0.5f, Pos);
	} else if (Name == "rifle" || Name == "sniper") {
		GenerateTone(700.0f, 0.14f, Vol * 0.6f, Pos);
	} else if (Name == "shotgun") {
		GenerateNoiseBurst(0.35f, Vol * 0.7f, Pos);
	} else if (Name == "explosion") {
		GenerateExplosionSound(2.5f, Vol, Pos);
	} else if (Name == "pickup") {
		GenerateTone(523.25f, 0.18f, Vol * 0.25f, Pos);
		GenerateTone(659.25f, 0.18f, Vol * 0.25f, Pos);
		GenerateTone(783.99f, 0.18f, Vol * 0.25f, Pos);
	} else if (Name == "cash") {
		GenerateTone(659.25f, 0.12f, Vol * 0.2f, Pos);
		GenerateTone(880.00f, 0.12f, Vol * 0.2f, Pos);
		GenerateTone(1046.50f, 0.12f, Vol * 0.2f, Pos);
	} else if (Name == "wasted") {
		PlayWastedStinger();
	} else {
		GenerateTone(440.0f, 0.2f, Vol * 0.3f, Pos);
	}
}

void AATGAudio::GenerateTone(float Frequency, float Duration, float Volume, const FVector* Pos) {
	SFXInstance SFX;
	SFX.Name = "tone";
	SFX.Duration = Duration;
	SFX.Volume = Volume;
	SFX.Pos = Pos ? *Pos : FVector::ZeroVector;
	SFX.Phase = 0.0;
	SFX.Frequency = Frequency;
	SFX.bActive = true;
	ActiveSFX.Add(SFX);
}

void AATGAudio::GenerateNoiseBurst(float Duration, float Volume, const FVector* Pos) {
	SFXInstance SFX;
	SFX.Name = "noise";
	SFX.Duration = Duration;
	SFX.Volume = Volume;
	SFX.Pos = Pos ? *Pos : FVector::ZeroVector;
	SFX.bActive = true;
	ActiveSFX.Add(SFX);
}

void AATGAudio::GenerateExplosionSound(float Duration, float Volume, const FVector* Pos) {
	SFXInstance SFX;
	SFX.Name = "explosion";
	SFX.Duration = Duration;
	SFX.Volume = Volume;
	SFX.Pos = Pos ? *Pos : FVector::ZeroVector;
	SFX.bActive = true;
	ActiveSFX.Add(SFX);
}

void AATGAudio::PlayWastedStinger() {
	GenerateTone(300.0f, 2.0f, 0.8f, nullptr);
	GenerateTone(250.0f, 1.5f, 0.6f, nullptr);
	GenerateTone(200.0f, 1.0f, 0.4f, nullptr);
}

// --- Audio generation for queueing (game thread) ---

void AATGAudio::GenerateEngineAudioData(float Dt) {
	if (!EngineSoundWave) return;
	
	const int32 SampleRate = 48000;
	const int32 NumChannels = 1;
	const int32 BytesPerSample = 2;
	const int32 NumSamples = 9600; // 200ms buffer
	
	if (EngineAudioBuffer.Num() != NumSamples * NumChannels * BytesPerSample) {
		EngineAudioBuffer.SetNum(NumSamples * NumChannels * BytesPerSample);
	}
	
	int16* Samples = reinterpret_cast<int16*>(EngineAudioBuffer.GetData());
	
	for (int32 i = 0; i < NumSamples; ++i) {
		float Sample = 0.0f;
		const float SampleDt = 1.0f / SampleRate;
		
		if (VehAudio.EngineGain > 0.001f) {
			float BaseFreq = (VehAudio.RPM / 60.0f) * 0.5f;
			
			float Harmonic1 = FMath::Sin(EnginePhase * 2.0f * PI) * 0.6f;
			float Harmonic2 = FMath::Sin(EnginePhase * 4.0f * PI) * 0.3f;
			float Harmonic3 = FMath::Sin(EnginePhase * 6.0f * PI) * 0.15f;
			float Harmonic4 = FMath::Sin(EnginePhase * 8.0f * PI) * 0.08f;
			
			EngineNoisePhase += SampleDt * 5000.0f;
			float Noise = (FMath::FRand() * 2.0f - 1.0f) * 0.1f;
			
			Sample = (Harmonic1 + Harmonic2 + Harmonic3 + Harmonic4 + Noise) * VehAudio.EngineGain;
			
			EnginePhase += BaseFreq * SampleDt;
			if (EnginePhase > 1.0) EnginePhase -= 1.0;
		}
		
		// Tire noise
		if (VehAudio.TireGain > 0.001f) {
			float TireNoise = (FMath::FRand() * 2.0f - 1.0f) * VehAudio.TireGain * 0.5f;
			Sample += TireNoise;
		}
		
		// Wind noise
		if (VehAudio.WindGain > 0.001f) {
			float WindNoise = (FMath::FRand() * 2.0f - 1.0f) * VehAudio.WindGain * 0.3f;
			Sample += WindNoise;
		}
		
		// Horn
		if (VehAudio.HornGain > 0.001f) {
			Sample += FMath::Sin(EnginePhase * 2.0f * PI) * VehAudio.HornGain;
		}
		
		// Apply lowpass for muffled effect
		if (MuffleTarget < 20000.0f) {
			static float LastSample = 0.0f;
			float Alpha = FMath::Clamp(MuffleTarget / 20000.0f, 0.0f, 1.0f);
			Sample = LastSample * (1.0f - Alpha) + Sample * Alpha;
			LastSample = Sample;
		}
		
		Sample = FMath::Clamp(Sample * MasterVolume, -1.0f, 1.0f);
		Samples[i] = int16(Sample * 32767.0f);
	}
	
	EngineSoundWave->QueueAudio(EngineAudioBuffer.GetData(), EngineAudioBuffer.Num());
}

void AATGAudio::GenerateAmbienceAudioData(float Dt) {
	if (!AmbienceSoundWave) return;
	
	const int32 SampleRate = 48000;
	const int32 NumChannels = 2;
	const int32 BytesPerSample = 2;
	const int32 NumSamples = 9600; // 200ms buffer
	
	if (AmbienceAudioBuffer.Num() != NumSamples * NumChannels * BytesPerSample) {
		AmbienceAudioBuffer.SetNum(NumSamples * NumChannels * BytesPerSample);
	}
	
	int16* Samples = reinterpret_cast<int16*>(AmbienceAudioBuffer.GetData());
	const float SampleDt = 1.0f / SampleRate;
	
	for (int32 i = 0; i < NumSamples; ++i) {
		// City ambience - low rumble
		CityPhase += SampleDt * 60.0f;
		if (CityPhase > 1.0) CityPhase -= 1.0;
		float CityRumble = FMath::Sin(CityPhase * 2.0f * PI) * 0.15f;
		CityRumble += (FMath::FRand() * 2.0f - 1.0f) * 0.1f;
		
		// Waves - low frequency noise
		WavePhase += SampleDt * 0.5f;
		if (WavePhase > 1.0) WavePhase -= 1.0;
		float WaveSound = FMath::Sin(WavePhase * 2.0f * PI) * 0.2f;
		WaveSound += (FMath::FRand() * 2.0f - 1.0f) * 0.15f;
		
		// Rain - white noise
		RainPhase += SampleDt * 1000.0f;
		if (RainPhase > 1.0) RainPhase -= 1.0;
		float RainSound = (FMath::FRand() * 2.0f - 1.0f) * 0.2f;
		
		float SampleL = (CityRumble + WaveSound + RainSound) * 0.3f;
		float SampleR = (CityRumble + WaveSound + RainSound) * 0.3f;
		
		SampleL = FMath::Clamp(SampleL * MasterVolume, -1.0f, 1.0f);
		SampleR = FMath::Clamp(SampleR * MasterVolume, -1.0f, 1.0f);
		
		Samples[i * 2] = int16(SampleL * 32767.0f);
		Samples[i * 2 + 1] = int16(SampleR * 32767.0f);
	}
	
	AmbienceSoundWave->QueueAudio(AmbienceAudioBuffer.GetData(), AmbienceAudioBuffer.Num());
}

void AATGAudio::GenerateSFXAudioData(float Dt) {
	if (!SFXSoundWave) return;
	
	const int32 SampleRate = 48000;
	const int32 NumChannels = 2;
	const int32 BytesPerSample = 2;
	const int32 NumSamples = 9600; // 200ms buffer
	
	if (SFXAudioBuffer.Num() != NumSamples * NumChannels * BytesPerSample) {
		SFXAudioBuffer.SetNum(NumSamples * NumChannels * BytesPerSample);
	}
	
	int16* Samples = reinterpret_cast<int16*>(SFXAudioBuffer.GetData());
	
	// Clear buffer
	FMemory::Memzero(Samples, NumSamples * NumChannels * BytesPerSample);
	
	const float SampleDt = 1.0f / SampleRate;
	
	// Process active SFX
	for (SFXInstance& SFX : ActiveSFX) {
		for (int32 i = 0; i < NumSamples; ++i) {
			float t = SFX.Elapsed + i * SampleDt;
			if (t > SFX.Duration) continue;
			
			float Sample = 0.0f;
			float Envelope = 1.0f - FMath::Pow(t / SFX.Duration, 2.0f);
			
			if (SFX.Name == "tone") {
				SFX.Phase += SFX.Frequency * SampleDt;
				if (SFX.Phase > 1.0) SFX.Phase -= 1.0;
				Sample = FMath::Sin(SFX.Phase * 2.0f * PI) * Envelope;
			}
			else if (SFX.Name == "noise") {
				Sample = (FMath::FRand() * 2.0f - 1.0f) * Envelope;
			}
			else if (SFX.Name == "explosion") {
				float Rumble = FMath::Sin(t * 80.0f * 2.0f * PI) * 0.5f;
				float Noise = (FMath::FRand() * 2.0f - 1.0f) * 0.5f;
				Sample = (Rumble + Noise) * Envelope;
			}
			
			Sample *= SFX.Volume * 0.5f;
			Samples[i * 2] = int16(FMath::Clamp(Samples[i * 2] / 32767.0f + Sample, -1.0f, 1.0f) * 32767.0f);
			Samples[i * 2 + 1] = int16(FMath::Clamp(Samples[i * 2 + 1] / 32767.0f + Sample, -1.0f, 1.0f) * 32767.0f);
		}
	}
	
	SFXSoundWave->QueueAudio(SFXAudioBuffer.GetData(), SFXAudioBuffer.Num());
}

void AATGAudio::PlaySound(const FString& Name, float Vol, const FVector* Pos) {
	SynthesizeAndPlay(Name, Vol, Pos);
}

bool AATGAudio::PlaySample(const FString& Name, float Vol) {
	if (Name == "wasted") {
		PlayWastedStinger();
		return true;
	}
	return false;
}

void AATGAudio::StopSample(const FString& Name, float Fade) {
	AudioComponent->FadeOut(Fade, 0.0f);
}

void AATGAudio::Muffle(bool On) {
	MuffleTarget = On ? 420.0f : 20000.0f;
	MusicGain.SetTarget(On ? MusicVolume * 0.12f : MusicVolume * 0.55f, On ? 0.5f : 1.5f);
}

double AATGAudio::Clock() const {
	UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : -1;
}