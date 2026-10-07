#include "Game/ATGAudio.h"
#include "Sim/Game.h"
#include "Sim/Vehicle.h"
#include "Components/AudioComponent.h"
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
}

void AATGAudio::Update(float Dt, atg::Game* G) {
	if (!G) return;
	UpdateListener(G);
	UpdateVehicleAudio(Dt, G);
	UpdateAmbience(Dt, G);
	UpdateMuffle(Dt);
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
		
		// Generate engine tone
		if (VehicleEngineComponent && VehAudio.EngineGain > 0.01f) {
			float EngineFreq = FMath::Clamp(BaseFreq, 50.0f, 500.0f);
			VehicleEngineComponent->SetVolumeMultiplier(VehAudio.EngineGain * MasterVolume);
			// In a full implementation, we'd synthesize the engine sound here
			// For now, just update the gain
		}
		
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

void AATGAudio::SynthesizeAndPlay(const FString& Name, float Vol, const FVector* Pos) {
	// Simple procedural sound generation
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
		// Default simple tone
		GenerateTone(440.0f, 0.2f, Vol * 0.3f, Pos);
	}
}

void AATGAudio::GenerateTone(float Frequency, float Duration, float Volume, const FVector* Pos) {
	// Simple tone generation using a temporary sound
	// In a real implementation, this would use USoundWaveProcedural or MetaSounds
	// For now, we just log and use a placeholder
	if (Pos) {
		// Play at location - in practice would use a procedural sound
		UE_LOG(LogTemp, Verbose, TEXT("Play tone at %s: %.0f Hz for %.2fs"), *Pos->ToString(), Frequency, Duration);
	} else {
		UE_LOG(LogTemp, Verbose, TEXT("Play tone: %.0f Hz for %.2fs"), Frequency, Duration);
	}
}

void AATGAudio::GenerateNoiseBurst(float Duration, float Volume, const FVector* Pos) {
	UE_LOG(LogTemp, Verbose, TEXT("Play noise burst: %.2fs"), Duration);
}

void AATGAudio::GenerateExplosionSound(float Duration, float Volume, const FVector* Pos) {
	UE_LOG(LogTemp, Verbose, TEXT("Play explosion: %.2fs"), Duration);
}

void AATGAudio::PlayWastedStinger() {
	UE_LOG(LogTemp, Verbose, TEXT("Play WASTED stinger"));
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