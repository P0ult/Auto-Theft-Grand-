#include "Game/ATGPainter.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture.h"
#include "Engine/Engine.h"
#include "EngineFontServices.h"
#include "Fonts/FontMeasure.h"
#include "RenderUtils.h"

FLinearColor CssColor(uint32 Hex, float A) {
	FLinearColor C(FColor((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255));
	C.A = A;
	return C;
}

void FATGPainter::Rotate(float A) {
	const float C = FMath::Cos(A), S = FMath::Sin(A);
	const FMat O = M;
	M.a = O.a * C + O.c * S; M.b = O.b * C + O.d * S;
	M.c = -O.a * S + O.c * C; M.d = -O.b * S + O.d * C;
}

void FATGPainter::Point(float X, float Y) {
	if (!Paths.Num()) Paths.AddDefaulted();
	Paths.Last().P.Add(ToScreen(X, Y));
}
void FATGPainter::MoveTo(float X, float Y) { Paths.AddDefaulted(); Point(X, Y); }
void FATGPainter::LineTo(float X, float Y) { Point(X, Y); }

void FATGPainter::Arc(float X, float Y, float R, float A0, float A1, bool bCcw) {
	float Span = A1 - A0;
	if (!bCcw) { while (Span < 0) Span += 2 * PI; if (A1 - A0 >= 2 * PI) Span = 2 * PI; }
	else { while (Span > 0) Span -= 2 * PI; if (A0 - A1 >= 2 * PI) Span = -2 * PI; }
	const float Px = R * ScaleOf();
	const int32 N = FMath::Clamp((int32)(FMath::Abs(Span) * FMath::Max(3.f, Px) / 3.f), 4, 64);
	for (int32 I = 0; I <= N; I++) { const float A = A0 + Span * I / N; Point(X + FMath::Cos(A) * R, Y + FMath::Sin(A) * R); }
}

void FATGPainter::Ellipse(float X, float Y, float Rx, float Ry, float Rot, float A0, float A1) {
	const int32 N = 32;
	const float C = FMath::Cos(Rot), S = FMath::Sin(Rot);
	for (int32 I = 0; I <= N; I++) { const float A = A0 + (A1 - A0) * I / N; const float Ex = FMath::Cos(A) * Rx, Ey = FMath::Sin(A) * Ry; Point(X + Ex * C - Ey * S, Y + Ex * S + Ey * C); }
}

void FATGPainter::Rect(float X, float Y, float W, float H) { MoveTo(X, Y); LineTo(X + W, Y); LineTo(X + W, Y + H); LineTo(X, Y + H); ClosePath(); }

void FATGPainter::RoundRect(float X, float Y, float W, float H, float R) {
	R = FMath::Min(R, FMath::Min(W, H) / 2);
	MoveTo(X + R, Y);
	Arc(X + W - R, Y + R, R, -PI / 2, 0);
	Arc(X + W - R, Y + H - R, R, 0, PI / 2);
	Arc(X + R, Y + H - R, R, PI / 2, PI);
	Arc(X + R, Y + R, R, PI, PI * 1.5f);
	ClosePath();
}

namespace {
float Cross(const FVector2f& A, const FVector2f& B, const FVector2f& C) { return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X); }
bool InTri(const FVector2f& P, const FVector2f& A, const FVector2f& B, const FVector2f& C) {
	const float D1 = Cross(A, B, P), D2 = Cross(B, C, P), D3 = Cross(C, A, P);
	return !(((D1 < 0) || (D2 < 0) || (D3 < 0)) && ((D1 > 0) || (D2 > 0) || (D3 > 0)));
}
// ear clipping for a simple polygon (either winding)
void Triangulate(const TArray<FVector2f>& In, TArray<int32>& Out) {
	TArray<int32> V;
	for (int32 I = 0; I < In.Num(); I++) if (I == 0 || !In[I].Equals(In[I - 1], 1e-3f)) V.Add(I);
	if (V.Num() > 2 && In[V[0]].Equals(In[V.Last()], 1e-3f)) V.Pop();
	if (V.Num() < 3) return;
	float Area = 0;
	for (int32 I = 0; I < V.Num(); I++) { const FVector2f& A = In[V[I]]; const FVector2f& B = In[V[(I + 1) % V.Num()]]; Area += A.X * B.Y - B.X * A.Y; }
	const float Sign = Area > 0 ? 1.f : -1.f;
	int32 Guard = 0;
	while (V.Num() > 3 && Guard++ < 4000) {
		bool bCut = false;
		for (int32 I = 0; I < V.Num(); I++) {
			const int32 Ia = V[(I + V.Num() - 1) % V.Num()], Ib = V[I], Ic = V[(I + 1) % V.Num()];
			const FVector2f& A = In[Ia]; const FVector2f& B = In[Ib]; const FVector2f& C = In[Ic];
			if (Cross(A, B, C) * Sign <= 0) continue;
			bool bInside = false;
			for (int32 K : V) if (K != Ia && K != Ib && K != Ic && InTri(In[K], A, B, C)) { bInside = true; break; }
			if (bInside) continue;
			Out.Append({ Ia, Ib, Ic });
			V.RemoveAt(I);
			bCut = true;
			break;
		}
		if (!bCut) break; // (not simple: give up on the rest)
	}
	if (V.Num() == 3) Out.Append({ V[0], V[1], V[2] });
}
}

