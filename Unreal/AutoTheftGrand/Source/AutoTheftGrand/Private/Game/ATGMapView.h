// The pause menu's map (hud.js _tab_map): the state map centred on you at 1.6x, the district and town names,
// the GPS route, every blip and your arrow. Drag to pan, the wheel zooms, a right-click or a double-click sets
// a waypoint (again on the same spot clears it). It is drawn with FATGPainter into a render target, so the
// canvas code ports line for line, and shown as a Slate image.
#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class AATGPlayerController;

struct FSlateBrush;

class SATGMapView : public SLeafWidget {
public:
	SLATE_BEGIN_ARGS(SATGMapView) : _Width(1100.f), _Height(700.f) {}
		SLATE_ARGUMENT(AATGPlayerController*, PlayerController)
		SLATE_ARGUMENT(float, Width)   // (CSS pixels: the canvas's width and height)
		SLATE_ARGUMENT(float, Height)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SATGMapView();

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, Height); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& Geometry, const double Time, const float Dt) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

	// the view (hud.js this._mapView: kept between openings until the session ends)
	struct FView { double Cx = 0, Cz = 0, Zoom = 1.6; const void* Session = nullptr; bool bSet = false; };
	static FView& View();
	// the waypoint at a point in the map (CSS pixels from its top left), as the test hooks use it
	void SetWaypointAt(const FVector2D& Local);

private:
	AATGPlayerController* PlayerController = nullptr;
	float Width = 1100.f, Height = 700.f;
	TStrongObjectPtr<UTextureRenderTarget2D> Target;
	TSharedPtr<FSlateBrush> Brush;
	bool bDragging = false;
	FVector2D DragFrom = FVector2D::ZeroVector;
	double DragCx = 0, DragCz = 0;
	// screen pixels per metre at the current zoom (the browser's 2048 px state map, times the zoom)
	double PxPerMetre() const;
	FVector2D ToLocal(const FGeometry& Geometry, const FPointerEvent& Event) const;
	void Draw();
};
