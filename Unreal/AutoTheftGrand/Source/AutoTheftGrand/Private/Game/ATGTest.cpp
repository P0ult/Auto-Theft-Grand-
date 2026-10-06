#include "Game/ATGTest.h"
#include "AutoTheftGrand.h"
#include "Game/ATGGameMode.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGWorld.h"
#include "Sim/Game.h"

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
#undef ATG_CMD
}
