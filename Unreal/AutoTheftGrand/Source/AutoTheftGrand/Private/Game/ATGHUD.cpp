#include "Game/ATGHUD.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGWorld.h"
#include "Game/ATGCoords.h"
#include "Game/ATGPainter.h"
#include "Game/ATGNetText.h"
#include "Game/ATGGameMode.h"
#include "ATGGameInstance.h"
#include "Sim/Ragdoll.h"
#include "Sim/Game.h"
#include "Sim/Hud.h"
#include "Sim/Pets.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "RenderUtils.h"

namespace {
const FLinearColor Gold(1.f, 0.78f, 0.25f), Paper(0.95f, 0.93f, 0.88f), Ink(0.02f, 0.02f, 0.03f);
}

// the browser HUD's markup (<b>key</b>, <b style="...">, <small>, <br>) as plain text: every tag goes
static FString Plain(const std::string& S) {
	const FString In = UTF8_TO_TCHAR(S.c_str());
	FString R;
	R.Reserve(In.Len());
	bool bTag = false;
	for (const TCHAR Ch : In) {
		if (Ch == TEXT('<')) bTag = true;
		else if (Ch == TEXT('>') && bTag) bTag = false;
		else if (!bTag) R.AppendChar(Ch);
	}
	return R;
}
static FLinearColor CssHex(const std::string& S, const FLinearColor& Def) {
	if (S.size() == 7 && S[0] == '#') { const uint32 H = (uint32)std::strtoul(S.c_str() + 1, nullptr, 16); return FLinearColor(FColor((H >> 16) & 255, (H >> 8) & 255, H & 255)); }
	return Def;
}
static float Approach(float V, float Target, float Rate, float Dt) { return V < Target ? FMath::Min(Target, V + Rate * Dt) : FMath::Max(Target, V - Rate * Dt); }

static FLinearColor BlipColor(uint32 H) { return FLinearColor(FColor((H >> 16) & 255, (H >> 8) & 255, H & 255, 255)); }

void AATGHUD::Text(const FString& S, float X, float Y, UFont* Font, float Scale, const FLinearColor& Color, float AlignX) {
	if (S.IsEmpty() || !Font) return;
	float W = 0, H = 0;
	GetTextSize(S, W, H, Font, Scale);
	FCanvasTextItem Item(FVector2D(X - W * AlignX, Y), FText::FromString(S), Font, Color);
	Item.Scale = FVector2D(Scale, Scale);
	Item.EnableShadow(FLinearColor(0, 0, 0, 0.85f));
	Canvas->DrawItem(Item);
}

// CSS text: one of the browser's font stacks at Px (screen pixels), its baseline at Y, with black text-shadows
// (dx, dy, opacity) drawn under it
void AATGHUD::CssText(const FString& S, float X, float Y, float Px, const TCHAR* Face, const FLinearColor& Col, float AlignX, std::initializer_list<FVector3f> Shadows, float Spacing) {
	if (S.IsEmpty()) return;
	FATGPainter c(Canvas);
	c.LetterSpacing = Spacing; // (CSS letter-spacing, screen pixels)
	for (const FVector3f& Sh : Shadows) { c.Fill = FLinearColor(0, 0, 0, Sh.Z * Col.A); c.FontText(S, X + Sh.X, Y + Sh.Y, Px, Face, AlignX); }
	c.Fill = Col; c.FontText(S, X, Y, Px, Face, AlignX);
}

void AATGHUD::Arrow(float Cx, float Cy, double Heading, float Size, const FLinearColor& Color) {
	// forward in game axes is (sin h, cos h): on the north-up map x goes right and z goes down
	const FVector2D F(FMath::Sin(Heading), FMath::Cos(Heading)), R(-F.Y, F.X);
	const FVector2D C(Cx, Cy);
	const FVector2D A = C + F * Size, B = C - F * Size * 0.6 + R * Size * 0.7, D = C - F * Size * 0.6 - R * Size * 0.7, M = C - F * Size * 0.25;
	FCanvasTriangleItem T1(A, B, M, GWhiteTexture), T2(A, M, D, GWhiteTexture);
	T1.SetColor(Color); T2.SetColor(Color);
	Canvas->DrawItem(T1); Canvas->DrawItem(T2);
}

// the world layer, then the sharper city layer where the window overlaps it (game metres -> a screen rectangle)
void AATGHUD::DrawMapLayers(AATGWorld* W, double MinX, double MinZ, double MaxX, double MaxZ, float Sx, float Sy, float Sw, float Sh) {
	auto Layer = [&](UTexture2D* Tex, const FBox2D& Rect, bool bClip) {
		if (!Tex) return;
		double X0 = MinX, Z0 = MinZ, X1 = MaxX, Z1 = MaxZ;
		if (bClip) { X0 = FMath::Max(X0, Rect.Min.X); Z0 = FMath::Max(Z0, Rect.Min.Y); X1 = FMath::Min(X1, Rect.Max.X); Z1 = FMath::Min(Z1, Rect.Max.Y); if (X1 <= X0 || Z1 <= Z0) return; }
		const double Rw = Rect.Max.X - Rect.Min.X, Rh = Rect.Max.Y - Rect.Min.Y;
		const float U0 = (float)((X0 - Rect.Min.X) / Rw), V0 = (float)((Z0 - Rect.Min.Y) / Rh);
		const float Uw = (float)((X1 - X0) / Rw), Vh = (float)((Z1 - Z0) / Rh);
		const float Px0 = Sx + (float)((X0 - MinX) / (MaxX - MinX)) * Sw, Py0 = Sy + (float)((Z0 - MinZ) / (MaxZ - MinZ)) * Sh;
		const float Pw = (float)((X1 - X0) / (MaxX - MinX)) * Sw, Ph = (float)((Z1 - Z0) / (MaxZ - MinZ)) * Sh;
		DrawTexture(Tex, Px0, Py0, Pw, Ph, U0, V0, Uw, Vh, FLinearColor::White, BLEND_Translucent);
	};
	Layer(W->MapWorldTex, W->MapWorldRect, false);
	Layer(W->MapCityTex, W->MapCityRect, true);
}

