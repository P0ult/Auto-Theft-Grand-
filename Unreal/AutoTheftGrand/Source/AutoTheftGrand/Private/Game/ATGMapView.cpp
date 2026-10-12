#include "Game/ATGMapView.h"
#include "Game/ATGHudDraw.h"
#include "Game/ATGPainter.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGWorld.h"
#include "Sim/Game.h"
#include "Sim/Hud.h"
#include "Sim/Player.h"
#include "Sim/Vehicle.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"

namespace {
// the browser's state map is 2048 px across the world (mapimage.js buildMapImage)
constexpr double MapImagePx = 2048;

AATGWorld* WorldOf(AATGPlayerController* PC) { return PC ? AATGWorld::Get(PC) : nullptr; }
}

SATGMapView::FView& SATGMapView::View() {
	static FView V;
	return V;
}

void SATGMapView::Construct(const FArguments& InArgs) {
	PlayerController = InArgs._PlayerController;
	Width = FMath::Max(64.f, InArgs._Width);
	Height = FMath::Max(64.f, InArgs._Height);
	SetCanTick(true);
	// hud.js: const view = this._mapView || { cx: p.x, cz: p.z, zoom: 1.6 } (a new session starts on you again)
	AATGWorld* W = WorldOf(PlayerController);
	atg::Game* G = W ? W->Game() : nullptr;
	FView& V = View();
	if (G && (!V.bSet || V.Session != G)) {
		const atg::V3 P = G->player->vehicle ? G->player->vehicle->pos : G->player->pos;
		V = FView();
		V.Cx = P.x; V.Cz = P.z; V.Zoom = 1.6; V.Session = G; V.bSet = true;
	}
	if (W) {
		Target.Reset(UKismetRenderingLibrary::CreateRenderTarget2D(W, FMath::RoundToInt(Width), FMath::RoundToInt(Height), RTF_RGBA8_SRGB, FLinearColor::Black, false));
		if (Target.IsValid()) {
			Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Target.Get());
			Brush->ImageSize = FVector2D(Width, Height);
			Brush->DrawAs = ESlateBrushDrawType::Image;
		}
	}
	Draw();
}

SATGMapView::~SATGMapView() {}

double SATGMapView::PxPerMetre() const {
	const AATGWorld* W = WorldOf(PlayerController);
	const double Ww = W ? W->MapWorldRect.GetSize().X : 7200.0;
	return MapImagePx / FMath::Max(1.0, Ww) * View().Zoom;
}

void SATGMapView::Tick(const FGeometry&, const double, const float) { Draw(); }

// hud.js _tab_map draw()
void SATGMapView::Draw() {
	AATGWorld* W = WorldOf(PlayerController);
	atg::Game* G = W ? W->Game() : nullptr;
	if (!Target.IsValid() || !W || !G) return;
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(W, Target.Get(), Canvas, Size, Ctx);
	if (!Canvas) return;
	const FView& V = View();
	const double K = PxPerMetre();
	const float Wd = Width, Ht = Height;
	auto W2S = [&](double X, double Z) { return FVector2f((float)((X - V.Cx) * K + Wd / 2), (float)((Z - V.Cz) * K + Ht / 2)); };
	FATGPainter c(Canvas);
	c.Fill = CssColor(0x0d2233); c.FillRect(0, 0, Wd, Ht);
	// the state layer, then the sharper city layer over it
	auto Layer = [&](UTexture2D* Tex, const FBox2D& R) {
		if (!Tex || !Tex->GetResource()) return;
		const FVector2f A = W2S(R.Min.X, R.Min.Y), B = W2S(R.Max.X, R.Max.Y);
		FCanvasTileItem T(FVector2D(A), Tex->GetResource(), FVector2D(B - A), FLinearColor::White);
		T.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(T);
	};
	Layer(W->MapWorldTex, W->MapWorldRect);
	Layer(W->MapCityTex, W->MapCityRect);
	// the names: the big ones always, the rest from 1.2x
	for (const FATGMapLabel& L : W->MapLabels) {
		if (!L.bBig && V.Zoom < 1.2) continue;
		const FVector2f P = W2S(L.X, L.Z);
		const FString Up = L.Name.ToUpper();
		const float Px = L.bBig ? 18.f : 14.f;
		c.Fill = FLinearColor(0, 0, 0, 0.6f); c.FontText(Up, P.X + 1, P.Y + 1, Px, TEXT("Hud"), 0.5f);
		c.Fill = L.bBig ? FLinearColor(CssColor(0xffecaa).R, CssColor(0xffecaa).G, CssColor(0xffecaa).B, 0.95f) : FLinearColor(1, 1, 1, 0.85f);
		c.FontText(Up, P.X, P.Y, Px, TEXT("Hud"), 0.5f);
	}
	// the GPS route
	if (G->hudModel && G->hudModel->route.size() >= 2) {
		c.Stroke = CssColor(0xd96cff); c.LineWidth = 3;
		c.BeginPath();
		for (size_t I = 0; I < G->hudModel->route.size(); I++) {
			const FVector2f P = W2S(G->hudModel->route[I].x, G->hudModel->route[I].z);
			if (I) c.LineTo(P.X, P.Y); else c.MoveTo(P.X, P.Y);
		}
		c.StrokePath();
	}
	for (const auto& B : G->blips) { const FVector2f P = W2S(B->x, B->z); ATGHudDraw::Blip(c, P.X, P.Y, *B, 1.3f); }
	const atg::Player& Pl = *G->player;
	const atg::V3 P0 = Pl.vehicle ? Pl.vehicle->pos : Pl.pos;
	const FVector2f Pp = W2S(P0.x, P0.z);
	ATGHudDraw::PlayerArrow(c, Pp.X, Pp.Y, (float)(Pl.vehicle ? Pl.vehicle->yaw : Pl.yaw), 1.4f, false);
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(W, Ctx);
}

