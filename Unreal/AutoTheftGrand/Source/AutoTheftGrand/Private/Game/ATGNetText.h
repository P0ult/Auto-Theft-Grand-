// The multiplayer chat lines are the browser's html: a <b> name in its colour, <i>, and the kill feed's
// <span class="kf"> weapon. The HUD feed and the pause menu's chat log draw them as runs of text.
#pragma once

#include "CoreMinimal.h"

#include <string>

struct FATGNetRun {
	FString S;
	FLinearColor Col;
	bool bBold = false, bItalic = false, bHud = false;
};

TArray<FATGNetRun> ATGNetRuns(const FString& Html, const FLinearColor& Base, bool bItalic);
// a chat line as html: a system line as it is, a player's line with their coloured name in front
FString ATGNetLineHtml(bool bSys, const std::string& Name, const std::string& Text, uint32 Col);
