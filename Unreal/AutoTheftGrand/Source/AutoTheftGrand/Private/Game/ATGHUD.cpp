#include "Game/ATGHUD.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGWorld.h"
#include "Sim/Game.h"
#include "Sim/Hud.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "RenderUtils.h"

namespace {
const FLinearColor Gold(1.f, 0.78f, 0.25f), Paper(0.95f, 0.93f, 0.88f), Ink(0.02f, 0.02f, 0.03f);
}

// the browser HUD's markup (<b>key</b>, <small>) as plain text
static FString Plain(const std::string& S) {
	FString R = UTF8_TO_TCHAR(S.c_str());
	for (const TCHAR* Tag : { TEXT("<b>"), TEXT("</b>"), TEXT("<small>"), TEXT("</small>"), TEXT("<br>") }) R.ReplaceInline(Tag, TEXT(""));
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

	UFont* Big = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetSmallFont();
	atg::Player& Pl = *G->player;
	atg::Vehicle* Car = Pl.vehicle;
	const double Px = Car ? Car->pos.x : Pl.pos.x, Pz = Car ? Car->pos.z : Pl.pos.z, Heading = Car ? Car->yaw : Pl.yaw;

	if (PC->bMapOpen) { DrawBigMap(W, Px, Pz, Heading); return; }
	// (WASTED / BUSTED: the HUD fades away over half a second)
	const bool bDead = G->hudModel && G->hudModel->dead;
	DeadAlpha = Approach(DeadAlpha, bDead ? 0.f : 1.f, 2.f, Dt);
	if (DeadAlpha <= 0) { DrawOverlays(G, Dt); return; }
	// the minimap and its bars, bottom left (hud-radar: 300 x 190, left 28, bottom 26, the bars 13 px under it)
	DrawMinimap(G, W, Dt, 28 * Ui, Canvas->ClipY - (26 + 13 + 190) * Ui);
	DrawTopRight(G, Canvas->ClipX - 30 * Ui, 22 * Ui);
	// zone and vehicle names, bottom right (higher in a vehicle, above the speedometer)
	const float Right = Canvas->ClipX - 30 * Ui, Bottom = Canvas->ClipY;
	if (atg::HudModel* M = G->hudModel) {
		const bool bIn = Car != nullptr;
		if (M->zone.t > 0) Text(Plain(M->zone.text), Right, Bottom - ((bIn ? 232 : 28) + 34) * Ui, Big, 1.45f * Ui, FLinearColor(1, 1, 1, FMath::Min(1.f, (float)M->zone.t * 3.f) * DeadAlpha), 1.f);
		if (M->veh.t > 0) Text(Plain(M->veh.text), Right, Bottom - ((bIn ? 274 : 70) + 30) * Ui, Big, 1.25f * Ui, FLinearColor(FColor(0xff, 0xe0, 0x8a)).CopyWithNewOpacity(FMath::Min(1.f, (float)M->veh.t * 3.f) * DeadAlpha), 1.f);
	}

	DrawSpeedo(G, Dt);
	if (PC->MessageTime > 0) Text(PC->Message, Canvas->ClipX / 2, 70 * Ui, Small, 1.5f * Ui, FLinearColor(Paper.R, Paper.G, Paper.B, FMath::Min(1.f, PC->MessageTime)), 0.5f);
	DrawMessages(G, Dt);
	DrawOverlays(G, Dt);
	if (G->paused) {
		DrawRect(FLinearColor(0, 0, 0, 0.55f), 0, 0, Canvas->ClipX, Canvas->ClipY);
		Text(TEXT("PAUSED"), Canvas->ClipX / 2, Canvas->ClipY * 0.42f, Big, 3.f * Ui, Gold, 0.5f);
		Text(TEXT("Esc / Start to carry on"), Canvas->ClipX / 2, Canvas->ClipY * 0.42f + 90 * Ui, Small, 1.4f * Ui, Paper, 0.5f);
	}
}

