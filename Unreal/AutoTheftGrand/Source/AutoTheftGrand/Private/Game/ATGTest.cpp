#include "Game/ATGTest.h"
#include "AutoTheftGrand.h"
#include "Game/ATGCar.h"
#include "Game/ATGCharacter.h"
#include "Game/ATGCoords.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGWorld.h"
#include "Gen/Models.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
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
AATGPlayerController* PlayerPC(UWorld* W) { return W ? Cast<AATGPlayerController>(W->GetFirstPlayerController()) : nullptr; }

FAutoConsoleCommandWithWorldAndArgs CmdScript(TEXT("ATG.Script"), TEXT("ATG.Script path: run a test script"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*) { if (Args.Num()) ATGTest::Script().Load(Args[0]); }));

FAutoConsoleCommandWithWorldAndArgs CmdShot(TEXT("ATG.Shot"), TEXT("ATG.Shot name: screenshot to Saved/Screenshots/ATG/name.png"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*) { ATGTest::Shot(Args.Num() ? Args[0] : FString(TEXT("shot"))); }));

FAutoConsoleCommandWithWorldAndArgs CmdTeleport(TEXT("ATG.Teleport"), TEXT("ATG.Teleport x z [heading]: move the player (or their car) to game coordinates"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W) {
		AATGPlayerController* PC = PlayerPC(W);
		AATGWorld* Wd = AATGWorld::Get(W);
		if (!PC || !PC->Body || !Wd || Args.Num() < 2) return;
		const double X = FCString::Atod(*Args[0]), Z = FCString::Atod(*Args[1]);
		const double H = Args.Num() > 2 ? FCString::Atod(*Args[2]) : PC->Body->Heading();
		const double Y = Wd->GroundHeight(X, Z);
		if (AATGCar* C = PC->CurrentCar()) { C->X = X; C->Z = Z; C->Y = Y; C->Yaw = H; C->VelX = C->VelZ = 0; return; }
		PC->Body->SetActorLocationAndRotation(ATG::ToUE(X, Y + 1.0, Z), FRotator(0, ATG::HeadingYaw(H), 0));
		PC->Body->GetCharacterMovement()->StopMovementImmediately();
		Wd->SetFocus(ATG::ToUE(X, Y, Z));
	}));

FAutoConsoleCommandWithWorldAndArgs CmdTime(TEXT("ATG.Time"), TEXT("ATG.Time hours: set the clock"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W) {
		if (AATGWorld* Wd = AATGWorld::Get(W)) if (Args.Num()) Wd->Hours = FCString::Atof(*Args[0]);
	}));

FAutoConsoleCommandWithWorldAndArgs CmdSpawn(TEXT("ATG.Spawn"), TEXT("ATG.Spawn type: a car in front of the player"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W) {
		AATGPlayerController* PC = PlayerPC(W);
		if (!PC || !PC->Body || !Args.Num()) return;
		const atg::CarDef* D = atg::FindCar(TCHAR_TO_UTF8(*Args[0]));
		if (!D) { UE_LOG(LogATG, Warning, TEXT("ATG.Spawn: no vehicle %s"), *Args[0]); return; }
		double X, Y, Z;
		PC->Body->GamePos(X, Y, Z);
		const double H = PC->Body->Heading(), Ahead = 4 + D->L / 2;
		AATGCar::SpawnCar(W, Args[0], X + FMath::Sin(H) * Ahead, Z + FMath::Cos(H) * Ahead, H + UE_DOUBLE_PI / 2, 0, true);
	}));

FAutoConsoleCommandWithWorldAndArgs CmdEnter(TEXT("ATG.Enter"), TEXT("ATG.Enter: get into the nearest car (or out of this one)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W) {
		AATGPlayerController* PC = PlayerPC(W);
		if (!PC || !PC->Body) return;
		if (AATGCar* C = PC->CurrentCar()) { PC->Body->ExitCar(); PC->Possess(PC->Body); return; }
		// (any car within 12 m: a test doesn't walk to the door first)
		double X, Y, Z;
		PC->Body->GamePos(X, Y, Z);
		AATGCar* Near = nullptr;
		double Best = 12;
		for (AATGCar* C : AATGCar::All()) {
			const double D = FMath::Sqrt(FMath::Square(C->X - X) + FMath::Square(C->Z - Z));
			if (!C->Driver && D < Best) { Best = D; Near = C; }
		}
		if (Near) { PC->Body->EnterCar(Near); PC->Possess(Near); }
		else UE_LOG(LogATG, Warning, TEXT("ATG.Enter: no car in reach"));
	}));

FAutoConsoleCommandWithWorldAndArgs CmdDrive(TEXT("ATG.Drive"), TEXT("ATG.Drive throttle steer [brake]: hold the car's controls (until changed)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W) {
		AATGPlayerController* PC = PlayerPC(W);
		if (!PC) return;
		PC->ScriptDrive[0] = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
		PC->ScriptDrive[1] = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f;
		PC->ScriptDrive[2] = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.f;
		PC->bScriptDrive = Args.Num() > 0;
	}));

FAutoConsoleCommandWithWorldAndArgs CmdWalk(TEXT("ATG.Walk"), TEXT("ATG.Walk forward right: hold the move stick (relative to the camera)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W) {
		AATGPlayerController* PC = PlayerPC(W);
		if (!PC) return;
		PC->ScriptMove[0] = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
		PC->ScriptMove[1] = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f;
		PC->bScriptMove = Args.Num() > 0;
	}));

FAutoConsoleCommandWithWorldAndArgs CmdState(TEXT("ATG.State"), TEXT("ATG.State: log the player's position, vehicle and speed"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W) {
		AATGPlayerController* PC = PlayerPC(W);
		if (!PC || !PC->Body) { UE_LOG(LogATG, Display, TEXT("ATG state: no player")); return; }
		if (AATGCar* C = PC->CurrentCar())
			UE_LOG(LogATG, Display, TEXT("ATG state: driving %s at (%.2f, %.2f, %.2f) yaw %.3f speed %.2f m/s health %.0f"), *C->Type, C->X, C->Y, C->Z, C->Yaw, C->Speed(), C->Health)
		else {
			double X, Y, Z;
			PC->Body->GamePos(X, Y, Z);
			UE_LOG(LogATG, Display, TEXT("ATG state: on foot at (%.2f, %.2f, %.2f) heading %.3f"), X, Y, Z, PC->Body->Heading());
		}
	}));
}