void AATGHUD::DrawHUD() {
	Super::DrawHUD();
	if (!Canvas) return;
	Ui = Canvas->ClipY / 1080.f;
	const double Now = GetWorld()->GetRealTimeSeconds();
	const float Dt = (float)FMath::Clamp(Now - LastTime, 0.0, 0.1);
	LastTime = Now;
	AATGWorld* W = AATGWorld::Get(this);
	AATGPlayerController* PC = Cast<AATGPlayerController>(PlayerOwner);
	atg::Game* G = W ? W->Game() : nullptr;
	if (!G || !PC) { DrawLoading(W); return; }
	if (G->hudModel && atg::Finite(G->hudModel->creditsT)) { DrawCredits(G); return; }
	// (body.menu .hud { display: none }: no HUD behind the title screen)
	if (const AATGGameMode* GM = GetWorld() ? Cast<AATGGameMode>(GetWorld()->GetAuthGameMode()) : nullptr; GM && GM->IsOnTitle()) return;
	// the FPS readout (hud.js .hud-fps: 12 px monospace, green, top left, the rate over half a second)
	if (const UATGGameInstance* GI = Cast<UATGGameInstance>(GetGameInstance()); GI && GI->IsShowFPS()) {
		FpsAcc += Dt; FpsN++;
		if (FpsAcc > 0.5f) { FpsText = FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(FpsN / FpsAcc)); FpsAcc = 0; FpsN = 0; }
		FATGPainter c(Canvas);
		c.Scale(Canvas->ClipY / 720.f, Canvas->ClipY / 720.f);
		c.Fill = FLinearColor::Black; c.FontText(FpsText, 9, 17, 12, TEXT("Regular"));
		c.Fill = FLinearColor(0.f, 1.f, 0.f); c.FontText(FpsText, 8, 16, 12, TEXT("Regular"));
	}

	UFont* Big = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetSmallFont();
	atg::Player& Pl = *G->player;
	atg::Vehicle* Car = Pl.vehicle;
	const double Px = Car ? Car->pos.x : Pl.pos.x, Pz = Car ? Car->pos.z : Pl.pos.z, Heading = Car ? Car->yaw : Pl.yaw;

	if (PC->bMapOpen) { DrawBigMap(W, Px, Pz, Heading); return; }
	// Snapmatic: nothing but the hint (body.photo-mode .hud, .phone { display: none })
	if (G->photoMode) { DrawPhotoHint(Dt); return; }
	PhotoT = 0;
	// (WASTED / BUSTED: the HUD fades away over half a second)
	const bool bDead = G->hudModel && G->hudModel->dead;
	DeadAlpha = Approach(DeadAlpha, bDead ? 0.f : 1.f, 2.f, Dt);
	if (DeadAlpha <= 0) { DrawOverlays(G, Dt); return; }
	DrawTags(G);
	// the radar, bottom left: the minimap and its bars (hud-radar: 300 x 190, left 28, bottom 26, the bars 13 px under
	// it), or the classic round one (230 x 230, left 26, bottom 22); then the top-right cluster and the lines under it
	const bool bClassic = G->settings.hudStyle == "classic";
	if (bClassic) DrawMinimap(G, W, Dt, 26 * Ui, Canvas->ClipY - (22 + 230) * Ui);
	else DrawMinimap(G, W, Dt, 28 * Ui, Canvas->ClipY - (26 + 13 + 190) * Ui);
	const float TrBottom = bClassic ? DrawTopRightClassic(G, Canvas->ClipX - 26 * Ui, 18 * Ui) : DrawTopRight(G, Canvas->ClipX - 30 * Ui, 22 * Ui);
	DrawTopRightLines(G, Canvas->ClipX - (bClassic ? 26 : 30) * Ui, TrBottom, bClassic ? 2.f : 4.f, DeadAlpha);
	// zone and vehicle names, bottom right (higher in a vehicle, above the speedometer)
	const float Right = Canvas->ClipX - 30 * Ui, Bottom = Canvas->ClipY;
	if (atg::HudModel* M = G->hudModel) {
		const bool bIn = Car != nullptr;
		if (bClassic) {
			// (.hud-zone: the script font, 42 px, white, right 30, bottom 30; .hud-vehname: the HUD font, 36 px, #7ecbff,
			// bottom 84; both higher while you drive: body.in-vehicle, the driver's seat only)
			const bool bDrive = Car && Pl.seat == 0;
			if (M->zone.t > 0 && !G->phoneOpen) CssText(Plain(M->zone.text), Right, Bottom - ((bDrive ? 226 : 30) + 13) * Ui, 42 * Ui, TEXT("Script"), FLinearColor(1, 1, 1, FMath::Min(1.f, (float)M->zone.t / 0.6f) * DeadAlpha), 1.f, { FVector3f(3 * Ui, 3 * Ui, 1), FVector3f(-1 * Ui, -1 * Ui, 1) });
			if (M->veh.t > 0 && !G->phoneOpen) CssText(Plain(M->veh.text), Right, Bottom - ((bDrive ? 276 : 84) + 10) * Ui, 36 * Ui, TEXT("Hud"), FLinearColor(FColor(0x7e, 0xcb, 0xff)).CopyWithNewOpacity(FMath::Min(1.f, (float)M->veh.t / 0.6f) * DeadAlpha), 1.f, { FVector3f(3 * Ui, 3 * Ui, 1) }, 2 * Ui);
		}
		// (.hud-zone: 600 italic 30 px, white; .hud-vehname: 600 italic 26 px, #ffe08a; both with a 2 px shadow)
		else if (M->zone.t > 0 && !G->phoneOpen) CssText(Plain(M->zone.text), Right, Bottom - ((bIn ? 232 : 28) + 8) * Ui, 30 * Ui, TEXT("Text BoldItalic"), FLinearColor(1, 1, 1, FMath::Min(1.f, (float)M->zone.t * 3.f) * DeadAlpha), 1.f, { FVector3f(2 * Ui, 2 * Ui, 1) });
		if (!bClassic && M->veh.t > 0 && !G->phoneOpen) CssText(Plain(M->veh.text), Right, Bottom - ((bIn ? 274 : 70) + 7) * Ui, 26 * Ui, TEXT("Text BoldItalic"), FLinearColor(FColor(0xff, 0xe0, 0x8a)).CopyWithNewOpacity(FMath::Min(1.f, (float)M->veh.t * 3.f) * DeadAlpha), 1.f, { FVector3f(2 * Ui, 2 * Ui, 1) });
		// the radio station, top centre (hud-radio: gold, the genre small and white under it)
		if (M->radio.t > 0) {
			const float A = FMath::Min(1.f, (float)M->radio.t * 3.f) * DeadAlpha;
			// (.hud-radio: the HUD font, 34 px, gold, a 3 px shadow; small: 18 px white)
			CssText(Plain(M->radio.text), Canvas->ClipX / 2, (22 + 30) * Ui, 34 * Ui, TEXT("Hud"), FLinearColor(FColor(0xe8, 0xb6, 0x4c)).CopyWithNewOpacity(A), 0.5f, { FVector3f(3 * Ui, 3 * Ui, 1) }, 2 * Ui);
			if (!M->radioGenre.empty()) CssText(Plain(M->radioGenre), Canvas->ClipX / 2, (22 + 30 + 20) * Ui, 18 * Ui, TEXT("Hud"), FLinearColor(1, 1, 1, A), 0.5f, { FVector3f(3 * Ui, 3 * Ui, 1) }, 2 * Ui);
		}
	}

	DrawSpeedo(G, Dt);
	if (DeadAlpha > 0.5f && !G->weaponWheelOpen()) DrawCrosshair(G); // (body.wheel-open hides the crosshair)
	DrawWheel(G, Dt);
	DrawPhone(G, Dt);
	if (PC->MessageTime > 0) Text(PC->Message, Canvas->ClipX / 2, 70 * Ui, Small, 1.5f * Ui, FLinearColor(Paper.R, Paper.G, Paper.B, FMath::Min(1.f, PC->MessageTime)), 0.5f);
	DrawMessages(G, Dt);
	DrawNet(G);
	DrawOverlays(G, Dt);
	if (G->hudModel && !G->hudModel->menu.kind.empty()) DrawShopMenu(G);
	else if (G->paused && !(GetWorld() && Cast<AATGGameMode>(GetWorld()->GetAuthGameMode()) && Cast<AATGGameMode>(GetWorld()->GetAuthGameMode())->IsMenuOpen())) {
		// (the plain pause screen of a run without the menus; the pause menu draws its own)
		MenuRects.Reset();
		DrawRect(FLinearColor(0, 0, 0, 0.55f), 0, 0, Canvas->ClipX, Canvas->ClipY);
		Text(TEXT("PAUSED"), Canvas->ClipX / 2, Canvas->ClipY * 0.42f, Big, 3.f * Ui, Gold, 0.5f);
		Text(TEXT("Esc / Start to carry on"), Canvas->ClipX / 2, Canvas->ClipY * 0.42f + 90 * Ui, Small, 1.4f * Ui, Paper, 0.5f);
	}
}

