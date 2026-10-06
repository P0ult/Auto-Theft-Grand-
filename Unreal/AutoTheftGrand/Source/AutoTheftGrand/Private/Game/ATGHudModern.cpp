// The GTA V style HUD of hud.js ("modern"): the rotating minimap with health, armour and special bars under it,
// the wanted stars, cash and weapon top right, the zone and vehicle names bottom right. Drawn with
// FATGPainter so the canvas code ports line for line.
#include "Game/ATGHUD.h"
#include "Game/ATGPainter.h"
#include "Game/ATGWorld.h"
#include "Sim/Game.h"
#include "Sim/Hud.h"
#include "Sim/Weapons.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"

namespace {
using P2 = FVector2f;

// ---- hud.js drawPlayerArrow (radar: relYaw 0 points up)
void PlayerArrow(FATGPainter& C, float X, float Y, float RelYaw, float Scale) {
	C.Save();
	C.Translate(X, Y);
	C.Rotate(-RelYaw);
	C.Scale(Scale, Scale);
	C.BeginPath(); C.MoveTo(0, -10); C.LineTo(7, 8); C.LineTo(0, 4); C.LineTo(-7, 8); C.ClosePath();
	C.Fill = FLinearColor::White; C.Stroke = FLinearColor::Black; C.LineWidth = 2;
	C.FillPath(); C.StrokePath();
	C.Restore();
}

// ---- hud.js ICONS
bool Icon(FATGPainter& c, const std::string& Id) {
	auto Fr = [&](float x, float y, float w, float h) { c.FillRect(x, y, w, h); };
	auto Circ = [&](float x, float y, float r) { c.BeginPath(); c.Arc(x, y, r, 0, 2 * PI); c.FillPath(); };
	auto Poly = [&](std::initializer_list<P2> Pts) { c.BeginPath(); bool f = true; for (const P2& p : Pts) { if (f) c.MoveTo(p.X, p.Y); else c.LineTo(p.X, p.Y); f = false; } c.ClosePath(); c.FillPath(); };
	const FLinearColor Bg = CssColor(0x111111);
	if (Id == "wrench") { c.Save(); c.Rotate(-0.75f); Fr(-1.3f, -2, 2.6f, 9); Circ(0, -4, 3.6f); const FLinearColor F = c.Fill; c.Fill = Bg; Fr(-1.2f, -8, 2.4f, 4.2f); c.Fill = F; c.Restore(); }
	else if (Id == "star") { c.BeginPath(); for (int k = 0; k < 10; k++) { const float r = k % 2 ? 2.6f : 6.5f, a = k / 10.f * PI * 2 - PI / 2; if (k == 0) c.MoveTo(FMath::Cos(a) * r, FMath::Sin(a) * r); else c.LineTo(FMath::Cos(a) * r, FMath::Sin(a) * r); } c.ClosePath(); c.FillPath(); }
	else if (Id == "house2") { Poly({ { -6, 0 }, { 0, -6 }, { 6, 0 } }); Fr(-4.5f, 0, 9, 6); }
	else if (Id == "ship") { Poly({ { -7, 1 }, { 7, 1 }, { 5, 5 }, { -5, 5 } }); Fr(-5, -3, 4, 4); Fr(0, -2, 4, 3); Fr(3, -6, 2, 4); }
	else if (Id == "boat") { Poly({ { -6, 2 }, { 6, 2 }, { 4, 5 }, { -4, 5 } }); Poly({ { 0, -6 }, { 0, 1 }, { 5, 1 } }); }
	else if (Id == "skate") { Fr(-6, -2, 12, 3); Circ(-4, 3, 1.7f); Circ(4, 3, 1.7f); }
	else if (Id == "gun" || Id == "weapon") { Fr(-6, Id == "gun" ? -3.f : -2.f, 11, 3); Fr(-5, Id == "gun" ? 0.f : 1.f, 3, Id == "gun" ? 5.f : 4.f); }
	else if (Id == "spray") { Fr(-3, -4, 6, 9); Fr(-1, -7, 2, 3); }
	else if (Id == "burger") { c.BeginPath(); c.Arc(0, 0, 5, PI, 2 * PI); c.ClosePath(); c.FillPath(); Fr(-5, 1, 10, 3); }
	else if (Id == "house") { Poly({ { -6, 0 }, { 0, -6 }, { 6, 0 } }); Fr(-4, 0, 8, 5); }
	else if (Id == "waypoint") { c.BeginPath(); c.MoveTo(0, 6); c.LineTo(-5, -2); c.Arc(0, -2, 5, PI, 2 * PI); c.ClosePath(); c.FillPath(); }
	else if (Id == "money") c.Text(TEXT("$"), 0, 1, GEngine->GetMediumFont(), 12, 0.5f, 0.5f);
	else if (Id == "health") { Fr(-5, -1.5f, 10, 3); Fr(-1.5f, -5, 3, 10); }
	else if (Id == "armor") Fr(-4, -5, 8, 10);
	else if (Id == "car") { Fr(-6, -2, 12, 5); Fr(-3, -5, 6, 3); }
	else if (Id == "target") { c.BeginPath(); c.Arc(0, 0, 5, 0, 2 * PI); c.Stroke = c.Fill; c.StrokePath(); Fr(-1, -1, 2, 2); }
	else if (Id == "skull") { Circ(0, -1, 5); Fr(-3, 3, 6, 3); }
	else if (Id == "flag") { Fr(-4, -6, 2, 12); Fr(-2, -6, 7, 5); }
	else if (Id == "plane") { Fr(-1, -7, 2, 13); Fr(-7, -2, 14, 2.5f); Fr(-3, 4, 6, 2); }
	else if (Id == "jet") Poly({ { 0, -7 }, { 6, 4 }, { 2, 3 }, { 0, 6 }, { -2, 3 }, { -6, 4 } });
	else if (Id == "heli") { Circ(-1, 1, 3.5f); Fr(1, 0, 6, 1.6f); Fr(-7, -4.5f, 12, 1.4f); Fr(-1.5f, -4, 1.4f, 3); }
	else if (Id == "tank") { Fr(-6, 0, 12, 4); Fr(-3, -3, 6, 3); Fr(2, -2.2f, 6, 1.4f); }
	else if (Id == "taxi") { Fr(-6, -1, 12, 5); Fr(-3, -4, 6, 3); Fr(-1.5f, -7, 3, 2); }
	else if (Id == "person") { Circ(0, -4, 2.6f); Fr(-3, -1, 6, 5); Fr(-3, 4, 2, 3); Fr(1, 4, 2, 3); }
	else if (Id == "train") { Fr(-4, -6, 8, 9); const FLinearColor F = c.Fill; c.Fill = Bg; Fr(-3, -5, 6, 3); c.Fill = F; Fr(-5, 4, 2, 2); Fr(3, 4, 2, 2); }
	else if (Id == "paw") { c.BeginPath(); c.Ellipse(0, 2.5f, 3.6f, 3, 0, 0, 2 * PI); c.FillPath(); for (const P2& p : { P2(-4.2f, -1.6f), P2(-1.5f, -4.4f), P2(1.5f, -4.4f), P2(4.2f, -1.6f) }) Circ(p.X, p.Y, 1.5f); }
	else if (Id == "cart") { Fr(-5, -3, 9, 5); Fr(-7, -5, 3, 1.5f); Circ(-3, 4.5f, 1.4f); Circ(3, 4.5f, 1.4f); }
	else if (Id == "beer") { Fr(-4, -4, 7, 10); Fr(3, -2, 2.5f, 1.4f); Fr(4.5f, -2, 1.4f, 5); Fr(3, 2, 2.5f, 1.4f); }
	else if (Id == "cup") { Poly({ { -5, -3 }, { 4, -3 }, { 3, 5 }, { -4, 5 } }); Fr(4, -1, 2.5f, 1.3f); Fr(5.2f, -1, 1.3f, 3.5f); }
	else return false;
	return true;
}

// ---- hud.js drawBlip
void Blip(FATGPainter& c, float X, float Y, const atg::Blip& b, float Scale) {
	const FLinearColor Col = CssColor(b.color);
	c.Save();
	c.Translate(X, Y);
	c.Scale(Scale, Scale);
	c.LineWidth = 2; c.Stroke = FLinearColor::Black;
	if (!b.letter.empty()) {
		c.Fill = Col; c.BeginPath(); c.Arc(0, 0, 9, 0, 2 * PI); c.FillPath(); c.StrokePath();
		c.Fill = CssColor(0x111111); c.Text(UTF8_TO_TCHAR(b.letter.c_str()), 0, 1, GEngine->GetMediumFont(), 13, 0.5f, 0.5f);
	} else if (!b.icon.empty() && b.icon != "dot") {
		// (the icon's own drawing first, to check it exists)
		FATGPainter::FMat Saved = c.M;
		c.Fill = CssColor(0x111111); c.BeginPath(); c.RoundRect(-9, -9, 18, 18, 4); c.FillPath();
		c.Stroke = Col; c.LineWidth = 1.5f; c.StrokePath();
		c.Fill = Col; c.Stroke = Col;
		if (!Icon(c, b.icon)) { c.M = Saved; c.Fill = Col; c.BeginPath(); c.Arc(0, 0, 4, 0, 2 * PI); c.FillPath(); }
	} else {
		c.Fill = Col;
		c.BeginPath();
		const float r = b.small ? 4.f : 6.f;
		if (b.square) c.Rect(-r, -r, r * 2, r * 2); else c.Arc(0, 0, r, 0, 2 * PI);
		c.FillPath(); c.StrokePath();
	}
	c.Restore();
}

// ---- hud.js drawWeaponIcon (a 96 x 96 drawing)
void WeaponIcon(FATGPainter& c, const std::string& id, float Size) {
	c.Save();
	c.Scale(Size / 96, Size / 96);
	c.Fill = CssColor(0xf5f5f5); c.Stroke = FLinearColor::Black; c.LineWidth = 3;
	auto P = [&](std::initializer_list<P2> Pts) { c.BeginPath(); bool f = true; for (const P2& p : Pts) { if (f) c.MoveTo(p.X, p.Y); else c.LineTo(p.X, p.Y); f = false; } c.ClosePath(); c.FillPath(); c.StrokePath(); };
	if (id == "fist") {
		c.BeginPath(); c.RoundRect(26, 30, 44, 34, 10); c.FillPath(); c.StrokePath();
		for (int i = 0; i < 4; i++) { c.BeginPath(); c.MoveTo(30 + i * 10, 30); c.LineTo(30 + i * 10, 44); c.StrokePath(); }
		c.BeginPath(); c.RoundRect(20, 44, 16, 22, 6); c.FillPath(); c.StrokePath();
	} else if (id == "knife") { P({ { 14, 54 }, { 44, 50 }, { 82, 40 }, { 44, 58 }, { 14, 60 } }); c.Fill = CssColor(0x6b3e1f); P({ { 10, 52 }, { 30, 52 }, { 30, 62 }, { 10, 62 } }); }
	else if (id == "bat") { c.Fill = CssColor(0xc69c6d); P({ { 10, 60 }, { 78, 30 }, { 86, 36 }, { 18, 66 } }); }
	else if (id == "pistol") P({ { 18, 36 }, { 76, 36 }, { 76, 48 }, { 40, 48 }, { 36, 70 }, { 22, 70 }, { 24, 48 }, { 18, 48 } });
	else if (id == "smg") P({ { 12, 36 }, { 80, 36 }, { 80, 46 }, { 52, 46 }, { 52, 72 }, { 44, 72 }, { 44, 46 }, { 36, 46 }, { 32, 62 }, { 22, 62 }, { 24, 46 }, { 12, 46 } });
	else if (id == "shotgun") P({ { 4, 44 }, { 90, 38 }, { 90, 46 }, { 40, 50 }, { 34, 62 }, { 20, 64 }, { 22, 52 }, { 4, 54 } });
	else if (id == "rifle") P({ { 4, 46 }, { 30, 40 }, { 88, 38 }, { 88, 44 }, { 62, 46 }, { 58, 66 }, { 50, 66 }, { 50, 48 }, { 36, 50 }, { 30, 60 }, { 20, 60 }, { 22, 52 }, { 4, 56 } });
	else if (id == "rpg") { c.Fill = CssColor(0x9aa77e); P({ { 6, 42 }, { 78, 38 }, { 90, 42 }, { 78, 46 }, { 6, 50 } }); c.Fill = CssColor(0xf5f5f5); P({ { 36, 50 }, { 44, 50 }, { 44, 64 }, { 36, 64 } }); }
	else if (id == "grenade") { c.BeginPath(); c.Arc(48, 54, 18, 0, 2 * PI); c.FillPath(); c.StrokePath(); P({ { 42, 30 }, { 54, 30 }, { 54, 38 }, { 42, 38 } }); }
	else if (id == "sniper") { P({ { 2, 48 }, { 26, 44 }, { 92, 42 }, { 92, 46 }, { 60, 48 }, { 56, 60 }, { 50, 60 }, { 50, 50 }, { 34, 52 }, { 28, 62 }, { 18, 62 }, { 20, 54 }, { 2, 58 } }); P({ { 32, 32 }, { 58, 32 }, { 58, 40 }, { 32, 40 } }); }
	else if (id == "minigun") { P({ { 10, 40 }, { 34, 36 }, { 34, 60 }, { 10, 58 } }); for (float y : { 38.f, 44.f, 50.f, 56.f }) P({ { 34, y }, { 90, y - 1 }, { 90, y + 3 }, { 34, y + 4 } }); P({ { 18, 58 }, { 26, 58 }, { 26, 72 }, { 18, 72 } }); }
	else if (id == "molotov") { c.Fill = CssColor(0x9fd0a0); P({ { 36, 40 }, { 60, 40 }, { 60, 82 }, { 36, 82 } }); P({ { 42, 26 }, { 54, 26 }, { 60, 40 }, { 36, 40 } }); c.Fill = CssColor(0xffb347); P({ { 44, 10 }, { 52, 12 }, { 50, 26 }, { 46, 26 } }); }
	c.Restore();
}

// Sutherland-Hodgman: a polygon clipped to a convex one (both counter-clockwise or both clockwise)
TArray<P2> ClipPoly(const TArray<P2>& Subject, const TArray<P2>& Clip) {
	TArray<P2> Out = Subject;
	float Area = 0;
	for (int32 i = 0; i < Clip.Num(); i++) { const P2 a = Clip[i], b = Clip[(i + 1) % Clip.Num()]; Area += a.X * b.Y - b.X * a.Y; }
	const float Sg = Area > 0 ? 1.f : -1.f;
	for (int32 i = 0; i < Clip.Num() && Out.Num(); i++) {
		const P2 A = Clip[i], B = Clip[(i + 1) % Clip.Num()];
		auto Inside = [&](const P2& p) { return ((B.X - A.X) * (p.Y - A.Y) - (B.Y - A.Y) * (p.X - A.X)) * Sg >= 0; };
		auto Hit = [&](const P2& p, const P2& q) { const float a1 = B.Y - A.Y, b1 = A.X - B.X, c1 = a1 * A.X + b1 * A.Y; const float a2 = q.Y - p.Y, b2 = p.X - q.X, c2 = a2 * p.X + b2 * p.Y; const float d = a1 * b2 - a2 * b1; return FMath::Abs(d) < 1e-6f ? p : P2((b2 * c1 - b1 * c2) / d, (a1 * c2 - a2 * c1) / d); };
		TArray<P2> In = Out; Out.Reset();
		for (int32 k = 0; k < In.Num(); k++) {
			const P2 Cur = In[k], Prev = In[(k + In.Num() - 1) % In.Num()];
			if (Inside(Cur)) { if (!Inside(Prev)) Out.Add(Hit(Prev, Cur)); Out.Add(Cur); }
			else if (Inside(Prev)) Out.Add(Hit(Prev, Cur));
		}
	}
	return Out;
}
}