void AATGHUD::DrawLoading(AATGWorld* W) {
	DrawRect(Ink, 0, 0, Canvas->ClipX, Canvas->ClipY);
	UFont* Big = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetSmallFont();
	const float Cx = Canvas->ClipX / 2, Cy = Canvas->ClipY / 2;
	Text(TEXT("AUTO THEFT GRAND"), Cx, Cy - 90 * Ui, Big, 3.2f * Ui, Gold, 0.5f);
	Text(W ? W->LoadingText() : FString(TEXT("Starting")), Cx, Cy + 20 * Ui, Small, 1.5f * Ui, Paper, 0.5f);
	// a simple "working" bar
	const float Bw = 360 * Ui, T = (float)FMath::Fmod(GetWorld()->GetRealTimeSeconds() * 0.6, 1.0);
	DrawRect(FLinearColor(1, 1, 1, 0.12f), Cx - Bw / 2, Cy + 70 * Ui, Bw, 4 * Ui);
	DrawRect(Gold, Cx - Bw / 2 + Bw * 0.8f * T, Cy + 70 * Ui, Bw * 0.2f, 4 * Ui);
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
	if (HelpAlpha > 0 && !M->helpLine.text.empty()) WrappedBox(Plain(M->helpLine.text), 24 * Ui, 20 * Ui, 380 * Ui, Small, 1.35f * Ui, HelpAlpha * A);
	// big message: 36% down the screen (transition 0.4 s)
	BigAlpha = Approach(BigAlpha, M->big.t > 0 ? 1.f : 0.f, 2.5f, Dt);
	if (BigAlpha > 0 && !M->big.text.empty()) {
		const std::string& St = M->big.style;
		const FLinearColor Col = St == "failed" ? FLinearColor(0.83f, 0.2f, 0.2f) : St == "wasted" ? FLinearColor(0.72f, 0.13f, 0.1f) : St == "busted" ? FLinearColor(0.17f, 0.42f, 0.85f) : St == "hint" ? Paper : Gold;
		const float Y = Canvas->ClipY * 0.36f;
		Text(Plain(M->big.text), Canvas->ClipX / 2, Y, Big, (St == "title" ? 3.2f : 3.6f) * Ui, FLinearColor(Col.R, Col.G, Col.B, BigAlpha * A), 0.5f);
		if (!M->big.sub.empty()) Text(Plain(M->big.sub), Canvas->ClipX / 2, Y + 90 * Ui, Big, 1.7f * Ui, FLinearColor(1, 1, 1, BigAlpha * A), 0.5f);
	}
	// subtitles: bottom centre
	SubsAlpha = Approach(SubsAlpha, M->subs.t > 0 ? 1.f : 0.f, 4.f, Dt);
	if (SubsAlpha > 0 && !M->subs.text.empty()) {
		const FString S = M->subsSpeaker.empty() ? Plain(M->subs.text) : FString(UTF8_TO_TCHAR(M->subsSpeaker.c_str())) + TEXT(": ") + Plain(M->subs.text);
		Text(S, Canvas->ClipX / 2, Canvas->ClipY * 0.86f, Small, 1.6f * Ui, FLinearColor(1, 1, 1, SubsAlpha * A), 0.5f);
	}
	// the bar (drift, mission progress)
	if (M->bar.on) {
		const float Bw = 200 * Ui, Bh = 15 * Ui, Bx = Canvas->ClipX - 40 * Ui - Bw, By = 130 * Ui;
		Text(UTF8_TO_TCHAR(M->bar.label.c_str()), Bx - 10 * Ui, By - 3 * Ui, Small, 1.2f * Ui, FLinearColor(1, 1, 1, A), 1.f);
		DrawRect(FLinearColor(0, 0, 0, 0.75f * A), Bx, By, Bw, Bh);
		const FLinearColor C = CssHex(M->bar.color, FLinearColor(0.9f, 0.22f, 0.27f));
		DrawRect(FLinearColor(C.R, C.G, C.B, A), Bx, By, Bw * (float)M->bar.v, Bh);
	}
	if (M->money.t > 0) Text(UTF8_TO_TCHAR(M->money.text.c_str()), Canvas->ClipX - 40 * Ui, 112 * Ui, Small, 1.6f * Ui, FLinearColor(0.49f, 1.f, 0.54f, FMath::Min(1.f, (float)M->money.t * 3.f) * A), 1.f);
	if (M->dispatchLine.t > 0) Text(TEXT("DISPATCH ") + Plain(M->dispatchLine.text), Canvas->ClipX - 40 * Ui, 160 * Ui, Small, 1.3f * Ui, FLinearColor(0.87f, 0.9f, 1.f, FMath::Min(1.f, (float)M->dispatchLine.t * 3.f) * A), 1.f);
	if (!M->objectiveText.empty() && M->subs.t <= 0) Text(Plain(M->objectiveText), Canvas->ClipX / 2, Canvas->ClipY * 0.92f, Small, 1.3f * Ui, FLinearColor(1, 1, 1, 0.8f * A), 0.5f);
}

void AATGHUD::DrawOverlays(atg::Game* G, float Dt) {
	atg::HudModel* M = G->hudModel;
	const float W = Canvas->ClipX, H = Canvas->ClipY;
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
		const FString Word = UTF8_TO_TCHAR(M->shard.c_str());
		float Tw = 0, Th = 0;
		GetTextSize(Word, Tw, Th, GEngine->GetLargeFont(), 1.f);
		const float Want = FMath::Clamp(0.125f * H, 66.f * Ui, 128.f * Ui);
		const float Scale = (1.5f - 0.5f * E) * Want / FMath::Max(1.f, Th), Alpha = FMath::Clamp(K / 0.6f, 0.f, 1.f);
		GetTextSize(Word, Tw, Th, GEngine->GetLargeFont(), Scale);
		FLinearColor Red = FLinearColor(FColor(0xc3, 0x27, 0x2b)); Red.A = Alpha;
		Text(Word, W / 2, H / 2 - Th / 2, GEngine->GetLargeFont(), Scale, Red, 0.5f);
	}
	// the black fade
	const double F = M ? M->fadeState.value() : 0;
	if (F > 0.001) DrawRect(FLinearColor(0, 0, 0, (float)F), 0, 0, W, H);
}
