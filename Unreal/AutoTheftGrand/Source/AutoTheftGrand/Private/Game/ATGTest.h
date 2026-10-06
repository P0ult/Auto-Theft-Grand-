// Test hooks, the Unreal side of the browser game's __step / __press / __game: console commands (ATG.*) and a
// script runner. A script is a text file with one command per line, run once the world is ready:
//
//   wait 2.5               let the game run for 2.5 seconds of game time
//   shot name              screenshot to Saved/Screenshots/ATG/name.png (with the HUD)
//   ATG.Teleport 120 -40   any console command
//   quit                   exit the game
//
// Start one with -ATGScript=path on the command line, or with the console command ATG.Script path.
// With -benchmark -fps=30 every frame advances the game by exactly 1/30 s, so runs repeat.
#pragma once

#include "CoreMinimal.h"

class UWorld;

class FATGTestScript {
public:
	// load the script named on the command line, if any
	void Init();
	bool Load(const FString& Path);
	// called every frame once the world is ready
	void Tick(UWorld* World, float Dt);
	bool IsRunning() const { return Line < Lines.Num(); }

private:
	TArray<FString> Lines;
	int32 Line = 0;
	double Wait = 0;
	int32 ShotFrames = 0;
};

namespace ATGTest {
	FATGTestScript& Script();
	// screenshot (with the HUD) to Saved/Screenshots/ATG/<Name>.png
	void Shot(const FString& Name);
}