// ------------------------------------------------------------------ the minimap (hud.js _drawRadar, modern)
void AATGHUD::DrawMinimap(atg::Game* G, AATGWorld* W, float Dt, float Left, float Top) {
	FATGPainter c(Canvas);
	c.Translate(Left, Top);
	c.Scale(Ui, Ui);
	const float Wd = 300, Ht = 190;
	const float C = Wd / 2, CY = Ht * 0.6f, R = Ht * 0.6f;
	atg::Player& p = *G->player;
	const atg::V3 pos = p.vehicle ? p.vehicle->pos : p.ragdolling && p.ragdoll ? p.ragdoll->center() : p.pos;
	const double speed = p.vehicle ? p.vehicle->speedAbs() : 0;
	const double target = p.vehicle ? 150 + FMath::Clamp(speed * 4, 0.0, 130.0) : 110;
	RadarRange += (target - RadarRange) * FMath::Min(1.0, (double)Dt * 2);
	const float scale = R / (float)RadarRange; // px per metre
	const double fy = G->rig.forwardYaw();
	const double alpha = std::atan2(std::cos(fy), std::sin(fy));
	const float theta = (float)(-PI / 2 - alpha);
	const float cs = FMath::Cos(theta), sn = FMath::Sin(theta);
	auto toRadar = [&](double x, double z) { const float dx = (float)(x - pos.x) * scale, dz = (float)(z - pos.z) * scale; return P2(dx * cs - dz * sn, dx * sn + dz * cs); };
	auto toWorld = [&](const P2& r) { const float rx = r.X - C, ry = r.Y - CY; const float dx = rx * cs + ry * sn, dz = -rx * sn + ry * cs; return P2((float)(pos.x + dx / scale), (float)(pos.z + dz / scale)); };
	// the shape (a rounded rectangle inset 3 px), as a polygon
	TArray<P2> Shape;
	{
		const float x = 3, y = 3, w = Wd - 6, h = Ht - 6, r = 6;
		auto Arc = [&](float cx, float cy, float a0, float a1) { for (int i = 0; i <= 4; i++) { const float a = a0 + (a1 - a0) * i / 4; Shape.Add(P2(cx + FMath::Cos(a) * r, cy + FMath::Sin(a) * r)); } };
		Arc(x + w - r, y + r, -PI / 2, 0); Arc(x + w - r, y + h - r, 0, PI / 2); Arc(x + r, y + h - r, PI / 2, PI); Arc(x + r, y + r, PI, PI * 1.5f);
	}
	auto ShapePath = [&]() { c.BeginPath(); c.MoveTo(Shape[0].X, Shape[0].Y); for (int32 i = 1; i < Shape.Num(); i++) c.LineTo(Shape[i].X, Shape[i].Y); c.ClosePath(); };
	c.Fill = CssColor(0x16303f); ShapePath(); c.FillPath();
	// the map layers, turned with the camera
	auto Layer = [&](UTexture2D* Tex, const FBox2D& Rect, bool bClip) {
		if (!Tex) return;
		TArray<P2> Poly = Shape;
		if (bClip) {
			TArray<P2> Box = { toRadar(Rect.Min.X, Rect.Min.Y), toRadar(Rect.Max.X, Rect.Min.Y), toRadar(Rect.Max.X, Rect.Max.Y), toRadar(Rect.Min.X, Rect.Max.Y) };
			for (P2& q : Box) { q.X += C; q.Y += CY; }
			Poly = ClipPoly(Box, Shape);
			if (Poly.Num() < 3) return;
		}
		TArray<P2> Uv;
		for (const P2& q : Poly) { const P2 w = toWorld(q); Uv.Add(P2((float)((w.X - Rect.Min.X) / (Rect.Max.X - Rect.Min.X)), (float)((w.Y - Rect.Min.Y) / (Rect.Max.Y - Rect.Min.Y)))); }
		c.TexturedPoly(Tex, Poly, Uv, FLinearColor(1, 1, 1, 0.88f));
	};
	Layer(W->MapWorldTex, W->MapWorldRect, false);
	Layer(W->MapCityTex, W->MapCityRect, true);
	// wanted: the minimap flashes red and blue while the police can see you; once they've lost you, their
	// search cones show where they're looking
	const int lvl = G->police ? G->police->wantedLevel() : 0;
	if (lvl > 0 && !G->police->searching()) {
		c.Fill = ((int)std::floor(G->time * 3) % 2) ? CssColor(0xdc2828, 0.22f) : CssColor(0x285aff, 0.22f);
		ShapePath(); c.FillPath();
	} else if (lvl > 0) {
		std::vector<atg::IPolice::RadarCone> Cones;
		G->police->radarCones(Cones);
		c.Fill = CssColor(0x4682ff, 0.28f);
		for (const auto& k : Cones) {
			const P2 r = toRadar(k.x, k.z);
			const float a = (float)(k.yaw - fy), L = (float)k.len * scale;
			// (a full circle for the helicopter: wide = pi)
			const bool Round = k.wide >= 3.14;
			const int N = Round ? 24 : 6;
			TArray<P2> Pts;
			if (!Round) Pts.Add(P2(C + r.X, CY + r.Y));
			for (int kk = -N; kk <= (Round ? N - 1 : N); kk++) { const float t = a + kk / (float)N * (float)k.wide; Pts.Add(P2(C + r.X - FMath::Sin(t) * L, CY + r.Y - FMath::Cos(t) * L)); }
			const TArray<P2> Clipped = ClipPoly(Pts, Shape);
			if (Clipped.Num() < 3) continue;
			c.BeginPath(); c.MoveTo(Clipped[0].X, Clipped[0].Y); for (int32 i = 1; i < Clipped.Num(); i++) c.LineTo(Clipped[i].X, Clipped[i].Y); c.ClosePath(); c.FillPath();
		}
	}
	// a little shade at the top and the bottom (the canvas's linear gradient, in bands)
	for (int i = 0; i < 6; i++) { c.Fill = FLinearColor(0, 0, 0, 0.28f * (1 - i / 6.f) * 0.5f); c.FillRect(3, 3 + i * (Ht - 6) * 0.25f / 6, Wd - 6, (Ht - 6) * 0.25f / 6); }
	for (int i = 0; i < 6; i++) { c.Fill = FLinearColor(0, 0, 0, 0.12f * (i / 6.f) * 0.5f); c.FillRect(3, 3 + (Ht - 6) * (0.25f + 0.75f * i / 6.f), Wd - 6, (Ht - 6) * 0.75f / 6); }
	// blips: the game's, then the police (flashing red / blue) while you're wanted
	TArray<atg::Blip> List;
	for (const auto& b : G->blips) List.Add(*b);
	if (lvl > 0 && G->police) {
		std::vector<atg::V3> Cops;
		G->police->radarCops(Cops);
		for (const atg::V3& q : Cops) { atg::Blip b; b.x = q.x; b.z = q.z; b.color = ((int)std::floor(G->time * 4) % 2) ? 0x3355ff : 0xff3333; b.icon = "dot"; b.small = true; b.noEdge = true; List.Add(b); }
	}
	const float edgeX0 = -C + 9, edgeX1 = Wd - C - 9, edgeY0 = -CY + 9, edgeY1 = Ht - CY - 9;
	for (const atg::Blip& b : List) {
		P2 r = toRadar(b.x, b.z);
		const bool out = r.X < edgeX0 || r.X > edgeX1 || r.Y < edgeY0 || r.Y > edgeY1;
		if (out) {
			if (b.noEdge) continue;
			const float k = FMath::Min(r.X < 0 ? edgeX0 / r.X : edgeX1 / r.X, r.Y < 0 ? edgeY0 / r.Y : edgeY1 / r.Y);
			r *= k;
		}
		Blip(c, C + r.X, CY + r.Y, b, b.small ? 0.8f : 1.f);
	}
	// north (on the edge of the view)
	{
		const float dx = FMath::Sin(theta), dy = -FMath::Cos(theta);
		const float k = FMath::Min(FMath::Abs(dx) > 1e-4f ? (dx < 0 ? edgeX0 / dx : edgeX1 / dx) : 1e9f, FMath::Abs(dy) > 1e-4f ? (dy < 0 ? edgeY0 / dy : edgeY1 / dy) : 1e9f);
		const float nX = C + dx * k, nY = CY + dy * k;
		c.Fill = FLinearColor(0, 0, 0, 0.7f); c.BeginPath(); c.Arc(nX, nY, 9, 0, 2 * PI); c.FillPath();
		c.Fill = FLinearColor::White; c.Text(TEXT("N"), nX, nY + 1, GEngine->GetMediumFont(), 15, 0.5f, 0.5f);
	}
	const double heading = p.vehicle ? p.vehicle->yaw : p.yaw;
	PlayerArrow(c, C, CY, (float)(heading - fy), 1);
	c.Stroke = FLinearColor(0, 0, 0, 0.85f); c.LineWidth = 4; ShapePath(); c.StrokePath();
	// ---- the bars under it: health (2), armour (1), special (1)
	const float By = Ht + 4, Bh = 9, Gap = 4, X0 = 3, Wt = Wd - 6 - Gap * 2;
	const float hp = FMath::Clamp((float)(p.health / (p.maxHealth ? p.maxHealth : 100)), 0.f, 1.f);
	const bool low = hp < 0.25f && (int)std::floor(G->time * 3) % 2 == 0;
	auto Bar = [&](float x, float w, const FLinearColor& Back, float v, const FLinearColor& Top, const FLinearColor& Bot) {
		c.Fill = FLinearColor(0, 0, 0, 0.85f); c.FillRect(x - 1, By - 1, w + 2, Bh + 2);
		c.Fill = Back; c.FillRect(x, By, w, Bh);
		c.Fill = Top; c.FillRect(x, By, w * v, Bh / 2);
		c.Fill = Bot; c.FillRect(x, By + Bh / 2, w * v, Bh / 2);
	};
	Bar(X0, Wt / 2, CssColor(0x0e200e, 0.78f), hp, low ? CssColor(0xff7a7a) : CssColor(0x78cf73), low ? CssColor(0xc62f2f) : CssColor(0x4c9c48));
	Bar(X0 + Wt / 2 + Gap, Wt / 4, CssColor(0x0c1a2e, 0.78f), FMath::Clamp((float)p.armor / 100.f, 0.f, 1.f), CssColor(0x7cc2f5), CssColor(0x3c86c8));
	// (the special ability's meter: phase 3b)
	Bar(X0 + Wt * 0.75f + Gap * 2, Wt / 4, CssColor(0x2e280a, 0.78f), 0, CssColor(0xf3d66a), CssColor(0xc9a529));
}

