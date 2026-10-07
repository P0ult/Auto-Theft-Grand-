// A small 2D canvas in the style of the browser's CanvasRenderingContext2D, on top of Unreal's UCanvas, so the
// HUD's drawing code (hud.js: the radar, blips, icons, the speedometer, the weapon wheel) ports line for line:
// save / restore, translate / rotate / scale, paths of lines and arcs, fill (any simple polygon, ear-clipped),
// stroke, rectangles, rounded rectangles, text and textured polygons.
#pragma once

#include "CoreMinimal.h"

class UCanvas;
class UFont;
class UTexture;

class FATGPainter {
public:
	explicit FATGPainter(UCanvas* InCanvas) : Canvas(InCanvas) {}

	// the canvas transform: x' = a x + c y + e, y' = b x + d y + f
	struct FMat { float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; };
	FMat M;
	FLinearColor Fill = FLinearColor::White, Stroke = FLinearColor::Black;
	float LineWidth = 1.f, Alpha = 1.f;
	bool Invert = false; // (CSS filter: invert(1), on the sRGB values)

	void Save() { Stack.Add({ M, Fill, Stroke, LineWidth, Alpha }); }
	void Restore() { if (Stack.Num()) { const FState S = Stack.Pop(); M = S.M; Fill = S.Fill; Stroke = S.Stroke; LineWidth = S.LineWidth; Alpha = S.Alpha; } }
	void Translate(float X, float Y) { M.e += M.a * X + M.c * Y; M.f += M.b * X + M.d * Y; }
	void Rotate(float A);
	void Scale(float X, float Y) { M.a *= X; M.b *= X; M.c *= Y; M.d *= Y; }

	// paths
	void BeginPath() { Paths.Reset(); }
	void MoveTo(float X, float Y);
	void LineTo(float X, float Y);
	void ClosePath() { if (Paths.Num()) Paths.Last().bClosed = true; }
	void Arc(float X, float Y, float R, float A0, float A1, bool bCcw = false);
	void Ellipse(float X, float Y, float Rx, float Ry, float Rot, float A0, float A1);
	void Rect(float X, float Y, float W, float H);
	void RoundRect(float X, float Y, float W, float H, float R);
	void FillPath();
	void StrokePath();
	void FillRect(float X, float Y, float W, float H) { BeginPath(); Rect(X, Y, W, H); FillPath(); }
	// text at (X, Y) in this painter's space; AlignX 0 left, 0.5 centre, 1 right; AlignY 0 top, 0.5 middle
	void Text(const FString& S, float X, float Y, UFont* Font, float Size, float AlignX = 0.5f, float AlignY = 0.5f, bool bShadow = false);
	// canvas fillText: text with its baseline at (X, Y), Px pixels to the em, AlignX 0 left, 0.5 centre, 1 right.
	// Rendered crisply at that size from the engine's Roboto (Face "Regular", "Bold", "Italic" or "Bold Italic");
	// Condense < 1 narrows it (for the browser's Impact)
	void FontText(const FString& S, float X, float Y, float Px, const TCHAR* Face, float AlignX = 0.f, float Condense = 1.f);
	// a polygon textured from Tex (a fan): points in this painter's space, uvs per point
	void TexturedPoly(UTexture* Tex, const TArray<FVector2f>& Pts, const TArray<FVector2f>& Uvs, const FLinearColor& Tint);
	FVector2f ToScreen(float X, float Y) const { return FVector2f(M.a * X + M.c * Y + M.e, M.b * X + M.d * Y + M.f); }
	float ScaleOf() const { return FMath::Sqrt(FMath::Abs(M.a * M.d - M.b * M.c)); }

private:
	struct FPath { TArray<FVector2f> P; bool bClosed = false; };
	struct FState { FMat M; FLinearColor Fill, Stroke; float LineWidth, Alpha; };
	UCanvas* Canvas;
	TArray<FState> Stack;
	TArray<FPath> Paths;
	FLinearColor WithAlpha(const FLinearColor& C) const {
		if (Invert) { const FColor S = C.ToFColor(true); const FLinearColor I(FColor(255 - S.R, 255 - S.G, 255 - S.B)); return FLinearColor(I.R, I.G, I.B, C.A * Alpha); }
		return FLinearColor(C.R, C.G, C.B, C.A * Alpha);
	}
	void Point(float X, float Y);
};

// CSS colour "#rrggbb" or 0xRRGGBB as an Unreal colour (sRGB to linear)
FLinearColor CssColor(uint32 Hex, float A = 1.f);