void AATGHUD::DrawLoading(AATGWorld* W) {
	DrawRect(FLinearColor(FColor(0x07, 0x06, 0x0b)), 0, 0, Canvas->ClipX, Canvas->ClipY);
	const float Cx = Canvas->ClipX / 2, Cy = Canvas->ClipY / 2;
	FATGPainter c(Canvas);
	// (a column, centred: .art 34 px + 30 margin, .bar 10 + 2 x 2 border, .msg 14 margin + 22 px)
	const float ArtH = 34 * 1.2f * Ui, Col = ArtH + 30 * Ui + 14 * Ui + 14 * Ui + 22 * 1.2f * Ui, Top = Cy - Col / 2;
	CssText(TEXT("Auto Theft Grand"), Cx, Top + ArtH * 0.78f, 34 * Ui, TEXT("Script"), Gold, 0.5f, { FVector3f(3 * Ui, 3 * Ui, 1) });
	const float Bw = FMath::Min(520.f * Ui, Canvas->ClipX * 0.7f), By = Top + ArtH + 30 * Ui;
	c.Fill = FLinearColor::Black; c.FillRect(Cx - Bw / 2 - 2 * Ui, By, Bw + 4 * Ui, 14 * Ui);
	c.Fill = FLinearColor(FColor(0x1b, 0x1a, 0x22)); c.FillRect(Cx - Bw / 2, By + 2 * Ui, Bw, 10 * Ui);
	const float P = W ? W->LoadingProgress() : 0.f;
	if (P > 0) c.GradientRect(Cx - Bw / 2, By + 2 * Ui, Bw * P, 10 * Ui, FLinearColor(FColor(0x9b, 0x5b, 0x12)), Gold);
	c.Fill = FLinearColor(FColor(0xaa, 0xaa, 0xaa));
	c.LetterSpacing = 2 * Ui;
	c.FontTextBox((W ? W->LoadingText() : FString(TEXT("Starting up"))).ToUpper(), Cx, By + 14 * Ui + 14 * Ui, 22 * Ui, TEXT("Hud"), 0.5f, 0.f, 1.2f);
	c.LetterSpacing = 0;
	// .tip: 40 px from the bottom, 15 px, #888 (one of main.js's TIPS, picked when the game starts)
	static const TCHAR* Tips[] = {
		TEXT("Tap Space while turning to kick the tail out. Hold throttle to keep the drift alive."),
		TEXT("Losing the cops? Break line of sight and wait for the stars to stop flashing — or visit a Spray Shack."),
		TEXT("Headshots are instant kills on most enemies."),
		TEXT("Hidden packages are scattered across Los Soles. Find them all for a special reward at your safehouse."),
		TEXT("Walk into the green marker at your safehouse to save the game."),
		TEXT("Big Bun Burgers restores your health for $10."),
		TEXT("Press N in a car to cycle radio stations. Radio Los Soles plays West Coast classics."),
		TEXT("Lowriders have hydraulics — press G to bounce."),
		TEXT("Gun Barn in the Market District sells weapons, ammo and body armor."),
		TEXT("In free roam, press T to teleport to any town, station, airfield or landmark."),
		TEXT("Press H on the pavement to whistle for a cab. In the back, Space skips the trip."),
		TEXT("Driving a cab? Press J to pick up fares for cash."),
		TEXT("Catch the Sol Line at Union Station: ride it to Fern Creek and Dry Wells, or climb into the cab and drive."),
	};
	static const int32 Tip = FMath::RandRange(0, UE_ARRAY_COUNT(Tips) - 1);
	c.Fill = FLinearColor(FColor(0x88, 0x88, 0x88));
	c.FontTextBox(Tips[Tip], Cx, Canvas->ClipY - 40 * Ui, 15 * Ui, TEXT("Text"), 0.5f, 1.f, 1.2f);
}