// ------------------------------------------------------------------ top right: stars, cash, weapon
void AATGHUD::DrawTopRight(atg::Game* G, float Right, float Top) {
	FATGPainter c(Canvas);
	c.Translate(Right, Top);
	c.Scale(Ui, Ui);
	atg::Player& p = *G->player;
	// stars (right-aligned row of five)
	const int lvl = G->police ? G->police->wantedLevel() : 0;
	const bool flash = G->police && G->police->searching() && (int)std::floor(G->time * 4) % 2 == 0;
	for (int i = 0; i < 5; i++) {
		const float x = -15 - (4 - i) * 35.f, y = 16;
		const bool on = i < lvl;
		c.BeginPath();
		for (int k = 0; k < 10; k++) { const float r = k % 2 ? 6.f : 14.f, a = k / 10.f * PI * 2 - PI / 2; if (k == 0) c.MoveTo(x + FMath::Cos(a) * r, y + FMath::Sin(a) * r); else c.LineTo(x + FMath::Cos(a) * r, y + FMath::Sin(a) * r); }
		c.ClosePath();
		if (on) { c.Fill = flash ? CssColor(0x8592b0) : FLinearColor::White; c.FillPath(); c.Stroke = FLinearColor::Black; c.LineWidth = 1.5f; c.StrokePath(); }
		else { c.Stroke = FLinearColor(1, 1, 1, 0.4f); c.LineWidth = 1.5f; c.StrokePath(); }
	}
	// cash
	c.Fill = CssColor(0xb9eab3);
	c.Text(FString::Printf(TEXT("$%08lld"), (long long)FMath::Max(0.0, FMath::Floor(p.money))), 0, 52, GEngine->GetLargeFont(), 30, 1.f, 0.5f, true);
	// weapon and ammo
	const atg::WeaponDef& def = p.weaponDef();
	c.Save(); c.Translate(-96 + 6, 72); WeaponIcon(c, p.weapon, 96); c.Restore();
	FString Ammo;
	auto it = p.weapons.find(p.weapon);
	if (def.type != "melee" && it != p.weapons.end()) {
		if (G->freeroamActive()) Ammo = TEXT("∞");
		else if (def.type == "thrown") Ammo = FString::Printf(TEXT("%d"), (int)(it->second.clip + it->second.ammo));
		else Ammo = FString::Printf(TEXT("%d  %d"), (int)it->second.clip, (int)it->second.ammo);
	}
	c.Fill = FLinearColor::White;
	c.Text(Ammo, -96, 106, GEngine->GetLargeFont(), 22, 1.f, 0.5f, true);
}

