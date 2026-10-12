// Starts the game: spawns the world (which generates everything and creates the simulation), then every
// frame feeds the simulation the player's input, advances it, and keeps an actor drawing each of its
// vehicles and people.
#include "Game/ATGGameMode.h"
#include "AutoTheftGrand.h"
#include "Game/ATGAudio.h"
#include "Game/ATGAnimals.h"
#include "Game/ATGMissionView.h"
#include "Game/ATGCar.h"
#include "Game/ATGCoords.h"
#include "Game/ATGEffects.h"
#include "Game/ATGPickups.h"
#include "Sim/Special.h"
#include "Game/ATGPoliceView.h"
#include "Game/ATGHUD.h"
#include "Game/ATGPerson.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGTest.h"
#include "Game/ATGWorld.h"
#include "ATGMenuSystem.h"
#include "ATGGameInstance.h"
#include "Sim/Game.h"
#include "Sim/Audio.h"
#include "Sim/Missions.h"
#include "Sim/Setup.h"

#include "Components/SpotLightComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Sim/SaveSystem.h"

AATGGameMode::AATGGameMode() {
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	DefaultPawnClass = nullptr; // (the player is the simulation's; nothing is possessed)
	PlayerControllerClass = AATGPlayerController::StaticClass();
	HUDClass = AATGHUD::StaticClass();
}

void AATGGameMode::StartPlay() {
	World = AATGWorld::Get(this);
	if (!World) {
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World = GetWorld()->SpawnActor<AATGWorld>(AATGWorld::StaticClass(), FTransform::Identity, P);
	}
	Super::StartPlay();
	bManual = FParse::Param(FCommandLine::Get(), TEXT("ATGManual"));
	bMenuTest = FParse::Param(FCommandLine::Get(), TEXT("ATGMenu"));
	FString ScriptPath;
	bAutomatedScript = FParse::Value(FCommandLine::Get(), TEXT("ATGScript="), ScriptPath) && !ScriptPath.IsEmpty();
	if (bAutomatedScript) {
		ATGTest::Script().Load(ScriptPath);
	} else {
		ATGTest::Script().Init();
	}
	// the player's headlight (one spot light, reused: game.js)
	Headlight = NewObject<USpotLightComponent>(World);
	Headlight->SetupAttachment(World->GetRootComponent());
	Headlight->SetMobility(EComponentMobility::Movable);
	Headlight->SetIntensityUnits(ELightUnits::Candelas);
	Headlight->SetIntensity(0.f);
	Headlight->SetAttenuationRadius(6000.f);
	Headlight->SetOuterConeAngle(32.f);
	Headlight->SetInnerConeAngle(16.f);
	Headlight->SetCastShadows(false);
	Headlight->SetLightColor(FLinearColor(1.f, 0.95f, 0.87f));
	Headlight->RegisterComponent();

	// The menu is shown after the game has loaded, so the worker-built world can keep drawing its loading screen.
	if (!bAutomatedScript || bMenuTest) TryInitializeMenuSystem();
}

void AATGGameMode::TryInitializeMenuSystem()
{
	UGameInstance* RawGI = GetGameInstance();
	UE_LOG(LogATG, Log, TEXT("TryInitializeMenuSystem: GetGameInstance returned %s"), RawGI ? *RawGI->GetName() : TEXT("nullptr"));

	UATGGameInstance* GI = Cast<UATGGameInstance>(RawGI);
	if (!GI && RawGI)
	{
		// GameInstance exists but is wrong type - replace it
		UE_LOG(LogATG, Warning, TEXT("GameInstance is base class, replacing with UATGGameInstance"));
		RawGI->ConditionalBeginDestroy();
		GI = NewObject<UATGGameInstance>(GetGameInstance()->GetOuter());
		GetWorld()->SetGameInstance(GI);
		GI->Init(); // (the save path and settings: without it the title thinks there is no save)
		RawGI = GI;
	}
	else if (!GI)
	{
		UE_LOG(LogATG, Log, TEXT("GameInstance not ready, will retry next tick"));
		return;
	}

	UE_LOG(LogATG, Log, TEXT("GameInstance found: %s"), *GI->GetName());
	MenuSystem = NewObject<UATGMenuSystem>(this);
	if (AATGPlayerController* PC = Cast<AATGPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		MenuSystem->Initialize(PC, GI);
		MenuSystem->OnMainMenuAction.AddUObject(this, &AATGGameMode::OnMainMenuAction);
		MenuSystem->OnPauseMenuAction.AddUObject(this, &AATGGameMode::OnPauseMenuAction);
		MenuSystem->ShowMainMenu();
		bInMenu = true;
		// (the title screen: the city flyover runs, the player does not take input)
		if (atg::Game* G = World ? World->Game() : nullptr) {
			G->menuOpen = true;
			G->input.enabled = false;
			G->input.keys.clear(); G->input.pressed.clear(); G->input.released.clear();
			// main.js boot() before showTitle: dusk over the city and slow clock for the flyover
			G->env.setTime(18.35);
			G->env.timeScale = 0.25;
		}
		UE_LOG(LogATG, Log, TEXT("Main menu shown"));
	}
	else
	{
		UE_LOG(LogATG, Error, TEXT("Failed to get PlayerController for menu system"));
	}
}

