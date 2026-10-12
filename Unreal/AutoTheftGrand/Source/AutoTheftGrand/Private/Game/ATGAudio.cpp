#include "Game/ATGAudio.h"
#include "AutoTheftGrand.h"
#include "Sim/Audio.h"
#include "Sim/Game.h"
#include "Kismet/GameplayStatics.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"

int32 UATGSoundStream::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) {
	// (16-bit interleaved stereo, as the procedural wave expects)
	const int32 Frames = NumSamples / 2;
	OutAudio.Reset();
	OutAudio.AddUninitialized(Frames * 2 * sizeof(int16));
	int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());
	TArray<float> Buf;
	Buf.SetNumUninitialized(Frames * 2);
	if (atg::Audio* A = Sim.load()) A->render(Buf.GetData(), Frames);
	else FMemory::Memzero(Buf.GetData(), Buf.Num() * sizeof(float));
	for (int32 I = 0; I < Frames * 2; I++) Out[I] = (int16)FMath::RoundToInt(FMath::Clamp(Buf[I], -1.f, 1.f) * 32767.f);
	return Frames * 2;
}

AATGAudio::AATGAudio() {
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AATGAudio::Start(atg::Game* G) {
	if (Stream || !G || !G->audioSys) return;
	UWorld* W = GetWorld();
	if (!W || !W->GetAudioDevice().IsValid()) { UE_LOG(LogATG, Log, TEXT("No audio device: the game runs silent")); return; }
	G->audioSys->init();
	// Load wasted sound if not set
	if (!WastedSound) {
		// Try to find the asset via asset registry
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		TArray<FAssetData> AssetList;
		FTopLevelAssetPath SoundWaveClassPath = USoundWave::StaticClass()->GetClassPathName();
		AssetRegistry.GetAssetsByClass(SoundWaveClassPath, AssetList, true);
		for (const FAssetData& Asset : AssetList) {
			if (Asset.AssetName.ToString().Contains("wasted")) {
				WastedSound = Cast<USoundWave>(Asset.GetAsset());
				if (WastedSound) {
					UE_LOG(LogATG, Log, TEXT("Found wasted sound: %s"), *WastedSound->GetName());
					break;
				}
			}
		}
		if (!WastedSound) {
			UE_LOG(LogATG, Warning, TEXT("Wasted sound not found in asset registry, trying direct load"));
			WastedSound = LoadObject<USoundWave>(nullptr, TEXT("/Game/Audio/wasted.wasted"));
		}
		if (!WastedSound) {
			UE_LOG(LogATG, Error, TEXT("Failed to load wasted sound!"));
		}
	}
	// Set up sample playback callback
	if (atg::Audio* AudioSys = G->audioSys) {
		AudioSys->onPlaySample = [this](const std::string& name, double vol) -> bool {
			if (name == "wasted" && WastedSound) {
				UGameplayStatics::PlaySound2D(GetWorld(), WastedSound, (float)vol);
				return true;
			}
			return false;
		};
	}
	Stream = NewObject<UATGSoundStream>(this);
	Stream->SetSampleRate(atg::wa::SR);
	Stream->NumChannels = 2;
	Stream->Duration = INDEFINITELY_LOOPING_DURATION;
	Stream->SoundGroup = SOUNDGROUP_Default;
	Stream->bLooping = false;
	Stream->Sim.store(G->audioSys);
	Component = NewObject<UAudioComponent>(this);
	Component->SetupAttachment(RootComponent);
	Component->bAutoActivate = false;
	Component->bAllowSpatialization = false;
	Component->bIsUISound = true;
	Component->SetSound(Stream);
	Component->RegisterComponent();
	Component->Play();
	UE_LOG(LogATG, Log, TEXT("Sound started (%d Hz stereo)"), atg::wa::SR);
}

void AATGAudio::EndPlay(const EEndPlayReason::Type Reason) {
	if (Stream) Stream->Sim.store(nullptr);
	if (Component) Component->Stop();
	Super::EndPlay(Reason);
}