// ------------------------------------------------------------------ the speedometer (hud.js _drawSpeedo)
void AATGHUD::DrawSpeedo(atg::Game* G, float Dt) {
	atg::Player& p = *G->player;
	atg::Vehicle* v = p.vehicle && p.seat == 0 ? p.vehicle : nullptr;
	SpeedoAlpha = FMath::Clamp(SpeedoAlpha + (v && !p.dead ? Dt : -Dt) / 0.25f, 0.f, 1.f);
	if (!v || SpeedoAlpha <= 0) return;
	// (a 220 px canvas shown at 190 px, right 26, bottom 22)
	FATGPainter c(Canvas);
	c.Translate(Canvas->ClipX - (26 + 190) * Ui, Canvas->ClipY - (22 + 190) * Ui);
	c.Scale(Ui * 190 / 220, Ui * 190 / 220);
	c.Alpha = SpeedoAlpha * DeadAlpha;
	const float S = 220, C = S / 2, R = S / 2 - 12;
	const bool air = v->def.aircraft;
	const double mph = std::fabs(v->forwardSpeed()) * 2.23694;
	const double max = air ? (atg::IsSet(v->def.maxDial) ? v->def.maxDial : 400) : v->def.tank ? 60 : 160;
	SpeedoNeedle += (FMath::Min(mph, max * 1.03) - SpeedoNeedle) * FMath::Min(1.0, (double)Dt * 10);
	const float a0 = PI * 0.75f, a1 = PI * 2.25f;
	auto ang = [&](double val) { return a0 + (a1 - a0) * (float)FMath::Clamp(val / max, 0.0, 1.03); };
	// face (the radial gradient as two discs)
	c.Fill = FLinearColor(FColor(6, 7, 9)).CopyWithNewOpacity(0.92f); c.BeginPath(); c.Arc(C, C, R, 0, 2 * PI); c.FillPath();
	c.Fill = FLinearColor(FColor(28, 30, 36)).CopyWithNewOpacity(0.5f); c.BeginPath(); c.Arc(C, C * 0.92f, R * 0.7f, 0, 2 * PI); c.FillPath();
	c.LineWidth = 4; c.Stroke = FLinearColor::Black; c.BeginPath(); c.Arc(C, C, R, 0, 2 * PI); c.StrokePath();
	c.LineWidth = 1.5f; c.Stroke = CssColor(0xe8b64c, 0.8f); c.BeginPath(); c.Arc(C, C, R - 4, 0, 2 * PI); c.StrokePath();
	// red zone
	c.LineWidth = 7; c.Stroke = CssColor(0xdc281e, 0.85f);
	c.BeginPath(); c.Arc(C, C, R - 13, ang(max * 0.85), ang(max)); c.StrokePath();
	// ticks and numbers
	const double major = max <= 60 ? 10 : max <= 200 ? 20 : 50, minor = major / (max <= 60 ? 2 : 4);
	for (double val = 0; val <= max + 0.01; val += minor) {
		const float a = ang(val);
		const bool big = std::fabs(val / major - std::round(val / major)) < 1e-6;
		const float r0 = R - (big ? 24 : 18), r1 = R - 9;
		c.Stroke = big ? CssColor(0xf4f4f4) : CssColor(0xf4f4f4, 0.55f); c.LineWidth = big ? 3.f : 1.5f;
		c.BeginPath(); c.MoveTo(C + FMath::Cos(a) * r0, C + FMath::Sin(a) * r0); c.LineTo(C + FMath::Cos(a) * r1, C + FMath::Sin(a) * r1); c.StrokePath();
		if (big) { c.Fill = CssColor(0xe9e9e9); c.Text(FString::FromInt((int32)std::round(val)), C + FMath::Cos(a) * (R - 38), C + FMath::Sin(a) * (R - 38), GEngine->GetMediumFont(), 17, 0.5f, 0.5f); }
	}
	// digital readout
	c.Fill = FLinearColor::White;
	FString Digits = FString::FromInt((int32)std::round(mph));
	while (Digits.Len() < (air ? 3 : 2)) Digits = TEXT("0") + Digits;
	c.Text(Digits, C, C + R * 0.42f, GEngine->GetLargeFont(), 38, 0.5f, 0.5f);
	c.Fill = CssColor(0xe8b64c);
	c.Text(air ? TEXT("MPH · AIRSPEED") : TEXT("MPH"), C, C + R * 0.62f, GEngine->GetMediumFont(), 14, 0.5f, 0.5f);
	if (air) {
		const double alt = FMath::Max(0.0, v->altitude());
		c.Fill = CssColor(0x9fe3ff);
		c.Text(TEXT("ALT FT"), C, C - R * 0.4f, GEngine->GetMediumFont(), 12, 0.5f, 0.5f);
		c.Text(FString::FromInt((int32)std::round(alt * 3.281)), C, C - R * 0.22f, GEngine->GetMediumFont(), 19, 0.5f, 0.5f);
		// (the throttle bar comes with the aircraft)
	} else {
		const double sp = v->speed();
		const FString gear = sp < -0.5 ? FString(TEXT("R")) : mph < 1 ? FString(TEXT("N")) : FString::FromInt((int32)FMath::Min(6.0, 1 + std::floor(mph / (max / 6.2))));
		c.Fill = gear == TEXT("R") ? CssColor(0xff6b5a) : FLinearColor::White;
		c.Text(gear, C, C - R * 0.34f, GEngine->GetLargeFont(), 22, 0.5f, 0.5f);
	}
	// damage bar
	const float hp = FMath::Clamp((float)(v->health / (v->maxHealth ? v->maxHealth : 1000)), 0.f, 1.f);
	c.Fill = FLinearColor(1, 1, 1, 0.12f); c.FillRect(C - 34, C + R * 0.74f, 68, 5);
	c.Fill = hp > 0.5f ? CssColor(0x6cd46c) : hp > 0.25f ? CssColor(0xffb13b) : CssColor(0xff4a3a); c.FillRect(C - 34, C + R * 0.74f, 68 * hp, 5);
	// needle (its glow, then the needle)
	const float na = ang(SpeedoNeedle);
	c.Save(); c.Translate(C, C); c.Rotate(na);
	c.Fill = CssColor(0xff7828, 0.3f);
	c.BeginPath(); c.MoveTo(-12, -6); c.LineTo(R - 14, -3); c.LineTo(R - 14, 3); c.LineTo(-12, 6); c.ClosePath(); c.FillPath();
	c.Fill = CssColor(0xff7a1a);
	c.BeginPath(); c.MoveTo(-10, -3); c.LineTo(R - 16, -1); c.LineTo(R - 16, 1); c.LineTo(-10, 3); c.ClosePath(); c.FillPath();
	c.Restore();
	c.Fill = CssColor(0x111111); c.Stroke = CssColor(0xe8b64c); c.LineWidth = 2;
	c.BeginPath(); c.Arc(C, C, 8, 0, 2 * PI); c.FillPath(); c.StrokePath();
}

