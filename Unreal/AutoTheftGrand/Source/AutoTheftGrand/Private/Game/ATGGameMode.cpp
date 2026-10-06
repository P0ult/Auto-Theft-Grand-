#include "Game/ATGGameMode.h"
#include "AutoTheftGrand.h"
#include "Game/ATGCar.h"
#include "Game/ATGCharacter.h"
#include "Game/ATGCoords.h"
#include "Game/ATGHUD.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGTest.h"
#include "Game/ATGWorld.h"
#include "Gen/CityMap.h"
#include "Gen/Models.h"

#include "Engine/World.h"
#include "EngineUtils.h"

AATGGameMode::AATGGameMode() {
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr; // (the player's character is spawned once the world is built)
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
}

void AATGGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) {
	// nothing yet: players get their character when the world is ready (Tick)
}

void AATGGameMode::Tick(float Dt) {
	Super::Tick(Dt);
	if (!World || !World->IsReady()) return;
	SpawnPlayers();
	if (AATGPlayerController* PC = Cast<AATGPlayerController>(GetWorld()->GetFirstPlayerController())) if (PC->Body) ATGTest::Script().Tick(GetWorld(), Dt);
	StreamTimer -= Dt;
	if (StreamTimer <= 0) { StreamTimer = 0.5; StreamParked(); }
}

void AATGGameMode::SpawnPlayers() {
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) {
		AATGPlayerController* PC = Cast<AATGPlayerController>(It->Get());
		if (!PC || PC->Body) continue;
		double X, Y, Z, Yaw;
		World->PlayerStart(X, Y, Z, Yaw);
		// the ground's collision is cooked in the background: wait for it (up to a few seconds) so the
		// player doesn't drop through the floor
		WaitForGround += GetWorld()->GetDeltaSeconds();
		FHitResult Hit;
		const FCollisionQueryParams Q(SCENE_QUERY_STAT(ATGSpawn), true);
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, ATG::ToUE(X, Y + 3, Z), ATG::ToUE(X, Y - 3, Z), FCollisionObjectQueryParams(ECC_WorldStatic), Q) && WaitForGround < 5) continue;
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AATGCharacter* C = GetWorld()->SpawnActor<AATGCharacter>(AATGCharacter::StaticClass(), ATG::ToUE(X, Y + 1.0, Z), FRotator(0, ATG::HeadingYaw(Yaw), 0), P);
		if (!C) continue;
		PC->Body = C;
		PC->Possess(C);
		PC->ShowMessage(TEXT("Welcome to San Andreas. F to get in a car, V to spawn one, M for the map."), 8.f);
		UE_LOG(LogATG, Log, TEXT("Player spawned at the safehouse (%.0f, %.0f)"), X, Z);
	}
}

void AATGGameMode::StreamParked() {
	const atg::CityMap* Map = World->Map();
	AATGPlayerController* PC = Cast<AATGPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!Map || !PC || !PC->Body) return;
	double Px, Py, Pz;
	if (AATGCar* Mine = PC->CurrentCar()) { Px = Mine->X; Pz = Mine->Z; }
	else PC->Body->GamePos(Px, Py, Pz);
	auto Dist2 = [](double Ax, double Az, double Bx, double Bz) { return (Ax - Bx) * (Ax - Bx) + (Az - Bz) * (Az - Bz); };
	// forget cars that have been used, remove the far ones
	for (auto It = Parked.CreateIterator(); It; ++It) {
		AATGCar* V = It.Value().Get();
		if (!V) { It.RemoveCurrent(); continue; }
		const bool bTouched = !V->bParked || V->Driver || V->Health < 1000;
		if (bTouched) { Consumed.Add(It.Key()); It.RemoveCurrent(); continue; }
		if (Dist2(V->X, V->Z, Px, Pz) > 170.0 * 170.0) { V->Destroy(); It.RemoveCurrent(); }
	}
	// cars left behind somewhere far away
	for (AATGCar* V : TArray<AATGCar*>(AATGCar::All())) {
		if (V->Driver || Dist2(V->X, V->Z, Px, Pz) < 450.0 * 450.0) continue;
		bool bTracked = false;
		for (const auto& P : Parked) if (P.Value.Get() == V) { bTracked = true; break; }
		if (!bTracked) V->Destroy();
	}
	if (Consumed.Num() > 200) Consumed.Empty();
	if (Parked.Num() >= MaxParked) return;
	// vehicledefs.js TRAFFIC_POOL (everything with a rarity, but not the box truck)
	std::vector<std::pair<std::string, double>> Pool;
	for (const atg::CarDef& D : atg::CarDefs()) if (D.rarity > 0 && std::string(D.id) != "boxer") Pool.push_back({ D.id, (double)D.rarity });
	int32 Added = 0;
	const std::vector<atg::ParkingSpot>& Spots = Map->parkingSpots;
	for (int32 I = 0; I < (int32)Spots.size() && Added < 3; I++) {
		const atg::ParkingSpot& S = Spots[I];
		const double D2 = Dist2(S.x, S.z, Px, Pz);
		if (D2 > 110.0 * 110.0 || D2 < 30.0 * 30.0) continue;
		if (Parked.Contains(I) || Consumed.Contains(I)) continue;
		if (atg::Hash2(I, 77) > 0.5) continue;
		bool bBlocked = false;
		for (AATGCar* V : AATGCar::All()) if (Dist2(V->X, V->Z, S.x, S.z) < 36) { bBlocked = true; break; }
		if (bBlocked) continue;
		atg::RNG Rng((uint32_t)(I * 31 + 7));
		std::string Type;
		if (S.police) Type = "police";
		else if (S.fancy) Type = Rng.Pick(std::vector<std::string>{ "zenith", "kestrel", "summit" });
		else if (S.district == "hood") Type = Rng.Weighted(std::vector<std::pair<std::string, double>>{ { "bouncer", 3 }, { "meridian", 4 }, { "brawler", 2 }, { "hauler", 2 }, { "summit", 1 } });
		else if (S.district == "docks") Type = Rng.Weighted(std::vector<std::pair<std::string, double>>{ { "boxer", 2 }, { "parcel", 3 }, { "hauler", 3 } });
		else { Rng.Chance(0.12); Type = Rng.Weighted(Pool); } // (bikes come later)
		if (AATGCar* V = AATGCar::SpawnCar(GetWorld(), FString(UTF8_TO_TCHAR(Type.c_str())), S.x, S.z, S.rot, 0, true)) {
			Parked.Add(I, V);
			Added++;
			if (Parked.Num() >= MaxParked) break;
		}
	}
}