void FATGPainter::FillPath() {
	const FLinearColor C = WithAlpha(Fill);
	if (C.A <= 0) return;
	for (const FPath& P : Paths) {
		TArray<int32> Tris;
		Triangulate(P.P, Tris);
		TArray<FCanvasUVTri> Batch;
		for (int32 I = 0; I + 2 < Tris.Num(); I += 3) {
			FCanvasUVTri T;
			T.V0_Pos = FVector2D(P.P[Tris[I]]); T.V1_Pos = FVector2D(P.P[Tris[I + 1]]); T.V2_Pos = FVector2D(P.P[Tris[I + 2]]);
			T.V0_Color = T.V1_Color = T.V2_Color = C;
			Batch.Add(T);
		}
		if (Batch.Num()) { FCanvasTriangleItem Item(Batch, GWhiteTexture); Item.BlendMode = SE_BLEND_Translucent; Canvas->DrawItem(Item); }
	}
}

void FATGPainter::StrokePath() {
	const FLinearColor C = WithAlpha(Stroke);
	if (C.A <= 0) return;
	const float W = FMath::Max(1.f, LineWidth * ScaleOf());
	for (const FPath& P : Paths) {
		const int32 N = P.P.Num();
		if (N < 2) continue;
		// each segment as a quad, so wide lines join without gaps at small angles
		TArray<FCanvasUVTri> Batch;
		const int32 Segs = P.bClosed ? N : N - 1;
		for (int32 I = 0; I < Segs; I++) {
			const FVector2f A = P.P[I], B = P.P[(I + 1) % N];
			FVector2f D = B - A;
			const float L = D.Size();
			if (L < 1e-3f) continue;
			D /= L;
			const FVector2f Nn(-D.Y * W / 2, D.X * W / 2), Ext = D * (W / 2);
			const FVector2f A0 = A - Ext + Nn, A1 = A - Ext - Nn, B0 = B + Ext + Nn, B1 = B + Ext - Nn;
			FCanvasUVTri T1, T2;
			T1.V0_Pos = FVector2D(A0); T1.V1_Pos = FVector2D(B0); T1.V2_Pos = FVector2D(B1);
			T2.V0_Pos = FVector2D(A0); T2.V1_Pos = FVector2D(B1); T2.V2_Pos = FVector2D(A1);
			T1.V0_Color = T1.V1_Color = T1.V2_Color = C; T2.V0_Color = T2.V1_Color = T2.V2_Color = C;
			Batch.Add(T1); Batch.Add(T2);
		}
		if (Batch.Num()) { FCanvasTriangleItem Item(Batch, GWhiteTexture); Item.BlendMode = SE_BLEND_Translucent; Canvas->DrawItem(Item); }
	}
}

void FATGPainter::Text(const FString& S, float X, float Y, UFont* Font, float Size, float AlignX, float AlignY, bool bShadow) {
	if (S.IsEmpty() || !Font) return;
	const FVector2f P = ToScreen(X, Y);
	float Bw = 0, Bh = 0;
	Canvas->TextSize(Font, S, Bw, Bh);
	const float K = Bh > 0 ? Size * ScaleOf() / Bh : 1.f;
	FCanvasTextItem Item(FVector2D(P.X - Bw * K * AlignX, P.Y - Bh * K * AlignY), FText::FromString(S), Font, WithAlpha(Fill));
	Item.Scale = FVector2D(K, K);
	if (bShadow) Item.EnableShadow(FLinearColor(0, 0, 0, 0.9f * Alpha), FVector2D(2, 2));
	Canvas->DrawItem(Item);
}

void FATGPainter::FontText(const FString& S, float X, float Y, float Px, const TCHAR* Face, float AlignX, float Condense) {
	if (S.IsEmpty() || !GEngine || !FEngineFontServices::IsInitialized()) return;
	const float K = ScaleOf();
	// (Slate sizes are points at 96 dpi)
	const FSlateFontInfo Info(GEngine->GetLargeFont(), FMath::Max(1.f, Px * K * 0.75f), FName(Face));
	const TSharedPtr<FSlateFontMeasure> Measure = FEngineFontServices::Get().GetFontMeasure();
	if (!Measure.IsValid()) return;
	const FVector2D Size = Measure->Measure(S, Info);
	const float Above = (float)Measure->GetMaxCharacterHeight(Info) - FMath::Abs((float)Measure->GetBaseline(Info));
	const FVector2f P = ToScreen(X, Y);
	FCanvasTextItem Item(FVector2D(P.X - Size.X * Condense * AlignX, P.Y - Above), FText::FromString(S), Info, WithAlpha(Fill));
	Item.Scale = FVector2D(Condense, 1.f);
	Canvas->DrawItem(Item);
}

void FATGPainter::TexturedPoly(UTexture* Tex, const TArray<FVector2f>& Pts, const TArray<FVector2f>& Uvs, const FLinearColor& Tint) {
	if (!Tex || Pts.Num() < 3) return;
	TArray<FCanvasUVTri> Batch;
	const FLinearColor C = WithAlpha(Tint);
	const FVector2f S0 = ToScreen(Pts[0].X, Pts[0].Y);
	for (int32 I = 1; I + 1 < Pts.Num(); I++) {
		FCanvasUVTri T;
		T.V0_Pos = FVector2D(S0); T.V1_Pos = FVector2D(ToScreen(Pts[I].X, Pts[I].Y)); T.V2_Pos = FVector2D(ToScreen(Pts[I + 1].X, Pts[I + 1].Y));
		T.V0_UV = FVector2D(Uvs[0]); T.V1_UV = FVector2D(Uvs[I]); T.V2_UV = FVector2D(Uvs[I + 1]);
		T.V0_Color = T.V1_Color = T.V2_Color = C;
		Batch.Add(T);
	}
	FCanvasTriangleItem Item(Batch, Tex->GetResource());
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}