void AATGHUD::DrawBigMap(AATGWorld* W, double Px, double Pz, double Heading) {
	DrawRect(FLinearColor(0.02f, 0.05f, 0.09f, 1.f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	const FBox2D& R = W->MapWorldRect;
	const double Ww = R.Max.X - R.Min.X, Wh = R.Max.Y - R.Min.Y;
	const float Margin = 40 * Ui;
	const float Scale = (float)FMath::Min((Canvas->ClipX - Margin * 2) / Ww, (Canvas->ClipY - Margin * 2) / Wh);
	const float Sw = (float)Ww * Scale, Sh = (float)Wh * Scale;
	const float Sx = (Canvas->ClipX - Sw) / 2, Sy = (Canvas->ClipY - Sh) / 2;
	DrawMapLayers(W, R.Min.X, R.Min.Y, R.Max.X, R.Max.Y, Sx, Sy, Sw, Sh);
	UFont* Small = GEngine->GetSmallFont();
	for (const FATGMapLabel& L : W->MapLabels) {
		const float Lx = Sx + (float)(L.X - R.Min.X) * Scale, Ly = Sy + (float)(L.Z - R.Min.Y) * Scale;
		Text(L.Name, Lx, Ly - 8 * Ui, Small, (L.bBig ? 1.25f : 0.95f) * Ui, L.bBig ? Gold : Paper, 0.5f);
	}
	if (atg::Game* G = W->Game()) for (const auto& B : G->blips) {
		const float Bs = (B->small ? 3.5f : 5.f) * Ui;
		DrawRect(BlipColor(B->color), Sx + (float)(B->x - R.Min.X) * Scale - Bs, Sy + (float)(B->z - R.Min.Y) * Scale - Bs, Bs * 2, Bs * 2);
	}
	Arrow(Sx + (float)(Px - R.Min.X) * Scale, Sy + (float)(Pz - R.Min.Y) * Scale, Heading, 11 * Ui, FLinearColor(1.f, 0.25f, 0.2f));
	Text(TEXT("M / Tab to close"), Canvas->ClipX - Margin, Canvas->ClipY - Margin, Small, 1.2f * Ui, Paper, 1.f);
}

// ------------------------------------------------------------------ the HUD model (hud.js messages and overlays)
// .hud-help: top 20, left 24, at most 380 px wide, padding 14 x 18, the text font at 16 px with line-height 1.4,
// a 78 % black box with 3 px corners; <b> in gold
void AATGHUD::HelpBox(const FString& Html, float Alpha) {
	struct FWord { FString S; bool bBold; };
	TArray<FWord> Words;
	{
		bool bBold = false, bTag = false;
		FString Tag, Cur;
		auto Flush = [&]() { if (!Cur.IsEmpty()) { Words.Add({ Cur, bBold }); Cur.Reset(); } };
		for (const TCHAR Ch : Html) {
			if (bTag) {
				if (Ch == TEXT('>')) {
					bTag = false;
					const FString T = Tag.TrimStartAndEnd().ToLower();
					if (T == TEXT("b") || T.StartsWith(TEXT("b "))) { Flush(); bBold = true; }
					else if (T == TEXT("/b")) { Flush(); bBold = false; }
					else if (T == TEXT("br") || T == TEXT("br/")) { Flush(); Words.Add({ TEXT("\n"), false }); }
				} else Tag.AppendChar(Ch);
				continue;
			}
			if (Ch == TEXT('<')) { bTag = true; Tag.Reset(); continue; }
			if (FChar::IsWhitespace(Ch)) { Flush(); Words.Add({ TEXT(" "), bBold }); continue; }
			Cur.AppendChar(Ch);
		}
		Flush();
	}
	const float Px = 16 * Ui, LineH = Px * 1.4f, PadX = 18 * Ui, PadY = 14 * Ui, MaxW = 380 * Ui - PadX * 2;
	FATGPainter c(Canvas);
	auto Face = [](bool b) { return b ? TEXT("Text Bold") : TEXT("Text"); };
	// lay the words out in lines
	struct FPlaced { FString S; bool bBold; float X; int32 Line; };
	TArray<FPlaced> Out;
	float X = 0, Widest = 0; int32 Line = 0;
	for (const FWord& W : Words) {
		if (W.S == TEXT("\n")) { Widest = FMath::Max(Widest, X); X = 0; Line++; continue; }
		const float Ww = c.FontWidth(W.S, Px, Face(W.bBold));
		if (W.S == TEXT(" ")) { if (X > 0) X += Ww; continue; }
		if (X > 0 && X + Ww > MaxW) { Widest = FMath::Max(Widest, X); X = 0; Line++; }
		Out.Add({ W.S, W.bBold, X, Line });
		X += Ww;
	}
	Widest = FMath::Max(Widest, X);
	const float Bx = 24 * Ui, By = 20 * Ui;
	c.Fill = FLinearColor(0, 0, 0, 0.78f * Alpha);
	c.BeginPath(); c.RoundRect(Bx, By, Widest + PadX * 2, LineH * (Line + 1) + PadY * 2, 3 * Ui); c.FillPath();
	const FLinearColor KeyGold = FLinearColor(FColor(0xe8, 0xb6, 0x4c)).CopyWithNewOpacity(Alpha), White = FLinearColor(1, 1, 1, Alpha);
	for (const FPlaced& P : Out) {
		c.Fill = P.bBold ? KeyGold : White;
		c.FontText(P.S, Bx + PadX + P.X, By + PadY + P.Line * LineH + LineH * 0.5f + Px * 0.35f, Px, Face(P.bBold));
	}
}

void AATGHUD::WrappedBox(const FString& S, float X, float Y, float MaxW, UFont* Font, float Scale, float Alpha) {
	TArray<FString> Words, Lines;
	S.ParseIntoArrayWS(Words);
	FString Cur;
	for (const FString& Wd : Words) {
		const FString Try = Cur.IsEmpty() ? Wd : Cur + TEXT(" ") + Wd;
		float W = 0, H = 0;
		GetTextSize(Try, W, H, Font, Scale);
		if (W > MaxW && !Cur.IsEmpty()) { Lines.Add(Cur); Cur = Wd; } else Cur = Try;
	}
	if (!Cur.IsEmpty()) Lines.Add(Cur);
	float Lw = 0, Lh = 0;
	GetTextSize(TEXT("Ag"), Lw, Lh, Font, Scale);
	const float PadX = 18 * Ui, PadY = 14 * Ui, LineH = Lh * 1.15f;
	float BoxW = 0;
	for (const FString& L : Lines) { float W = 0, H = 0; GetTextSize(L, W, H, Font, Scale); BoxW = FMath::Max(BoxW, W); }
	DrawRect(FLinearColor(0, 0, 0, 0.78f * Alpha), X, Y, BoxW + PadX * 2, LineH * Lines.Num() + PadY * 2);
	for (int32 I = 0; I < Lines.Num(); I++) Text(Lines[I], X + PadX, Y + PadY + I * LineH, Font, Scale, FLinearColor(Paper.R, Paper.G, Paper.B, Alpha));
}

void AATGHUD::DrawMessages(atg::Game* G, float Dt) {
	atg::HudModel* M = G->hudModel;
	if (!M) return;
	UFont* Big = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetSmallFont();
	const float A = DeadAlpha;
	// help: top left (transition 0.25 s)
	HelpAlpha = Approach(HelpAlpha, M->helpLine.t > 0 ? 1.f : 0.f, 4.f, Dt);
	if (HelpAlpha > 0 && !M->helpLine.text.empty()) HelpBox(UTF8_TO_TCHAR(M->helpLine.text.c_str()), HelpAlpha * A);
	// big message: 36% down the screen (transition 0.4 s)
	BigAlpha = Approach(BigAlpha, M->big.t > 0 ? 1.f : 0.f, 2.5f, Dt);
	if (BigAlpha > 0 && !M->big.text.empty()) {
		const std::string& St = M->big.style;
		// css .hud-big.title / passed / failed / wasted / busted / hint / chapter: the font, its size, colour and shadows
		const TCHAR* Face = TEXT("Title"); float Px = 70; uint32 Hex = 0xe8b64c;
		// (letter-spacing: passed and failed 3, wasted and busted 6, hint 2, chapter 10)
		const float Spacing = St == "wasted" || St == "busted" ? 6.f : St == "hint" ? 2.f : St == "chapter" ? 10.f : St == "title" ? 0.f : 3.f;
		const TArray<FVector3f> Drop3 = { FVector3f(3, 3, 1) };
		TArray<FVector3f> Sh = { FVector3f(4, 4, 1), FVector3f(-2, -2, 1), FVector3f(2, -2, 1), FVector3f(-2, 2, 1) };
		if (St == "title") { Face = TEXT("Script"); Px = 64; Sh = Drop3; }
		else if (St == "failed") { Px = 62; Hex = 0xdd3333; Sh = { FVector3f(4, 4, 1), FVector3f(-2, -2, 1) }; }
		else if (St == "wasted" || St == "busted") { Px = 110; Hex = St == "wasted" ? 0xb8201a : 0x2b6cd8; Sh = { FVector3f(5, 5, 1), FVector3f(-2, -2, 1), FVector3f(2, -2, 1), FVector3f(-2, 2, 1) }; }
		else if (St == "hint") { Face = TEXT("Hud"); Px = 44; Hex = 0xffffff; Sh = Drop3; }
		else if (St == "chapter") { Px = 40; Hex = 0xffffff; Sh = Drop3; }
		TArray<FVector3f> Scaled;
		for (const FVector3f& V : Sh) Scaled.Add(FVector3f(V.X * Ui, V.Y * Ui, V.Z));
		const float Top = Canvas->ClipY * 0.36f, Base = Top + Px * 0.95f * Ui;
		const FLinearColor Col = FLinearColor(FColor((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255)).CopyWithNewOpacity(BigAlpha * A);
		FATGPainter c(Canvas);
		c.LetterSpacing = Spacing * Ui;
		// (.hud-big.title div: rotate(-3deg) about its centre)
		const bool bTilt = St == "title" && Canvas->Canvas;
		if (bTilt) {
			const FVector Pivot(Canvas->ClipX / 2, Base - Px * 0.35f * Ui, 0);
			Canvas->Canvas->PushRelativeTransform(FTranslationMatrix(-Pivot) * FRotationMatrix(FRotator(0, -3, 0)) * FTranslationMatrix(Pivot));
		}
		for (const FVector3f& V : Scaled) { c.Fill = FLinearColor(0, 0, 0, V.Z * Col.A); c.FontText(Plain(M->big.text), Canvas->ClipX / 2 + V.X, Base + V.Y, Px * Ui, Face, 0.5f); }
		c.Fill = Col; c.FontText(Plain(M->big.text), Canvas->ClipX / 2, Base, Px * Ui, Face, 0.5f);
		if (bTilt) Canvas->Canvas->PopTransform();
		// (small: the HUD font 34 px white, 6 px under; a chapter's is the script, 58 px gold)
		if (!M->big.sub.empty()) {
			const bool bChapter = St == "chapter";
			const float SubPx = bChapter ? 58.f : 34.f;
			CssText(Plain(M->big.sub), Canvas->ClipX / 2, Base + (Px * 0.25f + 6 + SubPx * 0.95f) * Ui, SubPx * Ui, bChapter ? TEXT("Script") : TEXT("Hud"),
				(bChapter ? FLinearColor(FColor(0xe8, 0xb6, 0x4c)) : FLinearColor::White).CopyWithNewOpacity(BigAlpha * A), 0.5f, { FVector3f(3 * Ui, 3 * Ui, 1) }, bChapter ? 0.f : 2 * Ui);
		}
	}
	// subtitles: bottom centre
	SubsAlpha = Approach(SubsAlpha, M->subs.t > 0 ? 1.f : 0.f, 4.f, Dt);
	if (SubsAlpha > 0 && !M->subs.text.empty()) DrawSubtitles(G, SubsAlpha * A);
	// the timer, the counter and the bar, down the right edge (.hud-timer top 240, .hud-counter top 288,
	// .hud-progress top 330; right 28): the HUD font on black at 40 % with 12 px either side
	FATGPainter c(Canvas);
	const float Right = Canvas->ClipX - 28 * Ui;
	auto Boxed = [&](const TArray<TPair<FString, FLinearColor>>& Parts, float Top, float Px, float Sh) {
		float W = 0;
		for (const auto& P : Parts) W += c.FontWidth(P.Key, Px * Ui, TEXT("Hud"));
		const float H = Px * 1.2f * Ui, X0 = Right - W - 24 * Ui;
		c.Fill = FLinearColor(0, 0, 0, 0.4f * A); c.FillRect(X0, Top * Ui, W + 24 * Ui, H);
		float X = X0 + 12 * Ui;
		for (const auto& P : Parts) {
			c.Fill = FLinearColor(0, 0, 0, A); c.FontTextBox(P.Key, X + Sh * Ui, Top * Ui + Sh * Ui, Px * Ui, TEXT("Hud"), 0.f, 0.f, 1.2f);
			c.Fill = P.Value.CopyWithNewOpacity(A); X += c.FontTextBox(P.Key, X, Top * Ui, Px * Ui, TEXT("Hud"), 0.f, 0.f, 1.2f);
		}
	};
	if (atg::Finite(M->timerSeconds)) {
		const int32 Sec = FMath::Max(0, (int32)M->timerSeconds);
		Boxed({ { FString::Printf(TEXT("%d:%02d"), Sec / 60, Sec % 60), M->timerSeconds < 10 ? FLinearColor(FColor(0xff, 0x52, 0x52)) : FLinearColor::White } }, 240, 38, 3);
	}
	// (`${label} <b>${value}</b>`: the value in gold)
	if (!M->counterLabel.empty()) Boxed({ { Plain(M->counterLabel) + TEXT(" "), FLinearColor::White }, { Plain(M->counterValue), Gold } }, 288, 26, 2);
	if (M->bar.on) {
		// (.hud-progress: 200 px wide, the label right-aligned (20 px), under it a 12 px bar in a 2 px black border)
		const float Bw = 200 * Ui, Top = 330 * Ui;
		const FString Label = UTF8_TO_TCHAR(M->bar.label.c_str());
		c.Fill = FLinearColor(0, 0, 0, A); c.FontTextBox(Label, Right + 2 * Ui, Top + 2 * Ui, 20 * Ui, TEXT("Hud"), 1.f, 0.f, 1.2f);
		c.Fill = FLinearColor(1, 1, 1, A); c.FontTextBox(Label, Right, Top, 20 * Ui, TEXT("Hud"), 1.f, 0.f, 1.2f);
		const float By = Top + 20 * 1.2f * Ui + 2 * Ui;
		c.Fill = FLinearColor(0, 0, 0, A); c.FillRect(Right - Bw, By, Bw, 16 * Ui);
		c.Fill = FLinearColor(0, 0, 0, 0.7f * A); c.FillRect(Right - Bw + 2 * Ui, By + 2 * Ui, Bw - 4 * Ui, 12 * Ui);
		const FLinearColor C = CssHex(M->bar.color, FLinearColor(0.9f, 0.22f, 0.27f));
		c.Fill = C.CopyWithNewOpacity(A); c.FillRect(Right - Bw + 2 * Ui, By + 2 * Ui, (Bw - 4 * Ui) * (float)M->bar.v, 12 * Ui);
	}
}

// .hud-subtitles: the speaker's name in their colour, then the line with its coloured highlights, the text font at
// 600 and 22 px, centred and wrapped at min(900px, 80vw), line-height 1.35, its bottom 13 % up the screen (3.5 % in
// the letterbox); text-shadow 2px 2px 0 #000, -1px -1px 0 #000, 0 0 8px rgba(0,0,0,0.9)
void AATGHUD::DrawSubtitles(atg::Game* G, float Alpha) {
	atg::HudModel* M = G->hudModel;
	FString Html = UTF8_TO_TCHAR(M->subs.text.c_str());
	if (!M->subsSpeaker.empty()) {
		// (hud.js speakerColor: Dre is light blue; anyone else a hue from their name, at 70 % saturation and lightness)
		const FString Name = UTF8_TO_TCHAR(M->subsSpeaker.c_str());
		uint32 Hex = 0x8fd3ff;
		if (Name != TEXT("Dre")) {
			uint32 H = 0;
			for (const TCHAR Ch : Name) H = H * 31 + (uint32)Ch;
			const double Hue = H % 360, C = 0.42, X = C * (1 - FMath::Abs(FMath::Fmod(Hue / 60.0, 2.0) - 1)), Mn = 0.49;
			double R = 0, Gg = 0, B = 0;
			if (Hue < 60) { R = C; Gg = X; } else if (Hue < 120) { R = X; Gg = C; } else if (Hue < 180) { Gg = C; B = X; }
			else if (Hue < 240) { Gg = X; B = C; } else if (Hue < 300) { R = X; B = C; } else { R = C; B = X; }
			auto To = [](double v) { return (uint32)FMath::Clamp(FMath::RoundToInt((v) * 255), 0, 255); };
			Hex = (To(R + Mn) << 16) | (To(Gg + Mn) << 8) | To(B + Mn);
		}
		Html = FString::Printf(TEXT("<b style=\"color:#%06x\">%s:</b> "), Hex, *Name) + Html;
	}
	const float Px = 22 * Ui, LineH = Px * 1.35f, MaxW = FMath::Min(900.f * Ui, Canvas->ClipX * 0.8f);
	FATGPainter c(Canvas);
	// the words, then lines that fit
	struct FWord { FString S; FLinearColor Col; float W; };
	TArray<FWord> Words;
	for (const FATGNetRun& R : ATGNetRuns(Html, FLinearColor::White, false)) {
		int32 I = 0;
		while (I < R.S.Len()) {
			int32 E = I;
			while (E < R.S.Len() && R.S[E] == TEXT(' ')) E++;
			while (E < R.S.Len() && R.S[E] != TEXT(' ')) E++;
			while (E < R.S.Len() && R.S[E] == TEXT(' ')) E++;
			const FString S = R.S.Mid(I, E - I);
			Words.Add({ S, R.Col, c.FontWidth(S, Px, TEXT("Text Bold")) });
			I = E;
		}
	}
	TArray<TArray<int32>> Lines;
	Lines.AddDefaulted();
	float Lw = 0;
	for (int32 I = 0; I < Words.Num(); I++) {
		const float Wt = Words[I].W - c.FontWidth(Words[I].S.Right(Words[I].S.Len() - Words[I].S.TrimEnd().Len()), Px, TEXT("Text Bold"));
		if (Lw > 0 && Lw + Wt > MaxW) { Lines.AddDefaulted(); Lw = 0; }
		Lines.Last().Add(I);
		Lw += Words[I].W;
	}
	const float Bottom = Canvas->ClipY * (M->letterboxed ? 0.965f : 0.87f);
	float Y = Bottom - Lines.Num() * LineH;
	for (const TArray<int32>& L : Lines) {
		float W = 0;
		for (int32 I : L) W += Words[I].W;
		if (L.Num()) W -= Words[L.Last()].W - c.FontWidth(Words[L.Last()].S.TrimEnd(), Px, TEXT("Text Bold"));
		float X = Canvas->ClipX / 2 - W / 2;
		for (int32 I : L) {
			const FWord& Wd = Words[I];
			for (const FVector2f O : { FVector2f(2.5f, 2.5f), FVector2f(-2.5f, 2.5f), FVector2f(2.5f, -2.5f), FVector2f(-2.5f, -2.5f) }) { c.Fill = FLinearColor(0, 0, 0, 0.25f * Alpha); c.FontTextBox(Wd.S, X + O.X * Ui, Y + O.Y * Ui, Px, TEXT("Text Bold"), 0.f, 0.f, 1.35f); }
			c.Fill = FLinearColor(0, 0, 0, Alpha); c.FontTextBox(Wd.S, X + 2 * Ui, Y + 2 * Ui, Px, TEXT("Text Bold"), 0.f, 0.f, 1.35f);
			c.FontTextBox(Wd.S, X - Ui, Y - Ui, Px, TEXT("Text Bold"), 0.f, 0.f, 1.35f);
			c.Fill = Wd.Col.CopyWithNewOpacity(Alpha); c.FontTextBox(Wd.S, X, Y, Px, TEXT("Text Bold"), 0.f, 0.f, 1.35f);
			X += Wd.W;
		}
		Y += LineH;
	}
}

void AATGHUD::DrawCredits(atg::Game* G) {
	DrawRect(FLinearColor::Black, 0, 0, Canvas->ClipX, Canvas->ClipY);
	struct Row { const TCHAR* Text; float Size, Space; FLinearColor Color; };
	const FLinearColor White = FLinearColor::White, Gray(0.67f, 0.67f, 0.67f);
	const TArray<Row> Rows = {
		{TEXT("AUTO THEFT GRAND"),60,108,Gold}, {TEXT("Los Soles"),20,76,White},
		{TEXT("STARRING"),28,80,Gray}, {TEXT("Andre \"Dre\" Castillo"),20,36,White},
		{TEXT("Big Lou · Marisol Castillo · Deacon"),20,36,White}, {TEXT("Detective Frank Voss · Officer Ruiz"),20,36,White},
		{TEXT("Rico · Maddox · Salazar · Chino"),20,76,White}, {TEXT("THE CITY"),28,80,Gray},
		{TEXT("Cedar Row · Downtown · Market District · Rosewood"),20,36,White}, {TEXT("El Corona · Port Morena · Santa Luz Beach · Vistawood Hills"),20,76,White},
		{TEXT("MADE WITH"),28,80,Gray}, {TEXT("Unreal Engine 5.8 · C++"),20,36,White},
		{TEXT("Original browser game: Three.js · WebGL 2 · Web Audio"),20,36,White}, {TEXT("Every building, car, person, song and sunset is generated at runtime."),20,76,White},
		{TEXT("IN MEMORY OF"),28,80,Gray}, {TEXT("Tino Castillo"),20,76,White},
		{TEXT("THANK YOU FOR PLAYING"),28,80,Gray}, {TEXT("The city is yours. Keep exploring — there are still hidden packages to find."),20,76,White}
	};
	float Height = 0; for (const auto& R : Rows) Height += R.Space * Ui;
	const float K = FMath::Clamp((float)G->hudModel->creditsT / 40.f, 0.f, 1.f);
	float Y = (Canvas->ClipY - Height) / 2 + FMath::Lerp(Canvas->ClipY * 0.8f, -Height * 1.6f, K);
	FATGPainter c(Canvas);
	for (const auto& R : Rows) {
		c.Fill = R.Color;
		c.LetterSpacing = (R.Size >= 60 ? 4.f : R.Size >= 28 ? 3.f : 0.f) * Ui;
		c.FontTextBox(R.Text, Canvas->ClipX / 2, Y, R.Size * Ui, R.Size >= 60 ? TEXT("Title") : R.Size >= 28 ? TEXT("Hud") : TEXT("Text"), 0.5f, 0.f, 1.2f);
		Y += R.Space * Ui;
	}
}

// hud.js: the speech bubbles over people's heads and the wanted stars over NPC suspects
void AATGHUD::DrawTags(atg::Game* G) {
	if (!PlayerOwner) return;
	const atg::V3 Cam = G->rig.camPos;
	auto Screen = [&](const atg::V3& P, FVector2D& Out) { return PlayerOwner->ProjectWorldLocationToScreen(ATG::ToUE(P), Out, true); };
	auto Head = [](const atg::Character* C) { return C->ragdolling && C->ragdoll ? C->ragdoll->center() : C->pos; };
	FATGPainter c(Canvas);
	// the stars: yellow with a black edge, flashing red while the police are on them
	std::vector<atg::NpcTag> Suspects;
	if (G->npcCrime) G->npcCrime->tagged(Suspects);
	for (const atg::NpcTag& T : Suspects) {
		const atg::V3 H = T.c->vehicle ? T.c->vehicle->pos : Head(T.c);
		const double D = (H - Cam).length();
		FVector2D S;
		if (D > 90 || !Screen(atg::V3(H.x, H.y + (T.c->vehicle ? 2.1 : 2.0), H.z), S)) continue;
		const float Fs = FMath::Clamp(30 - (float)D * 0.22f, 13.f, 26.f) * Ui, R = Fs * 0.42f, Gap = Fs * 0.95f;
		const bool Red = T.hot && FMath::Fmod((float)G->time, 0.5f) >= 0.25f;
		for (int I = 0; I < T.n; I++) {
			const float X = (float)S.X + (I - (T.n - 1) / 2.f) * Gap, Y = (float)S.Y - R;
			c.BeginPath();
			for (int K = 0; K < 10; K++) { const float Rr = K % 2 ? R * 0.43f : R, A = K / 10.f * PI * 2 - PI / 2; if (K == 0) c.MoveTo(X + FMath::Cos(A) * Rr, Y + FMath::Sin(A) * Rr); else c.LineTo(X + FMath::Cos(A) * Rr, Y + FMath::Sin(A) * Rr); }
			c.ClosePath();
			c.Fill = Red ? CssColor(0xff5a4a) : CssColor(0xf7d154); c.FillPath();
			c.Stroke = FLinearColor::Black; c.LineWidth = 1.5f * Ui; c.StrokePath();
		}
	}
	// speech: a white rounded bubble with a tail, bold dark text, fading over its last half second
	if (atg::HudModel* M = G->hudModel) for (const auto& Sp : M->speeches) {
		const atg::Character* C = Sp.who.get();
		if (!C || C->removed) continue;
		const atg::V3 H = Head(C);
		FVector2D S;
		if ((H - Cam).length() > 35 || !Screen(atg::V3(H.x, H.y + 2.1, H.z), S)) continue;
		c.Alpha = FMath::Min(1.f, (float)Sp.t * 2.f);
		const FString Str = Plain(Sp.text);
		const float Fs = 14 * Ui, W = c.FontWidth(Str, Fs, TEXT("Text Bold")) + 20 * Ui, Hh = Fs * 1.2f + 10 * Ui;
		const float X = (float)S.X - W / 2, Y = (float)S.Y - Hh - 6 * Ui;
		c.Fill = FLinearColor(1, 1, 1, 0.92f);
		c.BeginPath(); c.RoundRect(X, Y, W, Hh, 12 * Ui); c.FillPath();
		c.BeginPath(); c.MoveTo((float)S.X - 6 * Ui, Y + Hh); c.LineTo((float)S.X + 6 * Ui, Y + Hh); c.LineTo((float)S.X, Y + Hh + 6 * Ui); c.ClosePath(); c.FillPath();
		c.Fill = CssColor(0x111111);
		c.FontTextBox(Str, (float)S.X, Y + Hh / 2, Fs, TEXT("Text Bold"), 0.5f, 0.5f, 1.2f);
		c.Alpha = 1;
	}
}

void AATGHUD::DrawOverlays(atg::Game* G, float Dt) {
	atg::HudModel* M = G->hudModel;
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	if ((M && M->letterboxed) || G->rig.cineBars) { const float K = M && M->letterboxed ? 0.11f : 0.08f; DrawRect(FLinearColor::Black, 0, 0, W, H * K); DrawRect(FLinearColor::Black, 0, H * (1 - K), W, H * K); }
	// damage: red creeping in from the edges (postfx.js uDamage)
	const float Dmg = M ? (float)M->vignette : 0.f;
	if (Dmg > 0.01f) for (int32 K = 0; K < 10; K++) {
		const float T = K / 10.f, Edge = 0.03f + T * 0.17f;
		const FLinearColor C(0.55f, 0, 0, Dmg * 0.8f * 0.12f * (1 - T));
		DrawRect(C, 0, 0, W, H * Edge); DrawRect(C, 0, H * (1 - Edge), W, H * Edge);
		DrawRect(C, 0, H * Edge, W * Edge, H * (1 - 2 * Edge)); DrawRect(C, W * (1 - Edge), H * Edge, W * Edge, H * (1 - 2 * Edge));
	}
	// the white flash (uFlash)
	if (G->post.flash > 0.001) DrawRect(FLinearColor(1, 1, 1, (float)FMath::Clamp(G->post.flash, 0.0, 1.0)), 0, 0, W, H);
	// the GTA V "shard": a dark band across the middle, the word in red scaling down into place
	if (M && !M->shard.empty()) {
		const float T = (float)M->shardT;
		const float BandA = FMath::Clamp(T / 0.35f, 0.f, 1.f);
		const float Bh = FMath::Clamp(0.18f * H, 104.f * Ui, 180.f * Ui), By = H / 2 - Bh / 2;
		DrawRect(FLinearColor(0, 0, 0, 0.3f * BandA), 0, By, W, Bh * 0.14f);
		DrawRect(FLinearColor(0, 0, 0, 0.6f * BandA), 0, By + Bh * 0.14f, W, Bh * 0.72f);
		DrawRect(FLinearColor(0, 0, 0, 0.3f * BandA), 0, By + Bh * 0.86f, W, Bh * 0.14f);
		const float K = FMath::Clamp(T / 0.5f, 0.f, 1.f), E = 1 - FMath::Pow(1 - K, 3.f);
		// (font-size: clamp(66px, 12.5vh, 128px), colour #c3272b)
		// (the title font, lower case, line-height 1; text-shadows 2 px down-right and a 1 px outline)
		const FString Word = FString(UTF8_TO_TCHAR(M->shard.c_str())).ToLower();
		const float Px = FMath::Clamp(0.125f * H, 66.f * Ui, 128.f * Ui) * (1.5f - 0.5f * E), Alpha = FMath::Clamp(K / 0.6f, 0.f, 1.f);
		FLinearColor Red = FLinearColor(FColor(0xc3, 0x27, 0x2b)); Red.A = Alpha;
		CssText(Word, W / 2, H / 2 + Px * 0.36f, Px, TEXT("Title"), Red, 0.5f,
			{ FVector3f(2 * Ui, 2 * Ui, 0.9f), FVector3f(-Ui, -Ui, 0.7f), FVector3f(Ui, -Ui, 0.7f), FVector3f(-Ui, Ui, 0.7f) }, Ui); // (letter-spacing 1px)
	}
	// the black fade
	const double F = M ? M->fadeState.value() : 0;
	if (F > 0.001) DrawRect(FLinearColor(0, 0, 0, (float)F), 0, 0, W, H);
}
