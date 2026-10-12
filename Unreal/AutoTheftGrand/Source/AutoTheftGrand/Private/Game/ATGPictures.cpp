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

namespace {
// canvas fillText with textBaseline "middle" and a maxWidth (the text squeezed to fit)
void PicMiddle(FATGPainter& g, const FString& S, float X, float Y, float Px, const TCHAR* Face, float Condense, float MaxW) {
	const float Wd = g.FontWidth(S, Px, Face, Condense);
	const float K = Wd > MaxW && Wd > 0 ? MaxW / Wd : 1.f;
	g.FontText(S, X, Y + Px * 0.35f, Px, Face, 0.5f, Condense * K);
}
// textures.js ADS
struct PicAd { uint32 Bg0, Bg1, Fg; const TCHAR* Title; const TCHAR* Sub; };
const PicAd PicAds[] = {
	{ 0xd62828, 0x8b0000, 0xffffff, TEXT("SOLAR COLA"), TEXT("Taste the heat") },
	{ 0xffb703, 0xfb8500, 0x3a0ca3, TEXT("MAXX BURGER"), TEXT("Big. Bigger. MAXX.") },
	{ 0x023047, 0x219ebc, 0xffb703, TEXT("VISTA FM 98.1"), TEXT("West Coast Classics") },
	{ 0x2b9348, 0x007f5f, 0xffffff, TEXT("SPEEDY LOANS"), TEXT("Cash in 5 minutes*") },
	{ 0x111111, 0x444444, 0xe5e5e5, TEXT("ZENITH MOTORS"), TEXT("Drive the future") },
	{ 0xf1faee, 0xa8dadc, 0x1d3557, TEXT("DR. SMILE"), TEXT("Dental that shines") },
	{ 0x7209b7, 0x3a0ca3, 0xfee440, TEXT("LOS SOLES LOTTO"), TEXT("You could be next!") },
	{ 0xe76f51, 0xf4a261, 0xffffff, TEXT("SANTA LUZ PIER"), TEXT("Ride the big wheel") },
};
// city.js _signs styles: background, lettering, whether it glows
struct PicSign { const TCHAR* Text; uint32 Bg, Fg; bool Glow; };
const PicSign PicSigns[] = {
	{ TEXT("GUN BARN"), 0x1b1b1b, 0xff5a36, true }, { TEXT("HOSPITAL"), 0xf4f4f4, 0xd62828, false }, { TEXT("POLICE"), 0x0b1f4d, 0xe8eefc, true },
	{ TEXT("SPRAY SHACK"), 0x222222, 0x6df0ff, true }, { TEXT("BIG BUN"), 0x6b1a00, 0xffd166, true }, { TEXT("GARAGE"), 0x222222, 0xffffff, true },
	{ TEXT("LIQUOR"), 0x1d0826, 0xff5ec4, true }, { TEXT("GAS"), 0xb3121b, 0xffffff, true }, { TEXT("MOTEL"), 0x123d4a, 0xff6fb5, true },
	{ TEXT("SALOON"), 0x3b2412, 0xffcc66, true }, { TEXT("CANTINA"), 0x5a1a4a, 0xffd166, true }, { TEXT("FORT CARVER"), 0x2f3a26, 0xe9e4c8, true },
	{ TEXT("AIRFIELD"), 0x1c2f4a, 0xffffff, true }, { TEXT("MALL"), 0x20252b, 0x7ee0ff, true }, { TEXT("LODGE"), 0x2d3d22, 0xf4e3b2, true },
	{ TEXT("PET PALACE"), 0x1f6f4a, 0xfff3a8, true }, { TEXT("24/7"), 0x0f7a3a, 0xffffff, true }, { TEXT("RUSTY ANCHOR"), 0x2a1a10, 0xff9a3c, true },
	{ TEXT("BEAN SCENE"), 0x3b2418, 0xf2d7b0, true },
};
}

UTextureRenderTarget2D* ATGPictures::Paint(UObject* Outer, const FString& Name) {
	int32 W = 0, H = 0;
	if (Name == TEXT("target")) { W = 256; H = 320; }
	else if (Name == TEXT("rules") || Name == TEXT("adopt")) { W = 320; H = 200; }
	else if (Name == TEXT("burgermenu")) { W = 1024; H = 256; }
	else if (Name == TEXT("neon")) { W = 512; H = 160; }
	else if (Name == TEXT("cafemenu")) { W = 768; H = 256; }
	else if (Name == TEXT("lotto")) { W = 256; H = 128; }
	else if (Name == TEXT("ads")) { W = 1024; H = 1024; }
	else if (Name.StartsWith(TEXT("sign:"))) { W = 1024; H = 192; }
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
	} else if (Name == TEXT("ads")) {
		// textures.js adAtlas: eight ads, 2 x 4
		const float Aw = 512, Ah = 256;
		for (int32 i = 0; i < 8; i++) {
			const PicAd& Ad = PicAds[i];
			const float x = (i % 2) * Aw, y = (i / 2) * Ah;
			g.GradientRect(x, y, Aw, Ah, CssColor(Ad.Bg0), CssColor(Ad.Bg1));
			g.Fill = FLinearColor(1, 1, 1, 0.12f);
			for (int32 k = 0; k < 6; k++) PicDisc(g, x + 60 + k * 90, y + Ah - 20, 40.f + k * 6);
			Fill(Ad.Fg);
			PicMiddle(g, Ad.Title, x + Aw / 2, y + Ah * 0.42f, 74, TEXT("Bold"), PicImpact, Aw - 30);
			PicMiddle(g, Ad.Sub, x + Aw / 2, y + Ah * 0.75f, 34, TEXT("Italic"), 1.f, Aw - 40);
			g.Stroke = FLinearColor(0, 0, 0, 0.5f); g.LineWidth = 8;
			g.BeginPath(); g.Rect(x + 4, y + 4, Aw - 8, Ah - 8); g.StrokePath();
		}
	} else if (Name.StartsWith(TEXT("sign:"))) {
		// textures.js signTexture with city.js's style for the name
		const FString Text = Name.Mid(5);
		PicSign St = { nullptr, 0x111111, 0xffffff, true };
		for (const PicSign& S : PicSigns) if (Text == S.Text) St = S;
		Fill(St.Bg); g.FillRect(0, 0, w, h);
		const float Wd = g.FontWidth(Text, 130, TEXT("Bold"), PicImpact);
		const float K = Wd > w - 40 && Wd > 0 ? (w - 40) / Wd : 1.f;
		if (St.Glow) PicGlow(g, Text, w / 2, h / 2 + 6 + 130 * 0.35f, 130, 0.5f, PicImpact * K, St.Fg, 24);
		Fill(St.Fg); PicMiddle(g, Text, w / 2, h / 2 + 6, 130, TEXT("Bold"), PicImpact, w - 40);
	} else if (Name == TEXT("lotto")) {
		Fill(0xffd60a); g.FillRect(0, 0, w, h);
		Fill(0xc1121f); Txt(TEXT("LOTTO"), w / 2, 58, 44, TEXT("Bold"), 0.5f, PicImpact);
		Fill(0x111111); Txt(TEXT("Win big in Los Soles!"), w / 2, 100, 22, TEXT("Bold"), 0.5f);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(Outer, Ctx);
	return RT;
}
