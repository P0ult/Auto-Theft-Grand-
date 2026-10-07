#include "Game/ATGPictures.h"
#include "Game/ATGPainter.h"

#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"

namespace {
constexpr float PicImpact = 0.75f; // (Impact is about three quarters as wide as Roboto Bold)

void PicDisc(FATGPainter& g, float X, float Y, float R) { g.BeginPath(); g.Arc(X, Y, R, 0, 2 * PI); g.FillPath(); }

// canvas shadowBlur under a line of text: copies of it spread round a few rings, faint
void PicGlow(FATGPainter& g, const FString& S, float X, float Y, float Px, float AlignX, float Condense, uint32 Color, float Blur) {
	g.Save();
	g.Fill = CssColor(Color);
	g.Alpha = 0.09f;
	for (int32 Ring = 1; Ring <= 3; Ring++) {
		const float R = Blur * Ring / 6.f;
		for (int32 K = 0; K < 8; K++) {
			const float A = K * PI / 4 + Ring * 0.4f;
			g.FontText(S, X + FMath::Cos(A) * R, Y + FMath::Sin(A) * R, Px, TEXT("Bold"), AlignX, Condense);
		}
	}
	g.Restore();
}
}

UTextureRenderTarget2D* ATGPictures::Paint(UObject* Outer, const FString& Name) {
	int32 W = 0, H = 0;
	if (Name == TEXT("target")) { W = 256; H = 320; }
	else if (Name == TEXT("rules") || Name == TEXT("adopt")) { W = 320; H = 200; }
	else if (Name == TEXT("burgermenu")) { W = 1024; H = 256; }
	else if (Name == TEXT("neon")) { W = 512; H = 160; }
	else if (Name == TEXT("cafemenu")) { W = 768; H = 256; }
	else if (Name == TEXT("lotto")) { W = 256; H = 128; }
	else return nullptr;
	UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(Outer, W, H, RTF_RGBA8_SRGB, FLinearColor::Black, false);
	if (!RT) return nullptr;
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(Outer, RT, Canvas, Size, Ctx);
	if (!Canvas) return RT;
	FATGPainter g(Canvas);
	const float w = (float)W, h = (float)H;
	auto Fill = [&](uint32 C) { g.Fill = CssColor(C); };
	auto Txt = [&](const FString& S, float X, float Y, float Px, const TCHAR* Face, float AlignX, float Condense = 1.f) { g.FontText(S, X, Y, Px, Face, AlignX, Condense); };
	if (Name == TEXT("target")) {
		Fill(0xf4efe4); g.FillRect(0, 0, w, h);
		for (int32 k = 6; k >= 1; k--) { Fill(k % 2 ? 0x111111 : 0xf4efe4); PicDisc(g, w / 2, h * 0.45f, k * 18.f); }
		Fill(0xc1121f); PicDisc(g, w / 2, h * 0.45f, 12);
		Fill(0x111111); Txt(TEXT("GUN BARN RANGE"), w / 2, h - 22, 26, TEXT("Bold"), 0.5f, PicImpact);
	} else if (Name == TEXT("rules")) {
		Fill(0x1b1b1b); g.FillRect(0, 0, w, h);
		Fill(0xff5a36); Txt(TEXT("NO REFUNDS"), w / 2, 70, 40, TEXT("Bold"), 0.5f, PicImpact);
		Fill(0xeeeeee); Txt(TEXT("No questions asked."), w / 2, 118, 22, TEXT("Bold"), 0.5f); Txt(TEXT("Shoplifters will be shot."), w / 2, 152, 22, TEXT("Bold"), 0.5f);
	} else if (Name == TEXT("burgermenu")) {
		Fill(0x2a0d05); g.FillRect(0, 0, w, h);
		Fill(0xffd166); Txt(TEXT("BIG BUN"), 30, 96, 76, TEXT("Bold"), 0, PicImpact);
		Fill(0xff6b3d); Txt(TEXT("Order at the counter"), 34, 140, 24, TEXT("Bold Italic"), 0);
		const TCHAR* Items[4][2] = { { TEXT("Double Stack Combo"), TEXT("$10") }, { TEXT("Big Bun Classic"), TEXT("$6") }, { TEXT("Cluckin' Wings"), TEXT("$7") }, { TEXT("Fries & Shake"), TEXT("$4") } };
		for (int32 k = 0; k < 4; k++) {
			const float x = 340.f + (k % 2) * 340, y = 70.f + (k / 2) * 70;
			Fill(0xffffff); Txt(Items[k][0], x, y, 23, TEXT("Bold"), 0);
			Fill(0xffd166); Txt(Items[k][1], x + 318, y, 23, TEXT("Bold"), 1);
		}
		Fill(0xffffff); Txt(TEXT("Flame grilled since 1983 · Open 24 hours"), 340, 225, 22, TEXT("Bold"), 0);
	} else if (Name == TEXT("adopt")) {
		Fill(0xfff6d8); g.FillRect(0, 0, w, h);
		Fill(0x1f6f4a); Txt(TEXT("ADOPT A FRIEND"), w / 2, 70, 44, TEXT("Bold"), 0.5f, PicImpact);
		Fill(0x333333); Txt(TEXT("Dogs · Cats · Fish"), w / 2, 112, 22, TEXT("Bold"), 0.5f); Txt(TEXT("Ask at the counter"), w / 2, 150, 22, TEXT("Bold"), 0.5f);
	} else if (Name == TEXT("neon")) {
		Fill(0x0c0705); g.FillRect(0, 0, w, h);
		PicGlow(g, TEXT("RUSTY ANCHOR"), w / 2, 92, 64, 0.5f, PicImpact, 0xff9a3c, 18);
		Fill(0xffb35c); Txt(TEXT("RUSTY ANCHOR"), w / 2, 92, 64, TEXT("Bold"), 0.5f, PicImpact);
		Fill(0x7fd4ff); Txt(TEXT("COLD BEER · WHISKEY · POOL"), w / 2, 136, 22, TEXT("Bold"), 0.5f);
	} else if (Name == TEXT("cafemenu")) {
		Fill(0x2b1b12); g.FillRect(0, 0, w, h);
		Fill(0xf2d7b0); Txt(TEXT("Bean Scene"), 28, 80, 60, TEXT("Bold"), 0);
		const TCHAR* Items[5][2] = { { TEXT("Espresso"), TEXT("$3") }, { TEXT("Flat White"), TEXT("$4") }, { TEXT("Iced Latte"), TEXT("$5") }, { TEXT("Glazed Donut"), TEXT("$2") }, { TEXT("Club Sandwich"), TEXT("$6") } };
		for (int32 k = 0; k < 5; k++) {
			const float x = 360.f + (k % 2) * 200, y = 60.f + (k / 2) * 55;
			Fill(0xffffff); Txt(Items[k][0], x, y, 24, TEXT("Bold"), 0);
			Fill(0xf2b36b); Txt(Items[k][1], x + 150, y, 24, TEXT("Bold"), 0);
		}
		Fill(0xc9a27a); Txt(TEXT("Roasted in Los Soles"), 30, 130, 22, TEXT("Italic"), 0);
	} else if (Name == TEXT("lotto")) {
		Fill(0xffd60a); g.FillRect(0, 0, w, h);
		Fill(0xc1121f); Txt(TEXT("LOTTO"), w / 2, 58, 44, TEXT("Bold"), 0.5f, PicImpact);
		Fill(0x111111); Txt(TEXT("Win big in Los Soles!"), w / 2, 100, 22, TEXT("Bold"), 0.5f);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(Outer, Ctx);
	return RT;
}
