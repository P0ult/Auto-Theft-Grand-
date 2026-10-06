#include "Game/ATGHUD.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGWorld.h"
#include "Sim/Game.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "RenderUtils.h"

namespace {
const FLinearColor Gold(1.f, 0.78f, 0.25f), Paper(0.95f, 0.93f, 0.88f), Ink(0.02f, 0.02f, 0.03f);
}

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
	DrawRadar(W, Px, Pz, Heading, Dt, Car ? Car->speedAbs() : 0);

	// zone name (shown for a while when it changes) and the clock
	const FString Zone = W->ZoneName(Px, Pz);
	if (Zone != LastZone) { LastZone = Zone; ZoneTime = 5.f; }
	ZoneTime = FMath::Max(0.f, ZoneTime - Dt);
	const float Right = Canvas->ClipX - 40 * Ui, Bottom = Canvas->ClipY - 40 * Ui;
	if (ZoneTime > 0) Text(Zone, Right, Bottom - 120 * Ui, Big, 1.6f * Ui, FLinearColor(Paper.R, Paper.G, Paper.B, FMath::Min(1.f, ZoneTime)), 1.f);
	Text(W->TimeString(), Right, 36 * Ui, Big, 1.4f * Ui, Paper, 1.f);

	// speedometer (mph, as in the browser game)
	if (Car) {
		const int32 Mph = FMath::RoundToInt(FMath::Abs(Car->speed()) * 2.23694);
		Text(FString::Printf(TEXT("%d"), Mph), Right, Bottom - 70 * Ui, Big, 2.4f * Ui, Paper, 1.f);
		Text(TEXT("MPH"), Right, Bottom - 12 * Ui, Small, 1.2f * Ui, Gold, 1.f);
		Text(FString(UTF8_TO_TCHAR(Car->def.name.c_str())), Right - 120 * Ui, Bottom - 12 * Ui, Small, 1.2f * Ui, Paper, 1.f);
		if (Car->health <= 0) Text(TEXT("ENGINE DEAD"), Canvas->ClipX / 2, 120 * Ui, Big, 1.2f * Ui, FLinearColor(1, 0.3f, 0.2f), 0.5f);
	} else {
		// stamina bar while sprinting
		if (Pl.stamina < 0.99) {
			const float Bw = 220 * Ui, Bh = 8 * Ui, Bx = Right - Bw, By = Bottom - 8 * Ui;
			DrawRect(FLinearColor(0, 0, 0, 0.5f), Bx, By, Bw, Bh);
			DrawRect(Gold, Bx, By, Bw * (float)Pl.stamina, Bh);
		}
		if (atg::Vehicle* Near = G->vehicles.nearestEnterable(Pl.pos, 5)) if (!G->vehicles.isBusy(&Pl))
			Text(FString::Printf(TEXT("Press F to drive the %s"), UTF8_TO_TCHAR(Near->def.name.c_str())), Canvas->ClipX / 2, Canvas->ClipY * 0.72f, Small, 1.4f * Ui, Paper, 0.5f);
	}
	if (PC->MessageTime > 0) Text(PC->Message, Canvas->ClipX / 2, 70 * Ui, Small, 1.5f * Ui, FLinearColor(Paper.R, Paper.G, Paper.B, FMath::Min(1.f, PC->MessageTime)), 0.5f);
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

void AATGHUD::DrawRadar(AATGWorld* W, double Px, double Pz, double Heading, float Dt, double CarSpeed) {
	const double Target = CarSpeed > 0 ? 150 + FMath::Clamp(CarSpeed * 4, 0.0, 130.0) : 110;
	RadarRange += (Target - RadarRange) * FMath::Min(1.0, Dt * 2.0);
	const float S = 260 * Ui, X = 40 * Ui, Y = Canvas->ClipY - S - 40 * Ui;
	DrawRect(FLinearColor(0, 0, 0, 0.6f), X - 4 * Ui, Y - 4 * Ui, S + 8 * Ui, S + 8 * Ui);
	DrawMapLayers(W, Px - RadarRange, Pz - RadarRange, Px + RadarRange, Pz + RadarRange, X, Y, S, S);
	// parked cars
	const float K = S / (float)(RadarRange * 2);
	atg::Game* G = W->Game();
	if (G) for (const auto& C : G->vehicles.list) {
		if (C->driver()) continue;
		const float Dx = (float)(C->pos.x - Px) * K, Dz = (float)(C->pos.z - Pz) * K;
		if (FMath::Abs(Dx) < S / 2 - 3 && FMath::Abs(Dz) < S / 2 - 3) DrawRect(FLinearColor(0.35f, 0.75f, 1.f), X + S / 2 + Dx - 2.5f * Ui, Y + S / 2 + Dz - 2.5f * Ui, 5 * Ui, 5 * Ui);
	}
	// map blips (game.blips: stations, the trains, ...); the full GTA V radar comes with the HUD port
	if (G) for (const auto& B : G->blips) {
		const float Dx = (float)(B->x - Px) * K, Dz = (float)(B->z - Pz) * K;
		const float R = (B->small ? 3.5f : 5.f) * Ui;
		if (FMath::Abs(Dx) < S / 2 - R && FMath::Abs(Dz) < S / 2 - R) DrawRect(BlipColor(B->color), X + S / 2 + Dx - R, Y + S / 2 + Dz - R, R * 2, R * 2);
	}
	Arrow(X + S / 2, Y + S / 2, Heading, 9 * Ui, FLinearColor::White);
	Text(TEXT("N"), X + S / 2, Y - 2 * Ui, GEngine->GetSmallFont(), 1.1f * Ui, Gold, 0.5f);
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
