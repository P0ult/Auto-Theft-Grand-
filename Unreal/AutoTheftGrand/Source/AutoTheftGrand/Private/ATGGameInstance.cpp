#include "ATGGameInstance.h"
#include "ATGSaveSystem.h"
#include "Sim/Game.h"
#include "Game/ATGGameMode.h"
#include "Game/ATGWorld.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Sim/Audio.h"
#include "Sim/Peds.h"
#include "Sim/Traffic.h"

UATGGameInstance::UATGGameInstance() {}
void UATGGameInstance::Init()
{
	Super::Init();
	if (!SaveSystem) SaveSystem = NewObject<UATGSaveSystem>(this);
	SaveSystem->SetSavePath(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AutoTheftGrandSave.json")));
}
void UATGGameInstance::Shutdown() { Super::Shutdown(); }

bool UATGGameInstance::HasSave() const { return SaveSystem && SaveSystem->HasSave(); }
bool UATGGameInstance::LoadGame() { return SaveSystem && SaveSystem->LoadGame(); }
bool UATGGameInstance::SaveGame(bool bAutoSave) { return SaveSystem && SaveSystem->SaveGame(bAutoSave); }
void UATGGameInstance::ClearSave() { if (SaveSystem) SaveSystem->ClearSave(); }

void UATGGameInstance::InitializeSaveSystem(atg::Game* Game, const FString& SavePath)
{
	if (!Game) return;
	if (!SaveSystem) SaveSystem = NewObject<UATGSaveSystem>(this);
	SaveSystem->Initialize(Game);
	SaveSystem->SetSavePath(SavePath);
	const FString Quality = QualitySetting.ToString();
	const FString Crime = NPCrimeLevel.ToString();
	Game->settings.quality = TCHAR_TO_UTF8(*Quality);
	Game->settings.volume = MasterVolume;
	Game->settings.music = MusicVolume;
	Game->settings.sensitivity = MouseSensitivity;
	Game->settings.invertY = bInvertMouseY;
	Game->settings.npcCrime = TCHAR_TO_UTF8(*Crime);
	Game->input.sensitivity = MouseSensitivity;
	Game->input.invertY = bInvertMouseY;
	Game->quality = atg::QualityPreset(Game->settings.quality);
	if (Game->traffic) Game->traffic->maxCars = Game->quality.traffic;
	if (Game->peds) Game->peds->maxPeds = Game->quality.peds;
	if (Game->audioSys) { Game->audioSys->setVolume(MasterVolume); Game->audioSys->setMusic(MusicVolume); }
}

bool UATGGameInstance::IsFreeRoamActive() const { return bFreeRoamActive; }
void UATGGameInstance::SetFreeRoamActive(bool bActive) { bFreeRoamActive = bActive; }

void UATGGameInstance::ApplyQuality(FName Quality)
{
	QualitySetting = Quality;
	if (AATGGameMode* GM = GetWorld() ? Cast<AATGGameMode>(UGameplayStatics::GetGameMode(GetWorld())) : nullptr)
		if (AATGWorld* World = AATGWorld::Get(GM)) if (atg::Game* G = World->Game()) {
			G->settings.quality = TCHAR_TO_UTF8(*Quality.ToString());
			G->quality = atg::QualityPreset(G->settings.quality);
			if (G->traffic) G->traffic->maxCars = G->quality.traffic;
			if (G->peds) G->peds->maxPeds = G->quality.peds;
		}
}

void UATGGameInstance::SetHUDStyle(FName Style) { HUDStyle = Style; }
void UATGGameInstance::SetVolume(float Volume)
{
	MasterVolume = FMath::Clamp(Volume, 0.f, 1.f);
	if (AATGGameMode* GM = GetWorld() ? Cast<AATGGameMode>(UGameplayStatics::GetGameMode(GetWorld())) : nullptr)
		if (AATGWorld* World = AATGWorld::Get(GM)) if (atg::Game* G = World->Game()) { G->settings.volume = MasterVolume; if (G->audioSys) G->audioSys->setVolume(MasterVolume); }
}
void UATGGameInstance::SetMusicVolume(float Volume)
{
	MusicVolume = FMath::Clamp(Volume, 0.f, 1.f);
	if (AATGGameMode* GM = GetWorld() ? Cast<AATGGameMode>(UGameplayStatics::GetGameMode(GetWorld())) : nullptr)
		if (AATGWorld* World = AATGWorld::Get(GM)) if (atg::Game* G = World->Game()) { G->settings.music = MusicVolume; if (G->audioSys) G->audioSys->setMusic(MusicVolume); }
}
void UATGGameInstance::SetSensitivity(float Sensitivity)
{
	MouseSensitivity = FMath::Clamp(Sensitivity, .2f, 3.f);
	if (AATGGameMode* GM = GetWorld() ? Cast<AATGGameMode>(UGameplayStatics::GetGameMode(GetWorld())) : nullptr)
		if (AATGWorld* World = AATGWorld::Get(GM)) if (atg::Game* G = World->Game()) { G->settings.sensitivity = MouseSensitivity; G->input.sensitivity = MouseSensitivity; }
}
void UATGGameInstance::SetInvertY(bool bInvert)
{
	bInvertMouseY = bInvert;
	if (AATGGameMode* GM = GetWorld() ? Cast<AATGGameMode>(UGameplayStatics::GetGameMode(GetWorld())) : nullptr)
		if (AATGWorld* World = AATGWorld::Get(GM)) if (atg::Game* G = World->Game()) { G->settings.invertY = bInvert; G->input.invertY = bInvert; }
}
void UATGGameInstance::SetReflectionsEnabled(bool bEnabled) { bReflectionsEnabled = bEnabled; }
void UATGGameInstance::SetAOEnabled(bool bEnabled) { bAOEnabled = bEnabled; }
void UATGGameInstance::SetShowFPS(bool bShow) { bShowFPS = bShow; }
void UATGGameInstance::SetNPCrimeLevel(FName Level)
{
	NPCrimeLevel = Level;
	if (AATGGameMode* GM = GetWorld() ? Cast<AATGGameMode>(UGameplayStatics::GetGameMode(GetWorld())) : nullptr)
		if (AATGWorld* World = AATGWorld::Get(GM)) if (atg::Game* G = World->Game()) G->settings.npcCrime = TCHAR_TO_UTF8(*Level.ToString());
}
void UATGGameInstance::SetTimeScale(float Scale)
{
	if (AATGGameMode* GM = GetWorld() ? Cast<AATGGameMode>(UGameplayStatics::GetGameMode(GetWorld())) : nullptr)
		if (AATGWorld* World = AATGWorld::Get(GM)) if (atg::Game* G = World->Game()) G->env.timeScale = Scale;
}
