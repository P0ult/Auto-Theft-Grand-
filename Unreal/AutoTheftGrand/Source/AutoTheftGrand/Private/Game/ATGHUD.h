// The on-screen display (src/ui/hud.js): the loading screen, the GTA V style minimap with its bars, the wanted
// stars, cash and weapon, zone and vehicle names, a speed readout, the HUD model's messages and overlays, the
// full-screen map and pause.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ATGHUD.generated.h"

class AATGWorld;
namespace atg { class Game; }
class UFont;

UCLASS()
class AATGHUD : public AHUD {
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;

private:
	double RadarRange = 110;
	double LastTime = 0;
	float Ui = 1;

	void Text(const FString& S, float X, float Y, UFont* Font, float Scale, const FLinearColor& Color, float AlignX = 0.f);
	void DrawMapLayers(AATGWorld* W, double MinX, double MinZ, double MaxX, double MaxZ, float Sx, float Sy, float Sw, float Sh);
	void Arrow(float Cx, float Cy, double Heading, float Size, const FLinearColor& Color);
	void DrawLoading(AATGWorld* W);
	void DrawBigMap(AATGWorld* W, double Px, double Pz, double Heading);
	// the HUD model's messages (help, big messages, subtitles, the bar, money, dispatch) and the full-screen
	// layers (damage vignette, white flash, the fade, the WASTED / BUSTED shard)
	// the modern (GTA V) layout: ATGHudModern.cpp
	void DrawMinimap(atg::Game* G, AATGWorld* W, float Dt, float Left, float Top);
	void DrawTopRight(atg::Game* G, float Right, float Top);
	void DrawSpeedo(atg::Game* G, float Dt);
	void DrawCrosshair(atg::Game* G);
	void DrawWheel(atg::Game* G, float Dt);
	void DrawPhone(atg::Game* G, float Dt);
	void DrawPhotoHint(float Dt);
	float PhoneT = 0, PhoneScroll = 0, PhotoT = 0;
	float WheelAlpha = 0;
	double SpeedoNeedle = 0; float SpeedoAlpha = 0;
	void DrawMessages(atg::Game* G, float Dt);
	void DrawTags(atg::Game* G); // (speech bubbles and the stars over NPC suspects)
	void DrawOverlays(atg::Game* G, float Dt);
	void WrappedBox(const FString& S, float X, float Y, float MaxW, UFont* Font, float Scale, float Alpha);
	float HelpAlpha = 0, BigAlpha = 0, SubsAlpha = 0, DeadAlpha = 1;
};