int32 SATGMapView::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const {
	if (Brush.IsValid())
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), Brush.Get(), ESlateDrawEffect::None, Style.GetColorAndOpacityTint());
	return Layer + 1;
}

FVector2D SATGMapView::ToLocal(const FGeometry& Geometry, const FPointerEvent& Event) const {
	const FVector2D L = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const FVector2D S = Geometry.GetLocalSize();
	return FVector2D(S.X > 0 ? L.X * Width / S.X : L.X, S.Y > 0 ? L.Y * Height / S.Y : L.Y);
}

void SATGMapView::SetWaypointAt(const FVector2D& Local) {
	AATGWorld* W = WorldOf(PlayerController);
	atg::Game* G = W ? W->Game() : nullptr;
	if (!G || !G->hudModel) return;
	const FView& V = View();
	const double K = PxPerMetre();
	const double X = V.Cx + (Local.X - Width / 2) / K, Z = V.Cz + (Local.Y - Height / 2) / K;
	G->hudModel->setWaypoint(X, Z);
	G->hudModel->routeTimer = 0;
	G->hudModel->updateRoute();
	Draw();
}

FReply SATGMapView::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) {
	if (Event.GetEffectingButton() == EKeys::RightMouseButton) { SetWaypointAt(ToLocal(Geometry, Event)); return FReply::Handled(); }
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bDragging = true;
	DragFrom = ToLocal(Geometry, Event);
	DragCx = View().Cx; DragCz = View().Cz;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SATGMapView::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) {
	if (!bDragging || Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SATGMapView::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) {
	if (!bDragging) return FReply::Unhandled();
	const FVector2D L = ToLocal(Geometry, Event);
	const double K = PxPerMetre();
	View().Cx = DragCx - (L.X - DragFrom.X) / K;
	View().Cz = DragCz - (L.Y - DragFrom.Y) / K;
	Draw();
	return FReply::Handled();
}

FReply SATGMapView::OnMouseWheel(const FGeometry&, const FPointerEvent& Event) {
	// (a wheel step away from you zooms in: e.deltaY > 0 ? 0.85 : 1.18)
	View().Zoom = FMath::Clamp(View().Zoom * (Event.GetWheelDelta() < 0 ? 0.85 : 1.18), 0.3, 14.0);
	Draw();
	return FReply::Handled();
}

FReply SATGMapView::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) {
	SetWaypointAt(ToLocal(Geometry, Event));
	return FReply::Handled();
}

FCursorReply SATGMapView::OnCursorQuery(const FGeometry&, const FPointerEvent&) const {
	return FCursorReply::Cursor(bDragging ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand);
}
