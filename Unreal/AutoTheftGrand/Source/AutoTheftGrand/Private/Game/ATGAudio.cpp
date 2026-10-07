#include "Game/ATGAudio.h"
#include "Sim/Game.h"
#include "Sim/Vehicle.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "AudioThread.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"

namespace atg {

// Simple procedural audio implementation
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

AATGAudio::AATGAudio() {
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	AudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("AudioComponent"));
	AudioComponent->SetupAttachment(RootComponent);
	AudioComponent->bAutoActivate = true;
}

void AATGAudio::Init() {
	if (bInit) return;
	bInit = true;

	// Create a procedural sound wave for synthesized audio
	ProceduralSound = NewObject<USoundWaveProcedural>(this);
	ProceduralSound->SoundGroup = SOUNDGROUP_Default;
	ProceduralSound->bLooping = false;
	ProceduralSound->SetSampleRate(48000);
	ProceduralSound->NumChannels = 2;
	ProceduralSound->Duration = 1.0f;

	// AudioComponent will play procedural sounds
	AudioComponent->SetSound(ProceduralSound);
}

void AATGAudio::Sync(atg::Game* G) {
	if (!G) return;

	// Initialize on first sync
	if (!bInit) Init();

	// Create the IAudio interface if not exists
	if (!AudioInterface) {
		AudioInterface = new atg::AudioImpl(this);
		G->audio = AudioInterface;
	}

	// Update audio state
	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	Update(Dt, G);
}

void AATGAudio::Update(float Dt, atg::Game* G) {
	if (!G) return;
	UpdateListener(G);
	UpdateVehicleAudio(Dt, G);
	UpdateAmbience(Dt, G);
}

void AATGAudio::UpdateListener(atg::Game* G) {
	if (!G || !AudioComponent) return;
	// Listener is handled automatically by UE for local player
	// 3D audio positioning is done via PlaySoundAtLocation
}

void AATGAudio::UpdateVehicleAudio(float Dt, atg::Game* G) {
	// Vehicle engine audio synthesis would go here
	// For now, this is a placeholder
}

void AATGAudio::UpdateAmbience(float Dt, atg::Game* G) {
	// Ambience (wind, birds, city noise) would go here
}

void AATGAudio::PlaySound(const FString& Name, float Vol, const FVector* Pos) {
	if (!ProceduralSound || !AudioComponent) return;

	// For positional sounds, we'd use a separate audio component at the position
	// For now, play through the main component (2D)
	if (Pos) {
		UGameplayStatics::PlaySoundAtLocation(this, ProceduralSound, *Pos, Vol);
	} else {
		AudioComponent->SetVolumeMultiplier(Vol);
		AudioComponent->Play();
	}
}

bool AATGAudio::PlaySample(const FString& Name, float Vol) {
	// Try to load and play a recorded sample (like wasted.mp3)
	// This would need the sample to be imported as a SoundWave asset
	// For now, return false to fall back to procedural generation
	return false;
}

void AATGAudio::StopSample(const FString& Name, float Fade) {
	// Stop a sample
	AudioComponent->FadeOut(Fade, 0.0f);
}

void AATGAudio::Muffle(bool On) {
	// Muffle the world audio (lowpass filter)
	if (AudioComponent) {
		// Could apply a lowpass filter via submix
	}
}

double AATGAudio::Clock() const {
	UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : -1;
}