void AATGGameMode::OnMainMenuAction(const FString& Action)
{
	if (!MenuSystem) return;
	if (Action == "Continue")
	{
		PendingJourneyMode = "continue";
		bInMenu = false;
		MenuSystem->HideMainMenu();
	}
	else if (Action == "NewGame")
	{
		PendingJourneyMode = "story";
		if (UATGGameInstance* GI = Cast<UATGGameInstance>(GetGameInstance())) GI->ClearSave();
		bInMenu = false;
		MenuSystem->HideMainMenu();
	}
	else if (Action == "FreeRoam")
	{
		PendingJourneyMode = "free";
		bInMenu = false;
		MenuSystem->HideMainMenu();
	}
	else if (Action == "Multiplayer")
	{
		PendingJourneyMode = "free";
		bInMenu = false;
		MenuSystem->HideMainMenu();
	}
	else if (Action == "Settings")
	{
		MenuSystem->ShowPauseMenu(TEXT("Settings"));
	}
	else if (Action == "Controls")
	{
		MenuSystem->ShowPauseMenu(TEXT("Controls"));
	}
}

void AATGGameMode::OnPauseMenuAction(const FString& Action)
{
	if (!MenuSystem) return;
	if (Action == "NewGame" || Action == "Continue" || Action == "FreeRoam" || Action == "Multiplayer")
	{
		PendingJourneyMode = Action == "Continue" ? TEXT("continue") : Action == "FreeRoam" || Action == "Multiplayer" ? TEXT("free") : TEXT("story");
		if (PendingJourneyMode == TEXT("story"))
			if (UATGGameInstance* GI = Cast<UATGGameInstance>(GetGameInstance())) GI->ClearSave();
		bInMenu = false;
		MenuSystem->HidePauseMenu();
	}
	else if (Action == "Resume")
	{
		MenuSystem->HidePauseMenu();
	}
	else if (Action == "Save")
	{
		if (UATGGameInstance* GI = Cast<UATGGameInstance>(GetGameInstance())) GI->SaveGame();
	}
	else if (Action == "Quit")
{
		PendingJourneyMode.Empty();
		bStarted = false;
		bInMenu = true;
		// a fresh city behind the title screen, exactly as reloading the page gives (freeroam.js, story and all)
		ResetSession();
		if (MenuSystem) MenuSystem->ShowMainMenu();
	}
}

void AATGGameMode::TearDownViews() {
	for (AATGCar* C : Cars) if (C) C->Destroy();
	Cars.Reset();
	for (AATGPerson* P : People) if (P) P->Destroy();
	People.Reset();
	if (Effects) { Effects->Destroy(); Effects = nullptr; }
	if (PoliceView) { PoliceView->Destroy(); PoliceView = nullptr; }
	if (PickupsView) { PickupsView->Destroy(); PickupsView = nullptr; }
	if (AnimalsView) { AnimalsView->Destroy(); AnimalsView = nullptr; }
	if (MissionView) { MissionView->Destroy(); MissionView = nullptr; }
	if (Audio) { Audio->Destroy(); Audio = nullptr; }
}

void AATGGameMode::ResetSession() {
	TearDownViews();
	if (World) World->RestartSimulation();
	bWorldUsed = false;
	// (the title screen runs the city flyover, not the player: game.js leaves input.enabled false until startGame)
	if (atg::Game* G = World ? World->Game() : nullptr) {
		G->input.enabled = false;
		G->input.keys.clear(); G->input.pressed.clear(); G->input.released.clear();
		G->menuOpen = true;
		// main.js boot() before showTitle: dusk over the city and a slow clock for the flyover
		G->env.setTime(18.35);
		G->env.timeScale = 0.25;
	}
}

