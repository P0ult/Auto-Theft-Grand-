// The game's sound: Sim/Audio (the port of audio.js) renders it; this streams its samples to the audio device
// through one procedural sound wave, pulled on the audio render thread. Nothing starts without an audio device
// (the test runs use -nosound).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "ATGAudio.generated.h"

class UAudioComponent;
namespace atg { class Audio; class Game; }

UCLASS()
class UATGSoundStream : public USoundWaveProcedural {
	GENERATED_BODY()
public:
	std::atomic<atg::Audio*> Sim{ nullptr };
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
};

UCLASS()
class AATGAudio : public AActor {
	GENERATED_BODY()
public:
	AATGAudio();
	// start the game's sound, if this machine has an audio device
	void Start(atg::Game* G);
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> Component;
	UPROPERTY(Transient) TObjectPtr<UATGSoundStream> Stream;
};
