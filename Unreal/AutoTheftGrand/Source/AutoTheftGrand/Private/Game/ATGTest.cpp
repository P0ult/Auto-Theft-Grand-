#include "Game/ATGTest.h"
#include "AutoTheftGrand.h"
#include "Game/ATGGameMode.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGWorld.h"
#include "Sim/Aircraft.h"
#include "Sim/Army.h"
#include "Sim/Audio.h"
#include "Sim/Effects.h"
#include "Sim/Weapons.h"
#include "Sim/Game.h"
#include "Sim/Peds.h"
#include "Sim/Heists.h"
#include "Sim/Hud.h"
#include "Sim/Player.h"
#include "Sim/NpcCrime.h"
#include "Sim/Police.h"
#include "Sim/Roadblocks.h"
#include "Sim/Rail.h"
#include "Sim/Traffic.h"
#include "Sim/Wildlife.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

FATGTestScript& ATGTest::Script() { static FATGTestScript S; return S; }

void ATGTest::Shot(const FString& Name) {
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("ATG") / (Name + TEXT(".png")));
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	UE_LOG(LogATG, Display, TEXT("ATG shot %s"), *Path);
}

void FATGTestScript::Init() {
	FString Path;
	if (FParse::Value(FCommandLine::Get(), TEXT("ATGScript="), Path)) Load(Path);
}

bool FATGTestScript::Load(const FString& Path) {
	TArray<FString> Raw;
	if (!FFileHelper::LoadFileToStringArray(Raw, *Path)) { UE_LOG(LogATG, Error, TEXT("ATG script %s not found"), *Path); return false; }
	Lines.Reset();
	for (FString& L : Raw) { L.TrimStartAndEndInline(); if (!L.IsEmpty() && !L.StartsWith(TEXT("#"))) Lines.Add(L); }
	Line = 0; Wait = 0;
	UE_LOG(LogATG, Display, TEXT("ATG script %s: %d commands"), *Path, Lines.Num());
	return true;
}

void FATGTestScript::Tick(UWorld* World, float Dt) {
	if (ShotFrames > 0) { ShotFrames--; return; } // (let a screenshot finish before going on)
	if (Wait > 0) { Wait -= Dt; return; }
	while (Line < Lines.Num()) {
		const FString Cmd = Lines[Line++];
		UE_LOG(LogATG, Display, TEXT("ATG script> %s"), *Cmd);
		if (Cmd.StartsWith(TEXT("wait "))) { Wait = FCString::Atod(*Cmd.Mid(5)); return; }
		if (Cmd.StartsWith(TEXT("shot "))) { ATGTest::Shot(Cmd.Mid(5).TrimStartAndEnd()); ShotFrames = 3; return; }
		if (Cmd == TEXT("quit")) { UE_LOG(LogATG, Display, TEXT("ATG script done")); FPlatformMisc::RequestExit(false, TEXT("ATG script")); return; }
		if (GEngine) GEngine->Exec(World, *Cmd);
	}
}

