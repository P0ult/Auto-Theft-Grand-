#include "Game/ATGGameMode.h"
#include "AutoTheftGrand.h"
#include "Game/ATGCar.h"
#include "Game/ATGCoords.h"
#include "Game/ATGEffects.h"
#include "Game/ATGPickups.h"
#include "Game/ATGPoliceHeli.h"
#include "Game/ATGHUD.h"
#include "Game/ATGPerson.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGTest.h"
#include "Game/ATGWorld.h"
#include "Sim/Game.h"
#include "Sim/Setup.h"

#include "Components/SpotLightComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
	ATGTest::Script().Init();
	bManual = FParse::Param(FCommandLine::Get(), TEXT("ATGManual"));
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
}

void AATGGameMode::StartGame() {
	atg::Game* G = World->Game();
	bStarted = true;
	double X, Y, Z, Yaw;
	World->PlayerStart(X, Y, Z, Yaw);
	atg::StartGame(*G);
	G->respawnPlayer(X, Z, Yaw);
	atg::PopulateWorld(*G);
	if (G->hud) G->hud->help("Welcome back to Los Soles.", 4);
	UE_LOG(LogATG, Log, TEXT("Player at the safehouse (%.0f, %.0f)"), X, Z);
}

void AATGGameMode::Tick(float Dt) {
	Super::Tick(Dt);
	if (!World || !World->Game()) return;
	if (!bStarted) StartGame();
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
	if (!Effects) { FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; Effects = W->SpawnActor<AATGEffects>(AATGEffects::StaticClass(), FTransform::Identity, P); }
	if (Effects) Effects->Sync(G);
	if (!PoliceHeli) { FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; PoliceHeli = W->SpawnActor<AATGPoliceHeli>(AATGPoliceHeli::StaticClass(), FTransform::Identity, P); }
	if (PoliceHeli) PoliceHeli->Sync(G);
	if (!PickupsView) { FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; PickupsView = W->SpawnActor<AATGPickups>(AATGPickups::StaticClass(), FTransform::Identity, P); }
	if (PickupsView) PickupsView->Sync(G);
	// the world streams round the player
	atg::Player& Pl = *G->player;
	World->SetFocus(ATG::ToUE(Pl.vehicle ? Pl.vehicle->pos : Pl.pos));
	const auto& H = G->headlight;
	Headlight->SetIntensity(H.intensity > 0 ? 3000.f : 0.f);
	if (H.intensity > 0) Headlight->SetWorldLocationAndRotation(ATG::ToUE(H.pos), (ATG::ToUE(H.target) - ATG::ToUE(H.pos)).Rotation());
}
