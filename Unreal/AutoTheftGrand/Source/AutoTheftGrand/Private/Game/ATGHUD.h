// The on-screen display (a first cut of src/ui/hud.js): the loading screen, a north-up radar drawn from
// the generated map, zone name and clock, the speedometer, messages, the full-screen map and pause.
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
	FString LastZone;
	float ZoneTime = 0;
	double LastTime = 0;
	float Ui = 1;

	void Text(const FString& S, float X, float Y, UFont* Font, float Scale, const FLinearColor& Color, float AlignX = 0.f);
	void DrawMapLayers(AATGWorld* W, double MinX, double MinZ, double MaxX, double MaxZ, float Sx, float Sy, float Sw, float Sh);
	void Arrow(float Cx, float Cy, double Heading, float Size, const FLinearColor& Color);
	void DrawLoading(AATGWorld* W);
	void DrawRadar(AATGWorld* W, double Px, double Pz, double Heading, float Dt, double CarSpeed);
	void DrawBigMap(AATGWorld* W, double Px, double Pz, double Heading);
	// the HUD model's messages (help, big messages, subtitles, the bar, money, dispatch) and the full-screen
	// layers (damage vignette, white flash, the fade, the WASTED / BUSTED shard)
	void DrawMessages(atg::Game* G, float Dt);
	void DrawOverlays(atg::Game* G, float Dt);
	void WrappedBox(const FString& S, float X, float Y, float MaxW, UFont* Font, float Scale, float Alpha);
	float HelpAlpha = 0, BigAlpha = 0, SubsAlpha = 0, DeadAlpha = 1;
};