// ------------------------------------------------------------------ console commands
namespace {
atg::Game* Sim(UWorld* W) { AATGWorld* Wd = AATGWorld::Get(W); return Wd ? Wd->Game() : nullptr; }
AATGPlayerController* PlayerPC(UWorld* W) { return W ? Cast<AATGPlayerController>(W->GetFirstPlayerController()) : nullptr; }
#define ATG_CMD(Var, Name, Help, ...) FAutoConsoleCommandWithWorldAndArgs Var(TEXT(Name), TEXT(Help), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W) __VA_ARGS__));
double Arg(const TArray<FString>& A, int32 I, double Def = 0) { return A.IsValidIndex(I) ? FCString::Atod(*A[I]) : Def; }

ATG_CMD(CmdScript, "ATG.Script", "ATG.Script path: run a test script", { if (Args.Num()) ATGTest::Script().Load(Args[0]); })
ATG_CMD(CmdShot, "ATG.Shot", "ATG.Shot name: screenshot to Saved/Screenshots/ATG/name.png", { ATGTest::Shot(Args.Num() ? Args[0] : FString(TEXT("shot"))); })

ATG_CMD(CmdTeleport, "ATG.Teleport", "ATG.Teleport x z [heading]: move the player (or their vehicle) to game coordinates", {
	atg::Game* G = Sim(W);
	if (!G || Args.Num() < 2) return;
	const double X = Arg(Args, 0), Z = Arg(Args, 1);
	atg::Player& P = *G->player;
	const double H = Args.Num() > 2 ? Arg(Args, 2) : (P.vehicle ? P.vehicle->yaw : P.yaw);
	if (atg::Vehicle* V = P.vehicle) {
		V->pos.set(X, G->map.GroundHeight(X, Z), Z); V->yaw = H; V->vel.set(0, 0, 0); V->r = 0;
		if (atg::AirVehicle* A = dynamic_cast<atg::AirVehicle*>(V)) { A->quat = atg::Quat::FromAxisAngle(atg::V3(0, 1, 0), H); A->grounded = true; A->angVel = atg::V3(); }
		return;
	}
	G->respawnPlayer(X, Z, H);
})

ATG_CMD(CmdTime, "ATG.Time", "ATG.Time hours: set the clock", { if (atg::Game* G = Sim(W)) if (Args.Num()) G->env.setTime(Arg(Args, 0)); })
ATG_CMD(CmdWeather, "ATG.Weather", "ATG.Weather clear|cloudy|rain|storm|fog", { if (atg::Game* G = Sim(W)) if (Args.Num()) G->env.setWeather(TCHAR_TO_UTF8(*Args[0]), true); })
ATG_CMD(CmdGod, "ATG.God", "ATG.God 0|1: the player takes no damage", { if (atg::Game* G = Sim(W)) { G->cheats.god = Arg(Args, 0, 1) != 0; G->player->invincible = G->cheats.god; } })

ATG_CMD(CmdSpawn, "ATG.Spawn", "ATG.Spawn type [heading]: a vehicle in front of the player (side-on, or facing heading)", {
	atg::Game* G = Sim(W);
	if (!G || !Args.Num()) return;
	const atg::VehicleDef* D = atg::FindVehicle(TCHAR_TO_UTF8(*Args[0]));
	if (!D) { UE_LOG(LogATG, Warning, TEXT("ATG.Spawn: no vehicle %s"), *Args[0]); return; }
	atg::Player& P = *G->player;
	const double H = P.yaw, Ahead = 4 + D->L / 2;
	G->vehicles.spawn(D->id, P.pos.x + std::sin(H) * Ahead, P.pos.z + std::cos(H) * Ahead, Args.Num() > 1 ? Arg(Args, 1) : H + atg::kPi / 2);
})

ATG_CMD(CmdEnter, "ATG.Enter", "ATG.Enter: sit straight in the nearest vehicle within 12 m of its ends (or get out of this one)", {
	atg::Game* G = Sim(W);
	if (!G) return;
	atg::Player& P = *G->player;
	if (P.vehicle) { G->vehicles.exit(&P); return; }
	atg::Vehicle* Best = nullptr;
	double Bd = 12;
	for (const auto& V : G->vehicles.list) { const double D = atg::Dist(V->pos.x, V->pos.z, P.pos.x, P.pos.z) - V->def.L / 2; if (D < Bd && !V->driver()) { Bd = D; Best = V.get(); } }
	if (Best) G->vehicles.seatNow(&P, Best, 0);
	else UE_LOG(LogATG, Warning, TEXT("ATG.Enter: no vehicle in reach"));
})

ATG_CMD(CmdPress, "ATG.Press", "ATG.Press KeyW ...: hold keys (KeyboardEvent codes) until ATG.Release", { if (AATGPlayerController* PC = PlayerPC(W)) for (const FString& K : Args) PC->ScriptKeys.Add(K); })
ATG_CMD(CmdRelease, "ATG.Release", "ATG.Release KeyW ...: let go of keys (no names: all of them)", {
	if (AATGPlayerController* PC = PlayerPC(W)) { if (!Args.Num()) PC->ScriptKeys.Empty(); for (const FString& K : Args) PC->ScriptKeys.Remove(K); }
})
ATG_CMD(CmdMouseMove, "ATG.MouseMove", "ATG.MouseMove dx dy: move the mouse (browser pixels, y down) in the next frame", { if (AATGPlayerController* PC = PlayerPC(W)) PC->ScriptMouse(Arg(Args, 0), Arg(Args, 1)); })
ATG_CMD(CmdCamAt, "ATG.CamAt", "ATG.CamAt dx dy dz [tx ty tz]: hold the camera at the player plus (dx, dy, dz), looking at the player plus (tx, ty, tz) (no args: let go)", {
	atg::Game* G = Sim(W);
	if (!G) return;
	if (Args.Num() < 3) { G->rig.clearCinematic(); return; }
	const atg::V3 P = G->player->vehicle ? G->player->vehicle->pos : G->player->pos;
	const atg::V3 T(Args.Num() > 5 ? Arg(Args, 3) : 0, Args.Num() > 5 ? Arg(Args, 4) : 1, Args.Num() > 5 ? Arg(Args, 5) : 0);
	G->rig.setCinematic(P + atg::V3(Arg(Args, 0), Arg(Args, 1), Arg(Args, 2)), P + T, 50);
})
ATG_CMD(CmdCam, "ATG.Cam", "ATG.Cam yaw pitch: point the camera (radians; yaw is from the player towards the camera)", {
	if (atg::Game* G = Sim(W)) { G->rig.yaw = Arg(Args, 0, G->rig.yaw); G->rig.pitch = Arg(Args, 1, G->rig.pitch); }
})
ATG_CMD(CmdStep, "ATG.Step", "ATG.Step frames [dt]: advance the game (with -ATGManual it only moves when stepped)", {
	if (AATGGameMode* GM = W ? Cast<AATGGameMode>(W->GetAuthGameMode()) : nullptr) GM->StepFrames((int32)Arg(Args, 0, 1), Arg(Args, 1, 1.0 / 30));
})

ATG_CMD(CmdAudio, "ATG.Audio", "ATG.Audio: log the sound's clock (seconds rendered) and how many nodes are live", {
	atg::Game* G = Sim(W);
	if (!G || !G->audioSys || !G->audioSys->ctx) { UE_LOG(LogATG, Display, TEXT("ATG audio: off")); return; }
	UE_LOG(LogATG, Display, TEXT("ATG audio: clock %.2f s, %d nodes, radio %s"), G->audioSys->clock(), (int32)G->audioSys->ctx->nodeCount(), UTF8_TO_TCHAR(G->audioSys->radioLabel().c_str()));
})
ATG_CMD(CmdAnimal, "ATG.Animal", "ATG.Animal breed [dx dz]: place an animal beside the player", {
	atg::Game* G = Sim(W);
	auto* S = G ? dynamic_cast<atg::Wildlife*>(G->wildlife) : nullptr;
	if (!S || Args.IsEmpty()) return;
	const std::string B = TCHAR_TO_UTF8(*Args[0]);
	if (!atg::AnimalBreeds().count(B)) return;
	const auto P = G->player->pos;
	const double X = P.x + Arg(Args, 1, 0), Z = P.z + Arg(Args, 2, 8);
	auto A = std::make_shared<atg::Animal>(*G, B, X, Z, true, G->collision->floorHeight(X, Z, P.y + 3), true, 0);
	A->state = A->sp.bird ? "peck" : "graze"; S->list.push_back(A);
})
ATG_CMD(CmdAnimals, "ATG.Animals", "ATG.Animals [clear | scare]: log, clear or scare nearby wildlife", {
	atg::Game* G = Sim(W);
	auto* S = G ? dynamic_cast<atg::Wildlife*>(G->wildlife) : nullptr;
	if (!S) return;
	if (!Args.IsEmpty() && Args[0] == TEXT("clear")) S->clear();
	if (!Args.IsEmpty() && Args[0] == TEXT("scare")) S->scare(G->player->pos, 90);
	UE_LOG(LogATG, Display, TEXT("ATG animals: %d animals, %.1f ambient weight"), (int32)S->all().size(), S->count());
	for (const auto* A : S->all()) UE_LOG(LogATG, Display, TEXT("ATG animal: %s %s at (%.2f, %.2f, %.2f) speed %.2f dead %d"), UTF8_TO_TCHAR(A->breed.c_str()), UTF8_TO_TCHAR(A->state.c_str()), A->pos.x, A->pos.y, A->pos.z, A->speed, A->dead);
})
ATG_CMD(CmdMenu, "ATG.Menu", "ATG.Menu [row | down | up | close]: press a shop menu's button, move its highlight or leave; then log the menu", {
	atg::Game* G = Sim(W);
	atg::HudModel* M = G ? G->hudModel : nullptr;
	if (!M) return;
	if (Args.Num()) {
		if (Args[0] == TEXT("close")) M->closeOverlay();
		else if (Args[0] == TEXT("down")) M->menuMove(1);
		else if (Args[0] == TEXT("up")) M->menuMove(-1);
		else M->menuPress(FCString::Atoi(*Args[0]));
	}
	if (M->menu.kind.empty()) { UE_LOG(LogATG, Display, TEXT("ATG menu: closed, money %.0f"), G->player->money); return; }
	UE_LOG(LogATG, Display, TEXT("ATG menu: %s, focus %d, money %.0f, note '%s'"), UTF8_TO_TCHAR(M->menu.title.c_str()), M->menu.focus, G->player->money, UTF8_TO_TCHAR(M->menu.note.c_str()));
	for (int32 I = 0; I < (int32)M->menu.rows.size(); I++) UE_LOG(LogATG, Display, TEXT("ATG menu:   %d %s [%s]"), I, UTF8_TO_TCHAR(M->menu.rows[I].name.c_str()), UTF8_TO_TCHAR(M->menu.rows[I].button.c_str()));
})
ATG_CMD(CmdState, "ATG.State", "ATG.State: log the player's position, vehicle and speed", {
	atg::Game* G = Sim(W);
	if (!G) { UE_LOG(LogATG, Display, TEXT("ATG state: no game")); return; }
	atg::Player& P = *G->player;
	if (atg::Vehicle* V = P.vehicle)
		UE_LOG(LogATG, Display, TEXT("ATG state: t %.2f driving %s at (%.2f, %.2f, %.2f) yaw %.3f speed %.2f m/s health %.0f"), G->time, UTF8_TO_TCHAR(V->type.c_str()), V->pos.x, V->pos.y, V->pos.z, V->yaw, V->speed(), V->health)
	else UE_LOG(LogATG, Display, TEXT("ATG state: t %.2f on foot at (%.2f, %.2f, %.2f) yaw %.3f health %.0f vehicles %d"), G->time, P.pos.x, P.pos.y, P.pos.z, P.yaw, P.health, (int32)G->vehicles.list.size());
})
ATG_CMD(CmdStation, "ATG.Station", "ATG.Station dry|fern|union: stand on that station's platform, facing the track", {
	atg::Game* G = Sim(W);
	if (!G || !G->rail || !Args.Num()) return;
	const int32 I = G->rail->station(TCHAR_TO_UTF8(*Args[0]));
	if (I < 0) { UE_LOG(LogATG, Warning, TEXT("ATG.Station: no station %s"), *Args[0]); return; }
	const atg::RailStop& S = G->rail->stations[I];
	G->respawnPlayer(S.x, S.z, S.rot - atg::kPi / 2);
})
ATG_CMD(CmdCity, "ATG.City", "ATG.City: log the traffic, the people and the trains", {
	atg::Game* G = Sim(W);
	if (!G) return;
	int32 Moving = 0, Cars = 0;
	if (G->traffic) for (auto& C : G->traffic->cars) if (atg::Vehicle* V = C.get()) { Cars++; if (V->speedAbs() > 2) Moving++; }
	UE_LOG(LogATG, Display, TEXT("ATG city: %d traffic cars (%d moving), %d people, %d vehicles"), Cars, Moving, G->peds ? (int32)G->peds->list.size() : 0, (int32)G->vehicles.list.size());
	if (G->rail) for (atg::Train* T : G->rail->trains()) UE_LOG(LogATG, Display, TEXT("ATG city: %s at s %.0f, %.1f m/s, (%.0f, %.0f)"), UTF8_TO_TCHAR(T->type.c_str()), T->s, T->v, T->pos.x, T->pos.z);
})
ATG_CMD(CmdKill, "ATG.Kill", "ATG.Kill: the player dies (WASTED)", {
	if (atg::Game* G = Sim(W)) { atg::DamageInfo D; D.type = "fall"; G->player->invincible = false; G->cheats.god = false; G->player->takeDamage(10000, D); }
})
ATG_CMD(CmdBust, "ATG.Bust", "ATG.Bust: the police arrest the player (BUSTED)", { if (atg::Game* G = Sim(W)) G->events.busted.emit(); })
ATG_CMD(CmdExplode, "ATG.Explode", "ATG.Explode [metres] [radius]: an explosion that far in front of the player (effects only)", {
	if (atg::Game* G = Sim(W)) if (G->effects) {
		atg::Player& P = *G->player;
		const double D = Arg(Args, 0, 15), R = Arg(Args, 1, 6.75);
		const atg::V3 At(P.pos.x + std::sin(P.yaw) * D, G->map.GroundHeight(P.pos.x + std::sin(P.yaw) * D, P.pos.z + std::cos(P.yaw) * D) + 0.5, P.pos.z + std::cos(P.yaw) * D);
		G->effects->explosion(At, R);
	}
})
ATG_CMD(CmdSmoke, "ATG.Wreck", "ATG.Wreck health: the nearest car's health (below 400 it smokes, below 150 black smoke and fire)", {
	if (atg::Game* G = Sim(W)) {
		atg::Vehicle* Best = nullptr; double Bd = 30;
		for (const auto& V : G->vehicles.list) { const double D = atg::Dist(V->pos.x, V->pos.z, G->player->pos.x, G->player->pos.z); if (D < Bd) { Bd = D; Best = V.get(); } }
		if (Best) { Best->health = Arg(Args, 0, 100); Best->onFire = Best->health < 150; }
	}
})
ATG_CMD(CmdFx, "ATG.Fx", "ATG.Fx: log the effects pools", {
	if (atg::Game* G = Sim(W)) if (atg::Effects* F = dynamic_cast<atg::Effects*>(G->effects)) {
		UE_LOG(LogATG, Display, TEXT("ATG fx: %d smoke, %d fire, %d dots, %d emitters; player (%.1f, %.1f, %.1f) cam (%.1f, %.1f, %.1f)"), (int32)F->alphaPool.parts.size(), (int32)F->addPool.parts.size(), (int32)F->dotAlpha.parts.size(), (int32)F->emitters.size(), G->player->pos.x, G->player->pos.y, G->player->pos.z, G->rig.camPos.x, G->rig.camPos.y, G->rig.camPos.z);
		if (!F->addPool.parts.empty()) { const atg::Particle& P = F->addPool.parts.back(); UE_LOG(LogATG, Display, TEXT("ATG fx: last fire at (%.1f, %.1f, %.1f) size %.2f a %.2f col %.1f %.1f %.1f"), P.x, P.y, P.z, P.size, P.a, P.col[0], P.col[1], P.col[2]); }
	}
})
ATG_CMD(CmdGive, "ATG.Give", "ATG.Give weapon [ammo]: give the player a weapon and hold it", {
	if (atg::Game* G = Sim(W)) if (Args.Num()) { const std::string Id = TCHAR_TO_UTF8(*Args[0]); if (atg::FindWeapon(Id)) { G->player->giveWeapon(Id, Arg(Args, 1, 200)); G->player->equip(Id); } }
})
ATG_CMD(CmdMoney, "ATG.Money", "ATG.Money amount: set the player's cash", { if (atg::Game* G = Sim(W)) G->player->money = Arg(Args, 0, 1000); })
ATG_CMD(CmdWanted, "ATG.Wanted", "ATG.Wanted stars: set the wanted level (0 clears it)", {
	if (atg::Game* G = Sim(W)) if (G->policeSys) { const int L = (int)Arg(Args, 0, 1); if (L <= 0) G->policeSys->clear(); else G->policeSys->setLevel(L); }
})
ATG_CMD(CmdNoBust, "ATG.NoBust", "ATG.NoBust 0|1: the police can't arrest the player (as in some missions)", { if (atg::Game* G = Sim(W)) G->missionNoBust = Arg(Args, 0, 1) != 0; })
ATG_CMD(CmdCamHeli, "ATG.CamHeli", "ATG.CamHeli [0]: hold the camera behind the player looking at the police helicopter (0: let go)", {
	atg::Game* G = Sim(W);
	if (!G) return;
	if (Args.Num() && Arg(Args, 0) == 0) { G->rig.clearCinematic(); return; }
	if (!G->policeSys || !G->policeSys->heli) return;
	const atg::V3 H = G->policeSys->heli->pos, P = G->player->pos;
	atg::V3 Back = P - H; Back.y = 0; Back = Back.normalized();
	G->rig.setCinematic(P + Back * 8 + atg::V3(0, 3, 0), H, 50);
})
ATG_CMD(CmdCamArmy, "ATG.CamArmy", "ATG.CamArmy heli|tank|truck|jeep (0: let go): hold the camera behind the player looking at an army unit", {
	atg::Game* G = Sim(W);
	atg::Army* A = G ? dynamic_cast<atg::Army*>(G->army) : nullptr;
	if (!A || !Args.Num()) return;
	if (Args[0] == TEXT("0")) { G->rig.clearCinematic(); return; }
	const std::string K = TCHAR_TO_UTF8(*Args[0]);
	for (const auto& U : A->units) if (U.kind == K) if (atg::Vehicle* V = U.veh.get()) {
		const atg::V3 T = V->cgPoint(), P = G->player->vehicle ? G->player->vehicle->pos : G->player->pos;
		atg::V3 Back = P - T; Back.y = 0; Back = Back.normalized();
		G->rig.setCinematic(P + Back * 9 + atg::V3(0, 3, 0), T, 50);
		return;
	}
	UE_LOG(LogATG, Warning, TEXT("ATG.CamArmy: no %s"), *Args[0]);
})
ATG_CMD(CmdRoadblock, "ATG.Roadblock", "ATG.Roadblock: the police close the road ahead of the player's car now", {
	atg::Game* G = Sim(W);
	atg::Roadblocks* R = G ? dynamic_cast<atg::Roadblocks*>(G->roadblocks) : nullptr;
	if (!R || !G->player->vehicle) return;
	const bool Ok = R->place(G->player->vehicle);
	UE_LOG(LogATG, Display, TEXT("ATG roadblock: %s, %d standing"), Ok ? TEXT("placed") : TEXT("nowhere to put it"), (int32)R->blocks.size());
	for (const auto& B : R->blocks) UE_LOG(LogATG, Display, TEXT("ATG roadblock at (%.1f, %.2f, %.1f), %d cars, %d cops, spike %d at (%.1f, %.2f, %.1f) len %.1f, car y %.2f"), B.x, B.y, B.z, (int32)B.cars.size(), (int32)B.cops.size(), B.hasSpike ? 1 : 0, B.spike.x, B.spike.y, B.spike.z, B.spike.len, G->player->vehicle->pos.y);
})
ATG_CMD(CmdCamRoadblock, "ATG.CamRoadblock", "ATG.CamRoadblock [spike|0]: look at the first roadblock from in front of its spike strip (spike: at the strip; 0: let go)", {
	atg::Game* G = Sim(W);
	atg::Roadblocks* R = G ? dynamic_cast<atg::Roadblocks*>(G->roadblocks) : nullptr;
	if (!R) return;
	if ((Args.Num() && Args[0] == TEXT("0")) || R->blocks.empty()) { G->rig.clearCinematic(); return; }
	const auto& B = R->blocks[0];
	if (Args.Num() && Args[0] == TEXT("spike") && B.hasSpike) {
		const auto& S = B.spike;
		G->rig.setCinematic(atg::V3(S.x - B.tx * 5 + B.ux * 1.5, S.y + 1.4, S.z - B.tz * 5 + B.uz * 1.5), atg::V3(S.x, S.y, S.z), 50);
		return;
	}
	const atg::V3 At(B.x, B.y + 0.8, B.z);
	G->rig.setCinematic(atg::V3(B.x - B.tx * 24 + B.ux * 3, B.y + 2.5, B.z - B.tz * 24 + B.uz * 3), At, 50);
})
ATG_CMD(CmdCrime, "ATG.Crime", "ATG.Crime mug|steal|speed|jaywalk|assault: stage a street crime near the player (assault: a suspect in front of you, seen by a patrol)", {
	atg::Game* G = Sim(W);
	atg::NpcCrime* N = G ? dynamic_cast<atg::NpcCrime*>(G->npcCrime) : nullptr;
	if (!N || !Args.Num()) return;
	const std::string K = TCHAR_TO_UTF8(*Args[0]);
	if (K == "assault") {
		atg::Player& P = *G->player;
		atg::Ped* S = G->peds->spawnPed(P.pos.x + std::sin(P.yaw) * 7, P.pos.z + std::cos(P.yaw) * 7);
		atg::Vehicle* Car = G->policeSys ? G->policeSys->spawnCar(false, &S->pos) : nullptr;
		atg::NpcCrime::CommitOpts O; O.hasWitness = true; O.witness = Car;
		atg::NpcCase* R = N->commit(S, "assault", O);
		if (R && Args.Num() > 1) R->force = TCHAR_TO_UTF8(*Args[1]);
		UE_LOG(LogATG, Display, TEXT("ATG crime: assault, case %s, unit %s"), R ? TEXT("open") : TEXT("refused"), Car ? TEXT("on its way") : TEXT("none"));
		return;
	}
	UE_LOG(LogATG, Display, TEXT("ATG crime: %s %s"), *Args[0], N->stage(K) ? TEXT("staged") : TEXT("found nobody"));
})
ATG_CMD(CmdCrimes, "ATG.Crimes", "ATG.Crimes: log the open street-crime cases", {
	atg::Game* G = Sim(W);
	atg::NpcCrime* N = G ? dynamic_cast<atg::NpcCrime*>(G->npcCrime) : nullptr;
	if (!N) return;
	for (const auto& R : N->cases) UE_LOG(LogATG, Display, TEXT("ATG case: %s, %d stars, %s, phase %s, reaction %s, unit %d"), UTF8_TO_TCHAR(R->crime.c_str()), R->stars, R->known ? TEXT("known") : TEXT("unknown"), UTF8_TO_TCHAR(R->phase.c_str()), UTF8_TO_TCHAR(R->reaction.c_str()), R->unit ? 1 : 0);
	UE_LOG(LogATG, Display, TEXT("ATG crimes: %d open"), (int32)N->cases.size());
})
ATG_CMD(CmdHeist, "ATG.Heist", "ATG.Heist [here|open]: an armoured van on a lane nearby (here: parked 12 m ahead of the player; open: shoot its doors open)", {
	atg::Game* G = Sim(W);
	atg::Heists* H = G ? dynamic_cast<atg::Heists*>(G->system("heists")) : nullptr;
	if (!H) return;
	const FString A = Args.Num() ? Args[0] : FString();
	if (A == TEXT("open")) {
		if (!H->van || !H->van->v.get()) return;
		atg::Vehicle* V = H->van->v.get();
		const double Fx = std::sin(V->yaw), Fz = std::cos(V->yaw);
		const atg::V3 Back(V->pos.x - Fx * (V->def.L / 2 - 0.3), V->pos.y + V->def.clearance + 0.8, V->pos.z - Fz * (V->def.L / 2 - 0.3));
		for (int I = 0; I < 4; I++) G->events.vehicleShot.emit(V, G->player.get(), Back);
	} else if (!H->van && H->spawn() && A == TEXT("here")) {
		atg::Vehicle* V = H->van->v.get();
		atg::Player& P = *G->player;
		V->pos.set(P.pos.x + std::sin(P.yaw) * 12, V->pos.y, P.pos.z + std::cos(P.yaw) * 12);
		V->pos.y = G->map.GroundHeight(V->pos.x, V->pos.z);
		V->yaw = P.yaw; V->vel.set(0, 0, 0);
	}
	if (H->van) if (atg::Vehicle* V = H->van->v.get()) UE_LOG(LogATG, Display, TEXT("ATG heist: %s at (%.1f, %.1f), state %s, %d bags"), UTF8_TO_TCHAR(V->def.id.c_str()), V->pos.x, V->pos.z, UTF8_TO_TCHAR(H->van->state.c_str()), (int32)H->van->cash.size());
})
ATG_CMD(CmdPolice, "ATG.Police", "ATG.Police: log the wanted level, the units and the helicopter", {
	atg::Game* G = Sim(W);
	atg::Police* P = G ? G->policeSys : nullptr;
	if (!P) return;
	const atg::V3 Pp = G->player->pos;
	double Near = 1e9;
	for (auto& C : P->cars) if (atg::Vehicle* V = C.get()) Near = FMath::Min(Near, atg::Hypot(V->pos.x - Pp.x, V->pos.z - Pp.z));
	UE_LOG(LogATG, Display, TEXT("ATG police: level %d heat %.2f seen %d flash %d, %d cars (nearest %.0f m), %d cops, heli %s"), P->level, P->heat, P->seen ? 1 : 0, P->flash ? 1 : 0, (int32)P->cars.size(), Near, (int32)P->cops.size(),
		P->heli ? *FString::Printf(TEXT("at (%.0f, %.0f, %.0f) health %.0f%s"), P->heli->pos.x, P->heli->pos.y, P->heli->pos.z, P->heli->health, P->heli->down ? TEXT(" down") : TEXT("")) : TEXT("none"));
})
#undef ATG_CMD
}