void AATGGameMode::ShowPauseMenu(FName Tab)
{
	if (MenuSystem && !bInMenu)
	{
		MenuSystem->ShowPauseMenu(Tab);
	}
}

void AATGGameMode::HidePauseMenu()
{
	if (MenuSystem)
	{
		MenuSystem->HidePauseMenu();
	}
}

void AATGGameMode::StartGame(const FString& JourneyMode) {
	// A second session in the same simulation would keep the last one's free roam kit, cash, wanted level,
	// spawned cars and story state (the browser game reloads the page instead), so start a clean one.
	if (bWorldUsed) ResetSession();
	bWorldUsed = true;
	atg::Game* G = World ? World->Game() : nullptr;
	if (!G) return;
	G->menuOpen = false;
	G->paused = false;
	bStarted = true;
	double X, Y, Z, Yaw;
	World->PlayerStart(X, Y, Z, Yaw);
	atg::StartGame(*G);
	G->respawnPlayer(X, Z, Yaw);
	atg::PopulateWorld(*G);
	if (UATGGameInstance* GI = Cast<UATGGameInstance>(GetGameInstance()))
	{
		GI->InitializeSaveSystem(G, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AutoTheftGrandSave.json")));
		GI->SetFreeRoamActive(G->freeRoam);
		if (G->audioSys) G->audioSys->setVolume(GI->GetMasterVolume());
		if (G->audioSys) G->audioSys->setMusic(GI->GetMusicVolume());
		G->settings.quality = TCHAR_TO_UTF8(*GI->GetQuality());
		G->settings.volume = GI->GetMasterVolume(); G->settings.music = GI->GetMusicVolume();
		G->settings.sensitivity = GI->GetSensitivity(); G->input.sensitivity = G->settings.sensitivity;
		G->settings.invertY = GI->GetInvertY(); G->input.invertY = G->settings.invertY;
		G->settings.npcCrime = TCHAR_TO_UTF8(*GI->GetNPCrimeLevel());
	}
	if (G->hud) G->hud->help("Welcome back to Los Soles.", 4);
	// the sound
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Audio = GetWorld()->SpawnActor<AATGAudio>(AATGAudio::StaticClass(), FTransform::Identity, SP);
	if (Audio) Audio->Start(G);
	// the journey: the story (-ATGStory), the save (-ATGContinue) or free roam (-ATGFreeRoam)
	const TCHAR* Cmd = FCommandLine::Get();
	const char* Mode = "";
	if (FParse::Param(Cmd, TEXT("ATGStory"))) { Mode = "story"; }
	else if (FParse::Param(Cmd, TEXT("ATGContinue"))) { Mode = "continue"; }
	else if (FParse::Param(Cmd, TEXT("ATGFreeRoam"))) { Mode = "free"; }
	else if (!JourneyMode.IsEmpty()) { Mode = TCHAR_TO_UTF8(*JourneyMode); }
	atg::StartJourney(*G, Mode);
	if (UATGGameInstance* GI = Cast<UATGGameInstance>(GetGameInstance())) GI->SetFreeRoamActive(G->freeRoam);
	if (Mode[0]) { UE_LOG(LogATG, Log, TEXT("Journey: %s"), UTF8_TO_TCHAR(Mode)); }
	else { UE_LOG(LogATG, Log, TEXT("Player at the safehouse (%.0f, %.0f)"), X, Z); }
}

void AATGGameMode::Tick(float Dt) {
	Super::Tick(Dt);
	if (!World) return;

	// Wait for the simulation Game to be ready
	if (!World->Game())
	{
		return;
	}

	// Automated test scripts defer world simulation until generation and all scene construction finish.
	if (bAutomatedScript && !bMenuTest) {
		if (!World->IsReady()) return;
		if (!bStarted) {
			bInMenu = false;
			StartGame(FString());
		}
	} else {
		if (!World->IsReady()) return;
		if (!MenuSystem) TryInitializeMenuSystem();
		if (bInMenu) {
			// The title screen is the browser game's 'menu' state: the simulation keeps running, which drives the
			// city flyover camera (gameplay.js _flyover) and the traffic behind the menu.
			if (!bManual) Frame(FMath::Min((double)Dt, 1.0 / 20));
			else SyncViews(0);
			ATGTest::Script().Tick(GetWorld(), Dt);
			return;
		}
		if (!PendingJourneyMode.IsEmpty()) {
			StartGame(PendingJourneyMode);
			PendingJourneyMode.Empty();
		}
	}
	if (!bManual) Frame(FMath::Min((double)Dt, 1.0 / 20));
	else SyncViews(0);
	ATGTest::Script().Tick(GetWorld(), Dt);
}

void AATGGameMode::StepFrames(int32 Frames, double Dt) {
	if (!World || !World->Game()) return;
	for (int32 I = 0; I < Frames; I++) Frame(Dt);
}

void AATGGameMode::Frame(double Dt) {
	atg::Game* G = World->Game();
	if (AATGPlayerController* PC = Cast<AATGPlayerController>(GetWorld()->GetFirstPlayerController())) PC->FeedInput(*G, Dt);
	// (what the camera can see: traffic and people don't pop in on screen)
	if (UGameViewportClient* VC = GetWorld()->GetGameViewport()) { FVector2D S; VC->GetViewportSize(S); if (S.X > 0 && S.Y > 0) G->viewAspect = S.X / S.Y; }
	G->frame(Dt);
	SyncViews((float)Dt);
}

void AATGGameMode::SyncViews(float Dt) {
	atg::Game* G = World->Game();
	// the save file, for every simulation (a mission's autosave writes straight to it; a new simulation after
	// Quit to title starts without a path). Absolute: the simulation writes with the C++ library, which
	// resolves relative paths against the process's working directory
	if (G && G->saveSys && G->saveSys->path.empty()) G->saveSys->path = TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AutoTheftGrandSave.json")));
	UWorld* W = GetWorld();
	// vehicles
	for (int32 I = Cars.Num() - 1; I >= 0; I--) {
		AATGCar* C = Cars[I];
		atg::Vehicle* V = C ? C->Sim() : nullptr;
		if (!V || V->removed) { if (C) C->Destroy(); Cars.RemoveAt(I); }
	}
	for (const auto& V : G->vehicles.list) {
		bool bHave = false;
		for (AATGCar* C : Cars) if (C->IsFor(V.get())) { bHave = true; break; }
		if (!bHave) if (AATGCar* C = AATGCar::Spawn(W, V.get())) Cars.Add(C);
	}
	for (AATGCar* C : Cars) C->Sync(Dt);
	// people
	const std::vector<atg::Character*> Live = G->liveCharacters();
	for (int32 I = People.Num() - 1; I >= 0; I--) {
		bool bLive = false;
		for (atg::Character* C : Live) if (People[I] && People[I]->IsFor(C)) { bLive = true; break; }
		if (!bLive) { if (People[I]) People[I]->Destroy(); People.RemoveAt(I); }
	}
	for (atg::Character* C : Live) {
		bool bHave = false;
		for (AATGPerson* P : People) if (P->IsFor(C)) { bHave = true; break; }
		if (!bHave) if (AATGPerson* P = AATGPerson::Spawn(W, C)) People.Add(P);
	}
	for (AATGPerson* P : People) P->Sync(Dt);
	World->SetDeathLook(G->post.desat, G->post.death, G->post.deathBoost);
	World->SetFringe(G->special ? G->special->chroma : 0.0022);
	if (!Effects) { FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; Effects = W->SpawnActor<AATGEffects>(AATGEffects::StaticClass(), FTransform::Identity, P); }
	if (Effects) Effects->Sync(G);
	if (!PoliceView) { FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; PoliceView = W->SpawnActor<AATGPoliceView>(AATGPoliceView::StaticClass(), FTransform::Identity, P); }
	if (PoliceView) PoliceView->Sync(G);
	if (!PickupsView) { FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; PickupsView = W->SpawnActor<AATGPickups>(AATGPickups::StaticClass(), FTransform::Identity, P); }
	if (PickupsView) PickupsView->Sync(G);
	if (!AnimalsView) { FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; AnimalsView = W->SpawnActor<AATGAnimals>(AATGAnimals::StaticClass(), FTransform::Identity, P); }
	if (AnimalsView) AnimalsView->Sync(G);
	if (!MissionView) { FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; MissionView = W->SpawnActor<AATGMissionView>(AATGMissionView::StaticClass(), FTransform::Identity, P); }
	if (MissionView) MissionView->Sync(G);
	// the world streams round the player
	atg::Player& Pl = *G->player;
	World->SetFocus(ATG::ToUE(Pl.vehicle ? Pl.vehicle->pos : Pl.pos));
	const auto& H = G->headlight;
	Headlight->SetIntensity(H.intensity > 0 ? 3000.f : 0.f);
	if (H.intensity > 0) Headlight->SetWorldLocationAndRotation(ATG::ToUE(H.pos), (ATG::ToUE(H.target) - ATG::ToUE(H.pos)).Rotation());
}