#include "Game/ATGTest.h"
#include "AutoTheftGrand.h"
#include "Game/ATGGameMode.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGWorld.h"
#include "Sim/Effects.h"
#include "Sim/Weapons.h"
#include "Sim/Game.h"
#include "Sim/Peds.h"
#include "Sim/Police.h"
#include "Sim/Rail.h"
#include "Sim/Traffic.h"

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
	if (atg::Vehicle* V = P.vehicle) { V->pos.set(X, G->map.GroundHeight(X, Z), Z); V->yaw = H; V->vel.set(0, 0, 0); V->r = 0; return; }
	G->respawnPlayer(X, Z, H);
})

ATG_CMD(CmdTime, "ATG.Time", "ATG.Time hours: set the clock", { if (atg::Game* G = Sim(W)) if (Args.Num()) G->env.setTime(Arg(Args, 0)); })
ATG_CMD(CmdWeather, "ATG.Weather", "ATG.Weather clear|cloudy|rain|storm|fog", { if (atg::Game* G = Sim(W)) if (Args.Num()) G->env.setWeather(TCHAR_TO_UTF8(*Args[0]), true); })
ATG_CMD(CmdGod, "ATG.God", "ATG.God 0|1: the player takes no damage", { if (atg::Game* G = Sim(W)) { G->cheats.god = Arg(Args, 0, 1) != 0; G->player->invincible = G->cheats.god; } })

ATG_CMD(CmdSpawn, "ATG.Spawn", "ATG.Spawn type: a vehicle in front of the player", {
	atg::Game* G = Sim(W);
	if (!G || !Args.Num()) return;
	const atg::VehicleDef* D = atg::FindVehicle(TCHAR_TO_UTF8(*Args[0]));
	if (!D) { UE_LOG(LogATG, Warning, TEXT("ATG.Spawn: no vehicle %s"), *Args[0]); return; }
	atg::Player& P = *G->player;
	const double H = P.yaw, Ahead = 4 + D->L / 2;
	G->vehicles.spawn(D->id, P.pos.x + std::sin(H) * Ahead, P.pos.z + std::cos(H) * Ahead, H + atg::kPi / 2);
})

ATG_CMD(CmdEnter, "ATG.Enter", "ATG.Enter: sit straight in the nearest vehicle within 12 m (or get out of this one)", {
	atg::Game* G = Sim(W);
	if (!G) return;
	atg::Player& P = *G->player;
	if (P.vehicle) { G->vehicles.exit(&P); return; }
	atg::Vehicle* Best = nullptr;
	double Bd = 12;
	for (const auto& V : G->vehicles.list) { const double D = atg::Dist(V->pos.x, V->pos.z, P.pos.x, P.pos.z); if (D < Bd && !V->driver()) { Bd = D; Best = V.get(); } }
	if (Best) G->vehicles.seatNow(&P, Best, 0);
	else UE_LOG(LogATG, Warning, TEXT("ATG.Enter: no vehicle in reach"));
})

ATG_CMD(CmdPress, "ATG.Press", "ATG.Press KeyW ...: hold keys (KeyboardEvent codes) until ATG.Release", { if (AATGPlayerController* PC = PlayerPC(W)) for (const FString& K : Args) PC->ScriptKeys.Add(K); })
ATG_CMD(CmdRelease, "ATG.Release", "ATG.Release KeyW ...: let go of keys (no names: all of them)", {
	if (AATGPlayerController* PC = PlayerPC(W)) { if (!Args.Num()) PC->ScriptKeys.Empty(); for (const FString& K : Args) PC->ScriptKeys.Remove(K); }
})
ATG_CMD(CmdCam, "ATG.Cam", "ATG.Cam yaw pitch: point the camera (radians; yaw is from the player towards the camera)", {
	if (atg::Game* G = Sim(W)) { G->rig.yaw = Arg(Args, 0, G->rig.yaw); G->rig.pitch = Arg(Args, 1, G->rig.pitch); }
})
ATG_CMD(CmdStep, "ATG.Step", "ATG.Step frames [dt]: advance the game (with -ATGManual it only moves when stepped)", {
	if (AATGGameMode* GM = W ? Cast<AATGGameMode>(W->GetAuthGameMode()) : nullptr) GM->StepFrames((int32)Arg(Args, 0, 1), Arg(Args, 1, 1.0 / 30));
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