// ------------------------------------------------------------------ crosshair and sniper scope (hud.js / CSS)
void AATGHUD::DrawCrosshair(atg::Game* G) {
	atg::Player& p = *G->player;
	const atg::WeaponDef& def = p.weaponDef();
	const bool aimVisible = (p.aiming && (def.type == "gun" || def.type == "launcher") && !p.dead) || (p.vehicle && p.aiming);
	const bool scoped = G->rig.scopeBlend > 0.55;
	FATGPainter c(Canvas);
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	if (scoped) {
		// a dark ring outside 38 vmin, cross lines, a red dot and the zoom
		const float Vmin = FMath::Min(W, H) / 100.f, R = 38 * Vmin;
		// (UCanvas can't cut holes: the dark ring is an annulus of quads)
		const int32 N = 72;
		for (int32 i = 0; i < N; i++) {
			const float a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N, Ro = FMath::Max(W, H);
			c.BeginPath();
			c.MoveTo(W / 2 + FMath::Cos(a0) * R, H / 2 + FMath::Sin(a0) * R); c.LineTo(W / 2 + FMath::Cos(a0) * Ro, H / 2 + FMath::Sin(a0) * Ro);
			c.LineTo(W / 2 + FMath::Cos(a1) * Ro, H / 2 + FMath::Sin(a1) * Ro); c.LineTo(W / 2 + FMath::Cos(a1) * R, H / 2 + FMath::Sin(a1) * R); c.ClosePath();
			c.Fill = FLinearColor::Black; c.FillPath();
		}
		c.Fill = FLinearColor(0, 0, 0, 0.85f);
		c.FillRect(W / 2 - R, H / 2 - 0.75f, R * 2, 1.5f);
		c.FillRect(W / 2 - 0.75f, H / 2 - R, 1.5f, R * 2);
		c.Fill = CssColor(0xe0322a); c.BeginPath(); c.Arc(W / 2, H / 2, 2.5f, 0, 2 * PI); c.FillPath();
		c.Fill = FLinearColor::White;
		c.Text(FString::Printf(TEXT("%.1f×"), 60 / G->rig.scopeFov), W / 2, H / 2 + 30 * Vmin, GEngine->GetMediumFont(), 14 * Ui, 0.5f, 0, true);
		return;
	}
	if (!aimVisible) return;
	// four bars (30 px box; 44 px for the shotgun and SMG)
	const bool wide = def.id == "shotgun" || def.id == "smg";
	const float S = (wide ? 44 : 30) * Ui, Half = S / 2, L = 9 * Ui, T = 2 * Ui;
	const float X0 = W / 2 - Half, Y0 = H / 2 - Half;
	auto Bar = [&](float x, float y, float w, float h) { c.Fill = FLinearColor(0, 0, 0, 0.8f); c.FillRect(x - 1, y - 1, w + 2, h + 2); c.Fill = FLinearColor::White; c.FillRect(x, y, w, h); };
	Bar(X0 + Half - T / 2, Y0, T, L);
	Bar(X0 + Half - T / 2, Y0 + S - L, T, L);
	Bar(X0, Y0 + Half - T / 2, L, T);
	Bar(X0 + S - L, Y0 + Half - T / 2, L, T);
}
