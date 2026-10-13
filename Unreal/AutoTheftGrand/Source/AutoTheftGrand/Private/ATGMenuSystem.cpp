// Slate widgets for the title screen and the pause menu.
//
// The looks come from css/style.css: .title-screen / .title-logo / .title-menu / .title-foot for the title,
// and .hud-overlay.pause / .menu-panel.pause / .pause-head / .tabs / .tab / .pause-body / .pause-foot for the
// pause menu, whose tab bodies follow hud.js _tab_*. The menus own the highlight (one flag per row, so the
// controller, the keyboard and the mouse all show the same selection) rather than Slate's focus navigation,
// which would fight with it.
#include "ATGMenuSystem.h"
#include "ATGGameInstance.h"
#include "Game/ATGGameMode.h"
#include "Game/ATGPlayerController.h"
#include "Game/ATGWorld.h"
#include "ATGSaveSystem.h"
#include "Game/ATGFonts.h"
#include "Game/ATGMapView.h"
#include "Game/ATGNetText.h"
#include "Game/ATGNetTransport.h"
#include "Sim/Hud.h"
#include "HAL/IConsoleManager.h"
#include "AutoTheftGrand.h"
#include "Gen/VehicleDefs.h"
#include "Sim/Aircraft.h"
#include "Sim/Audio.h"
#include "Sim/Env.h"
#include "Sim/FreeRoam.h"
#include "Sim/Game.h"
#include "Sim/Missions.h"
#include "Sim/Net.h"
#include "Sim/Peds.h"
#include "Sim/Pickups.h"
#include "Sim/Police.h"
#include "Sim/Special.h"
#include "Sim/Traffic.h"
#include "Sim/Vehicle.h"
#include "Sim/Weapons.h"

#include "SlateCore.h"
#include "Slate.h"
#include "Algo/Reverse.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/UserInterfaceSettings.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ATGMenu"

namespace {

// ---- css/style.css :root and the menu rules -----------------------------------------------
FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float A = 1.f) { FLinearColor C(FColor(R, G, B)); C.A = A; return C; }
const FLinearColor Gold = Srgb(0xe8, 0xb6, 0x4c);            // --gold
const FLinearColor GoldLight = Srgb(0xff, 0xd2, 0x7a);       // (.btn.primary:hover)
const FLinearColor MoneyGreen = Srgb(0x3b, 0xa3, 0x4a);      // --money
const FLinearColor TextDim = Srgb(0xbb, 0xbb, 0xbb);         // .tab
const FLinearColor Muted = Srgb(0x99, 0x99, 0x99);           // .muted
const FLinearColor RowText = Srgb(0xee, 0xee, 0xee);         // .tp-btn
const FLinearColor BodyText = Srgb(0xdd, 0xdd, 0xdd);        // .menu-panel p
const FLinearColor Hairline = FLinearColor(1.f, 1.f, 1.f, 0.10f);
const FLinearColor PanelFill = Srgb(8, 10, 14, 0.86f);       // --panel
const FLinearColor RowFill = FLinearColor(1.f, 1.f, 1.f, 0.05f);   // .tp-btn
const FLinearColor RowHot = Srgb(0xe8, 0xb6, 0x4c, 0.18f);   // .tp-btn:hover
const FLinearColor BtnFill = FLinearColor(1.f, 1.f, 1.f, 0.08f);   // .btn
const FLinearColor BtnHot = Srgb(0xe8, 0xb6, 0x4c, 0.25f);   // .btn:hover
const FLinearColor TitleRowFill = FLinearColor(0.f, 0.f, 0.f, 0.45f); // .title-menu button
const FLinearColor TitleRowHot = FLinearColor(0.f, 0.f, 0.f, 0.75f);

// the four font stacks at CSS pixel sizes; letter-spacing in pixels too
FSlateFontInfo Spaced(FSlateFontInfo F, float Px, float SpacingPx) { F.LetterSpacing = Px > 0 ? (int32)FMath::RoundToInt(SpacingPx / Px * 1000.f) : 0; return F; }
FSlateFontInfo TextFont(float Px, bool bBold = false) { return ATGFonts::Get(EATGFont::Text, Px, bBold); }
FSlateFontInfo HudFont(float Px, float SpacingPx = 0.f) { return Spaced(ATGFonts::Get(EATGFont::Hud, Px), Px, SpacingPx); }
FSlateFontInfo TitleFont(float Px, float SpacingPx = 0.f) { return Spaced(ATGFonts::Get(EATGFont::Title, Px), Px, SpacingPx); }
FSlateFontInfo ScriptFont(float Px) { return ATGFonts::Get(EATGFont::Script, Px); }
// CSS line-height in ems as Slate padding: the line box shrinks (or grows) by the same amount above and below the
// glyphs (half-leading), where Slate's own line height would only trim below them
FMargin CssLineHeight(const FSlateFontInfo& Font, float Px, float Em) {
	if (!FSlateApplication::IsInitialized()) return FMargin(0.f);
	const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float Natural = (float)M->GetMaxCharacterHeight(Font);
	const float Half = Natural > 0 ? (Em * Px - Natural) / 2 : 0.f;
	return FMargin(0.f, Half, 0.f, Half);
}

const FSlateBrush* White() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }

// `.menu-panel` (a translucent panel with a hairline border and 6 px corners)
const FSlateBrush* PanelBrush() {
	static const FSlateRoundedBoxBrush B(PanelFill, FVector4(6, 6, 6, 6), FLinearColor(1.f, 1.f, 1.f, 0.08f), 1.f);
	return &B;
}
// the button backgrounds are tinted per state through ButtonColorAndOpacity, so the style itself is plain white
const FButtonStyle& FlatButton() {
	static const FButtonStyle S = FButtonStyle()
		.SetNormal(FSlateColorBrush(FLinearColor::White))
		.SetHovered(FSlateColorBrush(FLinearColor::White))
		.SetPressed(FSlateColorBrush(FLinearColor::White))
		.SetNormalPadding(FMargin(0.f))
		.SetPressedPadding(FMargin(0.f));
	return S;
}

// `.btn`'s 1 px border with 3 px corners (the row tints it: white 18 %, or gold when selected)
const FSlateBrush* ButtonOutline() {
	static const FSlateRoundedBoxBrush B(FLinearColor(0.f, 0.f, 0.f, 0.f), FVector4(3, 3, 3, 3), FLinearColor::White, 1.f);
	return &B;
}
// the map legend's dots (`.map-legend i`: 10 px, round, a black border)
const FSlateBrush* Dot(const FLinearColor& C) {
	static TMap<uint32, TSharedPtr<FSlateRoundedBoxBrush>> Cache;
	const uint32 Key = C.ToFColor(true).ToPackedARGB();
	if (TSharedPtr<FSlateRoundedBoxBrush>* Found = Cache.Find(Key)) return Found->Get();
	return Cache.Add(Key, MakeShared<FSlateRoundedBoxBrush>(C, 5.f, FLinearColor::Black, 1.f)).Get();
}

FVector2D ViewportSize() {
	FVector2D S(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport) {
		FVector2D V;
		GEngine->GameViewport->GetViewportSize(V);
		if (V.X > 200 && V.Y > 150) S = V;
	}
	return S;
}

FLinearColor HexColor(uint32 Hex) {
	auto S2L = [](double C) { return C <= 0.04045 ? C / 12.92 : std::pow((C + 0.055) / 1.055, 2.4); };
	return FLinearColor((float)S2L(((Hex >> 16) & 255) / 255.0), (float)S2L(((Hex >> 8) & 255) / 255.0), (float)S2L((Hex & 255) / 255.0), 1.f);
}

FText Txt(const std::string& S) { return FText::FromString(FString(UTF8_TO_TCHAR(S.c_str()))); }
FText Txt(const char* S) { return FText::FromString(FString(UTF8_TO_TCHAR(S))); }

// a 1 px hairline (the CSS border of the pause head and foot)
TSharedRef<SWidget> Hairline_(float Thickness = 1.f) {
	return SNew(SBox).HeightOverride(Thickness)[SNew(SImage).Image(White()).ColorAndOpacity(Hairline)];
}

// the small caption the browser shows above a list (`.muted`)
TSharedRef<SWidget> Muted_(const FText& Text) {
	return SNew(STextBlock).Text(Text).Font(TextFont(13)).ColorAndOpacity(FSlateColor(Muted)).AutoWrapText(true);
}

// the gold key label a controls row is keyed by (`.controls td:first-child`)
TSharedRef<SWidget> KeyLabel(const FText& Text) {
	return SNew(STextBlock).Text(Text).Font(HudFont(20, 1)).ColorAndOpacity(FSlateColor(Gold));
}

class SATGGradient : public SLeafWidget {
public:
	SLATE_BEGIN_ARGS(SATGGradient) {}
		SLATE_ARGUMENT(FLinearColor, Top)
		SLATE_ARGUMENT(FLinearColor, Bottom)
	SLATE_END_ARGS()
	void Construct(const FArguments& A) { Top = A._Top; Bottom = A._Bottom; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(8, 8); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override {
		TArray<FSlateGradientStop> Stops;
		Stops.Add(FSlateGradientStop(FVector2D(0, 0), Top));
		Stops.Add(FSlateGradientStop(FVector2D(0, G.GetLocalSize().Y), Bottom));
		FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(), Stops, Orient_Horizontal);
		return Layer + 1;
	}
private:
	FLinearColor Top, Bottom;
};

// Text filled with a vertical gradient (`.title-logo .l2`: background-clip: text over a linear-gradient, a black
// stroke and a drop shadow). Slate can't clip a gradient to glyphs, so the text is drawn once per thin horizontal
// band, each clipped to its band and filled with the gradient's colour there. The gradient spans the CSS line box
// (line-height x font size), centred on the glyphs.
class SATGGradientText : public SLeafWidget {
public:
	SLATE_BEGIN_ARGS(SATGGradientText) : _LineHeight(1.f), _Shadow(0.f, 0.f) {}
		SLATE_ARGUMENT(FText, Text)
		SLATE_ARGUMENT(FSlateFontInfo, Font)
		SLATE_ARGUMENT(float, LineHeight)   // (em)
		SLATE_ARGUMENT(FVector2D, Shadow)   // (drop-shadow offset, black)
		SLATE_ARGUMENT(TArray<FLinearColor>, Colors)
		SLATE_ARGUMENT(TArray<float>, Stops)
	SLATE_END_ARGS()
	void Construct(const FArguments& A) { Text = A._Text; Font = A._Font; LineHeight = A._LineHeight; Shadow = A._Shadow; Colors = A._Colors; Stops = A._Stops; }
	virtual FVector2D ComputeDesiredSize(float) const override {
		if (!FSlateApplication::IsInitialized()) return FVector2D(8, 8);
		const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		return M->Measure(Text, Font);
	}
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override {
		const FVector2f Size = G.GetLocalSize();
		if (!Shadow.IsZero()) {
			FSlateFontInfo SF = Font;
			SF.OutlineSettings.OutlineColor = FLinearColor::Black;
			FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Shadow))), Text, SF, ESlateDrawEffect::None, FLinearColor::Black);
		}
		const float Px = (float)Font.Size * 96.f / 72.f; // (Slate font sizes are points)
		const float Box = LineHeight * Px, Top = Size.Y / 2 - Box / 2;
		const int32 Bands = FMath::Clamp((int32)(Size.Y / 3.f), 8, 96);
		for (int32 I = 0; I < Bands; I++) {
			const float Y0 = Size.Y * I / Bands, Y1 = Size.Y * (I + 1) / Bands;
			const float T = FMath::Clamp(((Y0 + Y1) / 2 - Top) / FMath::Max(1.f, Box), 0.f, 1.f);
			FLinearColor C = Colors.Num() ? Colors.Last() : FLinearColor::White;
			for (int32 K = 1; K < Stops.Num() && K < Colors.Num(); K++)
				if (T <= Stops[K]) { C = FMath::Lerp(Colors[K - 1], Colors[K], (T - Stops[K - 1]) / FMath::Max(1e-4f, Stops[K] - Stops[K - 1])); break; }
			// (the band reaches past the sides so the stroke and the shadow side stay whole)
			const FVector2f A = G.LocalToAbsolute(FVector2f(-40.f, Y0)), B = G.LocalToAbsolute(FVector2f(Size.X + 40.f, Y1));
			Out.PushClip(FSlateClippingZone(FSlateRect(A.X, A.Y, B.X, B.Y)));
			FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(), Text, Font, ESlateDrawEffect::None, C);
			Out.PopClip();
		}
		return Layer + 2;
	}
private:
	FText Text;
	FSlateFontInfo Font;
	float LineHeight = 1.f;
	FVector2D Shadow;
	TArray<FLinearColor> Colors;
	TArray<float> Stops;
};

} // namespace

// ============================================================================ SATGMenuRow
void SATGMenuRow::Construct(const FArguments& A) {
	OnClicked = A._OnClicked;
	OnRightClicked = A._OnRightClicked;
	Highlight = A._Highlight;
	Active = A._Active;
	Label = A._Label;
	Note = A._Note;
	Swatch = A._Swatch;
	bPrimary = A._Primary;
	bTab = A._Tab;
	bTitle = A._Title;
	bList = A._List;
	RowPadding = A._Padding;
	// (CSS pixels: .title-menu button 32, .tab clamp(19px, 1.8vw, 24px), .tp-btn 14, .btn 15)
	FontSize = A._FontPx > 0.f ? A._FontPx : bTitle ? 32.f : bTab ? FMath::Clamp((float)ViewportSize().X * 0.018f, 19.f, 24.f) : bList ? 14.f : 15.f;
	if (RowPadding == FMargin(0.f)) RowPadding = bTab ? FMargin(FMath::Clamp((float)ViewportSize().X * 0.011f, 8.f, 16.f), 8.f) : bTitle ? FMargin(18.f, 6.f) : bList ? FMargin(10.f, 7.f) : FMargin(18.f, 10.f);

	TSharedRef<SButton> B = SAssignNew(InnerButton, SButton)
		.ButtonStyle(&FlatButton())
		.ContentPadding(FMargin(0.f))
		.ClickMethod(EButtonClickMethod::MouseDown)
		.IsFocusable(false) // (the menu drives the highlight; Slate's own navigation would double up with it)
		.OnClicked(FOnClicked::CreateSP(this, &SATGMenuRow::HandleClicked))
		.OnHovered(FSimpleDelegate::CreateSP(this, &SATGMenuRow::HandleHovered))
		.ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &SATGMenuRow::Background))
		[BuildContent()];
	if (!bTitle && !bTab && !bList) {
		// `.btn`: a 1 px border, white at 18 %, gold when selected (the primary button's matches its fill)
		ChildSlot[SNew(SBorder).BorderImage(ButtonOutline()).Padding(FMargin(1.f))
			.BorderBackgroundColor_Lambda([this]() {
				const bool bHot = Highlight.IsValid() && *Highlight;
				return FSlateColor(bPrimary || bHot ? Gold : FLinearColor(1.f, 1.f, 1.f, 0.18f));
			})[B]];
	} else ChildSlot[B];
}

bool SATGMenuRow::IsHot() const { return Highlight.IsValid() && *Highlight && (!InnerButton.IsValid() || InnerButton->IsEnabled()); }
float SATGMenuRow::Fade() const { return InnerButton.IsValid() && !InnerButton->IsEnabled() ? (bTitle ? 0.35f : 0.4f) : 1.f; }

FSlateColor SATGMenuRow::Background() const {
	const bool bHot = IsHot();
	FLinearColor C = bPrimary ? (bHot ? GoldLight : Gold) : bTitle ? (bHot ? TitleRowHot : TitleRowFill)
		: bTab ? FLinearColor(0.f, 0.f, 0.f, 0.f) : bList ? (bHot ? RowHot : RowFill) : (bHot ? BtnHot : BtnFill);
	C.A *= Fade();
	return FSlateColor(C);
}

FSlateColor SATGMenuRow::TextColor() const {
	const bool bHot = IsHot();
	const bool bOn = Active.IsValid() && *Active;
	FLinearColor C = bPrimary ? Srgb(0x1a, 0x12, 0x05) : bTitle ? (bHot ? Gold : FLinearColor::White)
		: bTab ? (bHot || bOn ? FLinearColor::White : TextDim) : bList ? (bHot ? FLinearColor::White : RowText) : FLinearColor::White;
	C.A *= Fade();
	return FSlateColor(C);
}

FSlateColor SATGMenuRow::StripColor() const {
	const bool bOn = Active.IsValid() && *Active;
	return (IsHot() || (bTab && bOn)) ? FSlateColor(Gold) : FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
}

TSharedRef<SWidget> SATGMenuRow::BuildContent() {
	TAttribute<FSlateColor> Text = TAttribute<FSlateColor>::CreateSP(this, &SATGMenuRow::TextColor);
	TAttribute<FSlateColor> Strip = TAttribute<FSlateColor>::CreateSP(this, &SATGMenuRow::StripColor);
	const bool bSwatch = Swatch.A > 0.f;
	// the display font is all capitals (Bebas Neue); its stand-ins are not
	TAttribute<FText> Shown = (bTitle || bTab) ? TAttribute<FText>::CreateLambda([L = Label]() { return FText::FromString(L.Get().ToString().ToUpper()); }) : Label;
	const FSlateFontInfo Font = bTitle ? HudFont(FontSize, 3.f) : bTab ? HudFont(FontSize, 1.5f) : TextFont(FontSize, true);

	TSharedRef<SHorizontalBox> Body = SNew(SHorizontalBox);
	if (bSwatch)
		Body->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(4.f, 0.f, 6.f, 0.f))[
			SNew(SBox).WidthOverride(26.f).HeightOverride(22.f)[SNew(SImage).Image(White()).ColorAndOpacity(Swatch)]];
	if (bTitle) // (the selected title button's text steps right: padding-left 18 -> 26)
		Body->AddSlot().AutoWidth()[SNew(SBox).WidthOverride_Lambda([this]() { return FOptionalSize(IsHot() ? 8.f : 0.f); })];
	Body->AddSlot().FillWidth(1.f).VAlign(VAlign_Center).Padding(RowPadding)[
		SNew(STextBlock).Text(Shown).Font(Font).ColorAndOpacity(Text).AutoWrapText(!bTab && !bTitle)];
	if (!Note.IsEmpty())
		Body->AddSlot().AutoWidth().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(FMargin(12.f, 0.f, 10.f, 0.f))[
			SNew(SBorder).BorderImage(PanelBrush()).Padding(FMargin(9.f, 4.f))[
				SNew(STextBlock).Text(Note).Font(TextFont(13, true))
				.ColorAndOpacity(FSlateColor(Note.ToString() == TEXT("OFF") ? TextDim : Note.ToString() == TEXT("ON") ? MoneyGreen : Gold))]];

	if (bTab) {
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center)[Body]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(3.f)[SNew(SImage).Image(White()).ColorAndOpacity(Strip)]];
	}
	if (!bTitle && !bList) return Body; // (.btn: no left border)
	// `.title-menu button` (4 px) / `.tp-btn` (3 px): a left border, transparent until the row is selected
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(bTitle ? 4.f : 3.f)[SNew(SImage).Image(White()).ColorAndOpacity(Strip)]]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Body];
}

FReply SATGMenuRow::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) {
	if (Event.GetEffectingButton() == EKeys::RightMouseButton && OnRightClicked.IsBound()) {
		const FOnClicked Action = OnRightClicked;
		return Action.Execute();
	}
	return SCompoundWidget::OnMouseButtonDown(Geometry, Event);
}

FReply SATGMenuRow::HandleClicked() {
	// A click can rebuild the tab, so do not execute a delegate stored in a row being removed.
	const FOnClicked Action = OnClicked;
	if (Action.IsBound()) return Action.Execute();
	return FReply::Handled();
}

void SATGMenuRow::HandleHovered() {
	if (OnHovered.IsBound()) OnHovered.Execute();
}

// ============================================================================ FATGMenuSystem
FATGMenuSystem::FATGMenuSystem() {}
FATGMenuSystem::~FATGMenuSystem() {}

void FATGMenuSystem::Initialize(AATGPlayerController* InPlayerController, UATGGameInstance* InGameInstance) {
	PlayerController = InPlayerController;
	GameInstance = InGameInstance;
}

atg::Game* FATGMenuSystem::Simulation() const {
	AATGWorld* W = PlayerController ? AATGWorld::Get(PlayerController) : nullptr;
	return W ? W->Game() : nullptr;
}

SATGMainMenu* FATGMenuSystem::MainWidget() const {
	return (MenuState == EATGMenuState::MainMenu && CurrentMenuWidget.IsValid()) ? static_cast<SATGMainMenu*>(CurrentMenuWidget.Get()) : nullptr;
}

SATGPauseMenu* FATGMenuSystem::PauseWidget() const {
	return (MenuState == EATGMenuState::PauseMenu && CurrentMenuWidget.IsValid()) ? static_cast<SATGPauseMenu*>(CurrentMenuWidget.Get()) : nullptr;
}

void FATGMenuSystem::ShowMainMenu() {
	if (MenuState == EATGMenuState::PauseMenu) HidePauseMenu();
	MenuState = EATGMenuState::MainMenu;
	ActivePauseTab = NAME_None; // (the tab belongs to the pause menu; don't leak it into the title's state)
	bPauseReturnsToMainMenu = false;
	// the title screen runs the city flyover, but the player takes no input (main.js leaves input.enabled false)
	if (atg::Game* G = Simulation()) {
		G->menuOpen = true;
		G->paused = false;
		G->input.enabled = false;
		G->input.keys.clear(); G->input.pressed.clear(); G->input.released.clear();
		G->input.mouse.left = G->input.mouse.right = false;
	}
	ShowMainMenuWidget();
}

void FATGMenuSystem::HideMainMenu() {
	if (MenuState != EATGMenuState::MainMenu) return;
	MenuState = EATGMenuState::None;
	if (PlayerController && PlayerController->GetWorld() && PlayerController->GetWorld()->GetGameViewport() && CurrentViewportWidget.IsValid())
		PlayerController->GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(CurrentViewportWidget.ToSharedRef());
	CurrentMenuWidget.Reset(); CurrentViewportWidget.Reset();
	if (PlayerController) { PlayerController->SetInputMode(FInputModeGameOnly()); PlayerController->bShowMouseCursor = false; }
	if (atg::Game* G = Simulation()) G->menuOpen = false;
	OnMainMenuClosed.ExecuteIfBound();
	if (OnMainMenuClosedMulticast) OnMainMenuClosedMulticast->Broadcast();
}

void FATGMenuSystem::ShowPauseMenu(FName InTab) {
	if (MenuState == EATGMenuState::None && PlayerController)
		if (AATGWorld* W = AATGWorld::Get(PlayerController))
			if (atg::Game* G = W->Game()) bIsFreeRoam = G->freeroamActive();
	bPauseCameFromMainMenu = MenuState == EATGMenuState::MainMenu;
	if (bPauseCameFromMainMenu) HideMainMenu();
	if (MenuState == EATGMenuState::PauseMenu && CurrentViewportWidget.IsValid() && PlayerController && PlayerController->GetWorld() && PlayerController->GetWorld()->GetGameViewport())
		PlayerController->GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(CurrentViewportWidget.ToSharedRef());
	CurrentMenuWidget.Reset(); CurrentViewportWidget.Reset();
	MenuState = EATGMenuState::PauseMenu;
	ActivePauseTab = InTab.IsNone() ? FName(TEXT("Map")) : InTab;
	// hud.js openPause: game.paused = true and game.menuOpen, so the world holds still behind the panel. From the
	// title screen it keeps running (main.js opens Settings with game.paused = false).
	if (atg::Game* G = Simulation()) {
		G->menuOpen = true;
		G->paused = !bPauseCameFromMainMenu;
		G->input.keys.clear(); G->input.pressed.clear(); G->input.released.clear();
		G->input.mouse.left = G->input.mouse.right = false;
	}
	ShowPauseMenuWidget(ActivePauseTab);
}

void FATGMenuSystem::HidePauseMenu() {
	if (MenuState != EATGMenuState::PauseMenu) return;
	MenuState = EATGMenuState::None;
	if (PlayerController && PlayerController->GetWorld() && PlayerController->GetWorld()->GetGameViewport() && CurrentViewportWidget.IsValid())
		PlayerController->GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(CurrentViewportWidget.ToSharedRef());
	CurrentMenuWidget.Reset(); CurrentViewportWidget.Reset();
	if (PlayerController) { PlayerController->SetInputMode(FInputModeGameOnly()); PlayerController->bShowMouseCursor = false; }
	if (atg::Game* G = Simulation()) {
		G->menuOpen = false;
		G->paused = false;
		G->input.keys.clear(); G->input.pressed.clear(); G->input.released.clear();
	}
	const bool bReturnToTitle = bPauseCameFromMainMenu || bPauseReturnsToMainMenu;
	bPauseReturnsToMainMenu = false;
	bPauseCameFromMainMenu = false;
	OnPauseMenuClosed.ExecuteIfBound();
	if (OnPauseMenuClosedMulticast) OnPauseMenuClosedMulticast->Broadcast();
	if (bReturnToTitle) ShowMainMenu();
}

void FATGMenuSystem::SwitchPauseTab(FName TabName) {
	if (MenuState == EATGMenuState::PauseMenu) {
		const TArray<FName> Tabs = bIsFreeRoam
			? TArray<FName>{ "Map", "Teleport", "Vehicles", "Admin", "Online", "Stats", "Settings", "Controls" }
			: TArray<FName>{ "Map", "Brief", "Online", "Stats", "Settings", "Controls" };
		if (!Tabs.Contains(TabName)) TabName = Tabs[0];
		ActivePauseTab = TabName;
		SwitchPauseTabInternal(TabName);
	}
}

bool FATGMenuSystem::IsPauseMenuOpen() const { return MenuState == EATGMenuState::PauseMenu; }

void FATGMenuSystem::MoveSelection(int32 X, int32 Y) {
	if (SATGMainMenu* Main = MainWidget()) Main->MoveSelection(X, Y);
	else if (SATGPauseMenu* Pause = PauseWidget()) Pause->MoveSelection(X, Y);
}

void FATGMenuSystem::ActivateSelection() {
	const TSharedPtr<SWidget> KeepAlive = CurrentMenuWidget;
	if (SATGMainMenu* Main = MainWidget()) Main->ActivateSelection();
	else if (SATGPauseMenu* Pause = PauseWidget()) Pause->ActivateSelection();
}

void FATGMenuSystem::Back() {
	const TSharedPtr<SWidget> KeepAlive = CurrentMenuWidget;
	if (SATGPauseMenu* Pause = PauseWidget()) Pause->Back();
}

void FATGMenuSystem::CyclePauseTab(int32 Direction) {
	if (MenuState != EATGMenuState::PauseMenu || !Direction) return;
	const TArray<FName> Tabs = bIsFreeRoam
		? TArray<FName>{ "Map", "Teleport", "Vehicles", "Admin", "Online", "Stats", "Settings", "Controls" }
		: TArray<FName>{ "Map", "Brief", "Online", "Stats", "Settings", "Controls" };
	int32 Index = Tabs.IndexOfByKey(GetActivePauseTab()); if (Index == INDEX_NONE) Index = 0;
	SwitchPauseTab(Tabs[(Index + Direction + Tabs.Num()) % Tabs.Num()]);
}

FString FATGMenuSystem::SelectionLabel() const {
	if (const SATGMainMenu* Main = MainWidget()) return Main->SelectionLabel();
	if (const SATGPauseMenu* Pause = PauseWidget()) return Pause->SelectionLabel();
	return {};
}

FString FATGMenuSystem::SelectionValue() const {
	const SATGPauseMenu* Pause = PauseWidget();
	return Pause ? Pause->SelectionValue() : FString();
}

bool FATGMenuSystem::SetMapWaypoint(float Fx, float Fy) {
	SATGPauseMenu* Pause = PauseWidget();
	if (!Pause || Pause->ActiveTab != FName(TEXT("Map")) || !Pause->MapView.IsValid()) return false;
	const FVector2D Size = Pause->MapView->ComputeDesiredSize(1.f);
	Pause->MapView->SetWaypointAt(FVector2D(Size.X * Fx, Size.Y * Fy));
	return true;
}

bool FATGMenuSystem::PauseDo(const FString& Action) {
	SATGPauseMenu* Pause = PauseWidget();
	if (!Pause) return false;
	Pause->DoAction(Action);
	return true;
}
bool FATGMenuSystem::PauseType(const FString& Field, const FString& Text) {
	SATGPauseMenu* Pause = PauseWidget();
	return Pause && Pause->TypeInto(Field, Text);
}
bool FATGMenuSystem::IsMenuOpen() const { return MenuState != EATGMenuState::None; }
FATGMenuSystem::EATGMenuState FATGMenuSystem::GetMenuState() const { return MenuState; }
FName FATGMenuSystem::GetActivePauseTab() const { const SATGPauseMenu* Pause = PauseWidget(); return Pause ? Pause->ActiveTab : NAME_None; }

void FATGMenuSystem::AdjustSelection(int32 Direction) {
	if (SATGPauseMenu* Pause = PauseWidget()) Pause->OnControllerAdjust(Direction);
}

TSharedRef<SWidget> FATGMenuSystem::CreateMainMenu() {
	return SNew(SATGMainMenu).OnAction(this, &FATGMenuSystem::OnMainMenuActionReceived).bHasSave(GameInstance ? GameInstance->HasSave() : false);
}
TSharedRef<SWidget> FATGMenuSystem::CreatePauseMenu() {
	return SNew(SATGPauseMenu).OnAction(this, &FATGMenuSystem::OnPauseMenuActionReceived).OnClosed(OnPauseMenuClosed).bIsFreeRoam(bIsFreeRoam).PlayerController(PlayerController);
}
void FATGMenuSystem::ShowMainMenuWidget() {
	if (!PlayerController || !PlayerController->GetWorld() || !PlayerController->GetWorld()->GetGameViewport()) return;
	CurrentMenuWidget = CreateMainMenu();
	CurrentViewportWidget = SNew(SOverlay) + SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[CurrentMenuWidget.ToSharedRef()];
	PlayerController->GetWorld()->GetGameViewport()->AddViewportWidgetContent(CurrentViewportWidget.ToSharedRef());
	FInputModeGameAndUI Mode; Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); Mode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(Mode); PlayerController->bShowMouseCursor = true;
	if (SATGMainMenu* Main = MainWidget()) Main->FocusSelection();
}
void FATGMenuSystem::ShowPauseMenuWidget(FName InTab) {
	if (!PlayerController || !PlayerController->GetWorld() || !PlayerController->GetWorld()->GetGameViewport()) return;
	CurrentMenuWidget = CreatePauseMenu();
	if (SATGPauseMenu* Pause = PauseWidget()) { Pause->InitializeTabs(bIsFreeRoam); Pause->SwitchTab(InTab); }
	CurrentViewportWidget = SNew(SOverlay) + SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[CurrentMenuWidget.ToSharedRef()];
	PlayerController->GetWorld()->GetGameViewport()->AddViewportWidgetContent(CurrentViewportWidget.ToSharedRef());
	FInputModeGameAndUI Mode; Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); Mode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(Mode); PlayerController->bShowMouseCursor = true;
	if (SATGPauseMenu* Pause = PauseWidget()) Pause->FocusSelection();
}
void FATGMenuSystem::SwitchPauseTabInternal(FName TabName) {
	if (SATGPauseMenu* Pause = PauseWidget()) Pause->SwitchTab(TabName);
}
void FATGMenuSystem::OnMainMenuActionReceived(const FString& Action) {
	OnMainMenuAction.ExecuteIfBound(Action); if (OnMainMenuActionMulticast) OnMainMenuActionMulticast->Broadcast(Action);
	if (Action == "Continue" || Action == "NewGame" || Action == "FreeRoam" || Action == "Multiplayer") HideMainMenu();
	else if ((Action == "Settings" || Action == "Controls") && MenuState != EATGMenuState::PauseMenu) ShowPauseMenu(FName(*Action));
}
void FATGMenuSystem::OnPauseMenuActionReceived(const FString& Action) {
	OnPauseMenuAction.ExecuteIfBound(Action); if (OnPauseMenuActionMulticast) OnPauseMenuActionMulticast->Broadcast(Action);
	if (Action == "Resume") HidePauseMenu();
	else if (Action == "Save" && GameInstance) GameInstance->SaveGame();
	else if (Action == "Quit") {
		bPauseReturnsToMainMenu = true;
		bPauseCameFromMainMenu = false;
		HidePauseMenu();
	}
}

// ============================================================================ SATGMainMenu
void SATGMainMenu::Construct(const FArguments& InArgs) {
	OnAction = InArgs._OnAction;
	bHasSave = InArgs._bHasSave;
	SetCanTick(true);
	ChildSlot[BuildMenu()];
}

void SATGMainMenu::Tick(const FGeometry&, const double InCurrentTime, const float) {
	if (bNewGameArmed && InCurrentTime - NewGameArmedAt > 3.5) bNewGameArmed = false; // (main.js disarms after 3.5 s)
	// (the window changed size: the clamp()s change with it)
	if (!ViewportSize().Equals(BuiltFor, 1.0)) Rebuild();
}

void SATGMainMenu::RegisterNavigationButton(TSharedRef<SButton> B, FOnClicked A, const FString& L) {
	NavigationButtons.Add(B);
	NavigationActions.Add(A);
	NavigationLabels.Add(L);
	NavigationHighlight.Add(MakeShared<bool>(false));
}

// One `.title-menu button`: big, left aligned, translucent black, a gold left border when selected.
TSharedPtr<SATGMenuRow> SATGMainMenu::AddRow(TSharedPtr<SVerticalBox> Column, TAttribute<FText> Label, FOnClicked Action, const FString& ActionName, bool bEnabled) {
	TSharedPtr<bool> Flag = MakeShared<bool>(false);
	TSharedRef<SATGMenuRow> Row = SNew(SATGMenuRow)
		.Label(Label)
		.OnClicked(Action)
		.Highlight(Flag)
		.Title(true);
	Row->Button()->SetEnabled(bEnabled);
	const int32 Index = NavigationButtons.Num();
	RegisterNavigationButton(Row->Button().ToSharedRef(), Action, ActionName);
	NavigationHighlight[Index] = Flag;
	if (Column.IsValid()) Column->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))[Row]; // (gap: 6px)
	TWeakPtr<SButton> Weak = Row->Button();
	Row->SetOnHovered(FSimpleDelegate::CreateLambda([this, Weak]() { if (Weak.IsValid()) FocusRow(Weak.Pin()); }));
	return Row;
}

// main.js showTitle with css/style.css .title-screen.transparent / .title-logo / .title-menu / .title-foot, at the
// viewport's own pixels (the CSS clamp()s are worked out from its width; the menu is rebuilt when it changes size)
TSharedRef<SWidget> SATGMainMenu::BuildMenu() {
	const FVector2D V = ViewportSize();
	BuiltFor = V;
	const float L1 = FMath::Clamp((float)V.X * 0.09f, 48.f, 140.f);
	const float L2 = FMath::Clamp((float)V.X * 0.12f, 56.f, 190.f);
	const float L3 = FMath::Clamp((float)V.X * 0.03f, 22.f, 40.f);

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	AddRow(Column, LOCTEXT("Continue", "Continue"), FOnClicked::CreateSP(this, &SATGMainMenu::OnContinueClicked), TEXT("Continue"), bHasSave);
	AddRow(Column,
		TAttribute<FText>::CreateLambda([this]() { return FText::FromString(bNewGameArmed ? TEXT("New Game? Click again") : TEXT("New Game")); }),
		FOnClicked::CreateSP(this, &SATGMainMenu::OnNewGameClicked), TEXT("New Game"), true);
	AddRow(Column, LOCTEXT("FreeRoam", "Free Roam"), FOnClicked::CreateSP(this, &SATGMainMenu::OnFreeRoamClicked), TEXT("Free Roam"), true);
	AddRow(Column, LOCTEXT("Multiplayer", "Multiplayer"), FOnClicked::CreateSP(this, &SATGMainMenu::OnMultiplayerClicked), TEXT("Multiplayer"), true);
	AddRow(Column, LOCTEXT("Settings", "Settings"), FOnClicked::CreateSP(this, &SATGMainMenu::OnSettingsClicked), TEXT("Settings"), true);
	AddRow(Column, LOCTEXT("Controls", "Controls"), FOnClicked::CreateSP(this, &SATGMainMenu::OnControlsClicked), TEXT("Controls"), true);

	// .title-logo: AUTO THEFT in white, GRAND in gold with a black stroke, Los Soles in the script; text-shadow
	// and drop-shadow 6 px down and right
	FSlateFontInfo F1 = TitleFont(L1, 4.f); F1.OutlineSettings = FFontOutlineSettings(2, FLinearColor::Black);
	FSlateFontInfo F2 = TitleFont(L2, 6.f); F2.OutlineSettings = FFontOutlineSettings(3, FLinearColor::Black);
	TSharedRef<SVerticalBox> Logo = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(CssLineHeight(F1, L1, 0.9f))[
			SNew(STextBlock).Text(LOCTEXT("Title1", "AUTO THEFT")).Font(F1)
			.ColorAndOpacity(FSlateColor(FLinearColor::White)).ShadowOffset(FVector2D(6, 6)).ShadowColorAndOpacity(FLinearColor::Black)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(CssLineHeight(F2, L2, 0.85f))[
			// (linear-gradient(180deg, #ffe89a 0%, #e8b64c 45%, #9b5b12 100%))
			SNew(SATGGradientText).Text(LOCTEXT("Title2", "GRAND")).Font(F2).LineHeight(0.85f).Shadow(FVector2D(6, 6))
			.Colors({ Srgb(0xff, 0xe8, 0x9a), Srgb(0xe8, 0xb6, 0x4c), Srgb(0x9b, 0x5b, 0x12) }).Stops({ 0.f, 0.45f, 1.f })]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)[
			SNew(STextBlock).Text(LOCTEXT("Title3", "Los Soles")).Font(ScriptFont(L3))
			.ColorAndOpacity(FSlateColor(Srgb(0xff, 0xd6, 0xa0))).ShadowOffset(FVector2D(2, 2)).ShadowColorAndOpacity(FLinearColor::Black)];

	const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint((int32)V.X, (int32)V.Y));
	return SNew(SDPIScaler).DPIScale(1.f / FMath::Max(Dpi, 0.1f))[
		SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SATGGradient).Top(Srgb(10, 6, 20, 0.25f)).Bottom(Srgb(10, 6, 20, 0.7f))]
		// (the logo's margin-top: -10vh lifts the whole block)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 0.f, (float)V.Y * 0.10f))[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Logo]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 30.f, 0.f, 0.f)[SNew(SBox).MinDesiredWidth(280.f)[Column]]]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(20.f, 0.f, 20.f, 14.f))[
			SNew(STextBlock)
			.Text(LOCTEXT("Footer", "WASD + Mouse · Xbox, PlayStation & Logitech controllers supported · An original parody inspired by open-world crime classics"))
			.Font(TextFont(12)).ColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.55f))).Justification(ETextJustify::Center).AutoWrapText(true)]];
}

void SATGMainMenu::Rebuild() {
	const int32 Keep = NavigationIndex;
	NavigationButtons.Reset(); NavigationActions.Reset(); NavigationLabels.Reset(); NavigationHighlight.Reset();
	NavigationIndex = INDEX_NONE;
	ChildSlot[BuildMenu()];
	SetFocusIndex(Keep == INDEX_NONE ? (bHasSave ? 0 : 1) : Keep);
}

bool SATGMainMenu::IsContinueEnabled() const { return bHasSave; }
FReply SATGMainMenu::OnContinueClicked() { OnAction.ExecuteIfBound("Continue"); return FReply::Handled(); }
FReply SATGMainMenu::OnNewGameClicked() {
	if (!OnAction.IsBound()) return FReply::Handled();
	// main.js: with a save on disk, ask once more before wiping it
	if (bHasSave && !bNewGameArmed) {
		bNewGameArmed = true;
		NewGameArmedAt = FSlateApplication::Get().GetCurrentTime();
		return FReply::Handled();
	}
	bNewGameArmed = false;
	OnAction.Execute(TEXT("NewGame"));
	return FReply::Handled();
}
FReply SATGMainMenu::OnFreeRoamClicked() { if (OnAction.IsBound()) OnAction.Execute(TEXT("FreeRoam")); return FReply::Handled(); }
FReply SATGMainMenu::OnMultiplayerClicked() { if (OnAction.IsBound()) OnAction.Execute(TEXT("Multiplayer")); return FReply::Handled(); }
FReply SATGMainMenu::OnSettingsClicked() { if (OnAction.IsBound()) OnAction.Execute(TEXT("Settings")); return FReply::Handled(); }
FReply SATGMainMenu::OnControlsClicked() { if (OnAction.IsBound()) OnAction.Execute(TEXT("Controls")); return FReply::Handled(); }

void SATGMainMenu::SetFocusIndex(int32 Index) {
	if (!NavigationButtons.IsValidIndex(Index)) return;
	NavigationIndex = Index;
	for (int32 I = 0; I < NavigationHighlight.Num(); I++) if (NavigationHighlight[I].IsValid()) *NavigationHighlight[I] = (I == Index);
}

void SATGMainMenu::FocusRow(TSharedPtr<SButton> RowButton) {
	if (!RowButton.IsValid()) return;
	const int32 Index = NavigationButtons.IndexOfByKey(RowButton);
	if (Index != INDEX_NONE) SetFocusIndex(Index);
}

void SATGMainMenu::FocusSelection() {
	if (NavigationButtons.IsEmpty()) return;
	// (the browser focuses Continue when there is a save, New Game otherwise)
	SetFocusIndex(NavigationIndex == INDEX_NONE ? (bHasSave ? 0 : 1) : FMath::Clamp(NavigationIndex, 0, NavigationButtons.Num() - 1));
}

void SATGMainMenu::MoveSelection(int32 X, int32 Y) {
	if (NavigationButtons.IsEmpty()) return;
	const int32 D = Y != 0 ? Y : X;
	if (!D) return;
	int32 Index = NavigationIndex == INDEX_NONE ? (bHasSave ? 0 : 1) : NavigationIndex;
	for (int32 N = 0; N < NavigationButtons.Num(); ++N) {
		Index = (Index + D + NavigationButtons.Num()) % NavigationButtons.Num();
		if (NavigationButtons[Index].IsValid() && NavigationButtons[Index]->IsEnabled()) break;
	}
	SetFocusIndex(Index);
}

void SATGMainMenu::ActivateSelection() {
	if (NavigationButtons.IsValidIndex(NavigationIndex) && NavigationButtons[NavigationIndex].IsValid()
		&& NavigationButtons[NavigationIndex]->IsEnabled() && NavigationActions[NavigationIndex].IsBound())
		NavigationActions[NavigationIndex].Execute();
}

FString SATGMainMenu::SelectionLabel() const {
	return NavigationLabels.IsValidIndex(NavigationIndex) ? NavigationLabels[NavigationIndex] : FString();
}

// ============================================================================ SATGPauseMenu
void SATGPauseMenu::Construct(const FArguments& A) {
	OnAction = A._OnAction;
	OnClosed = A._OnClosed;
	bIsFreeRoam = A._bIsFreeRoam;
	PlayerController = A._PlayerController;
	SaveText = LOCTEXT("SaveGame", "Save game");
	ChildSlot[BuildPauseMenu()];
}

atg::Game* SATGPauseMenu::GetSimulation() const {
	if (!PlayerController) return nullptr;
	AATGWorld* W = AATGWorld::Get(PlayerController);
	return W ? W->Game() : nullptr;
}

void SATGPauseMenu::RegisterTabButton(TSharedRef<SButton> B, FOnClicked A, const FString& L) {
	TabNavigationButtons.Add(B); TabNavigationActions.Add(A); TabNavigationLabels.Add(L); TabNavigationHighlight.Add(MakeShared<bool>(false));
}
void SATGPauseMenu::RegisterContentButton(TSharedRef<SButton> B, FOnClicked A, const FString& L) {
	ContentNavigationButtons.Add(B); ContentNavigationActions.Add(A); ContentNavigationLabels.Add(L); ContentNavigationHighlight.Add(MakeShared<bool>(false));
}
void SATGPauseMenu::RegisterFooterButton(TSharedRef<SButton> B, FOnClicked A, const FString& L) {
	FooterNavigationButtons.Add(B); FooterNavigationActions.Add(A); FooterNavigationLabels.Add(L); FooterNavigationHighlight.Add(MakeShared<bool>(false));
}

// Every row in the panel is made here, so the mouse and the controller always agree about the highlight.
TSharedPtr<SATGMenuRow> SATGPauseMenu::MakeRow(TAttribute<FText> Label, const FString& Action, bool bPrimary, const FText& Note, bool bList) {
	TSharedPtr<bool> Flag = MakeShared<bool>(false);
	TSharedRef<SATGMenuRow> Row = SNew(SATGMenuRow)
		.Label(Label)
		.Note(Note)
		.OnRightClicked(IsSetting(Action) ? FOnClicked::CreateSP(this, &SATGPauseMenu::ApplySetting, Action, -1) : FOnClicked())
		.Primary(bPrimary)
		.List(bList)
		.Highlight(Flag)
		.Padding(bList ? FMargin(10.f, 7.f) : FMargin(16.f, 9.f))
		.OnClicked(FOnClicked::CreateSP(this, &SATGPauseMenu::OnActionButtonClicked, Action));
	RegisterContentButton(Row->Button().ToSharedRef(), FOnClicked::CreateSP(this, &SATGPauseMenu::OnActionButtonClicked, Action), Label.Get(FText::GetEmpty()).ToString());
	ContentNavigationHighlight.Last() = Flag;
	ContentNavigationIds.Add(Action);
	TWeakPtr<SButton> Weak = Row->Button();
	Row->SetOnHovered(FSimpleDelegate::CreateLambda([this, Weak]() { if (Weak.IsValid()) FocusRow(Weak.Pin()); }));
	return Row;
}

TSharedPtr<SATGMenuRow> SATGPauseMenu::Track(TSharedPtr<SATGMenuRow> Row) {
	if (Row.IsValid()) Rows.Add(Row);
	return Row;
}

void SATGPauseMenu::AddRow(TSharedPtr<SVerticalBox> Column, TSharedPtr<SATGMenuRow> Row, float Padding) {
	if (!Row.IsValid()) return;
	Rows.Add(Row);
	if (Column.IsValid()) Column->AddSlot().AutoHeight().Padding(0.f, Padding)[Row.ToSharedRef()];
}

// a run of plain rows (the Cheats grid, the Settings list): label, what it does, and its value on the right
void SATGPauseMenu::AddRows(TSharedPtr<SVerticalBox> Column, const TArray<FATGMenuRowDef>& Defs) {
	for (const FATGMenuRowDef& D : Defs)
		AddRow(Column, MakeRow(FText::FromString(D.Label), D.Action, false, FText::FromString(D.Note), true));
}

void SATGPauseMenu::SetFocusIndex(int32 Index) {
	if (!NavigationButtons.IsValidIndex(Index)) return;
	NavigationIndex = Index;
	for (int32 I = 0; I < NavigationHighlight.Num(); I++) if (NavigationHighlight[I].IsValid()) *NavigationHighlight[I] = (I == Index);
	if (TabScroll.IsValid() && Index >= NumTabButtons && Index < NumTabButtons + ContentNavigationButtons.Num())
		TabScroll->ScrollDescendantIntoView(NavigationButtons[Index], false);
}

void SATGPauseMenu::FocusRow(TSharedPtr<SButton> RowButton) {
	if (!RowButton.IsValid() || NavigationButtons.IsEmpty()) return;
	const int32 Index = NavigationButtons.IndexOfByKey(RowButton);
	if (Index != INDEX_NONE) SetFocusIndex(Index);
}

void SATGPauseMenu::FocusSelection() {
	if (NavigationButtons.IsEmpty()) return;
	// (the browser focuses the first tab when the pause menu opens)
	SetFocusIndex(NavigationIndex == INDEX_NONE ? 0 : FMath::Clamp(NavigationIndex, 0, NavigationButtons.Num() - 1));
}

FString SATGPauseMenu::SelectedAction() const {
	return NavigationIds.IsValidIndex(NavigationIndex) ? NavigationIds[NavigationIndex] : FString();
}

// ---------------------------------------------------------------- the panel shell
// `.menu-panel.pause`: min(1180px, 96vw) wide, a translucent panel with a hairline under the head and above
// the footer, and a body of at least 320 px.
TSharedRef<SWidget> SATGPauseMenu::MakePanel(TSharedRef<SWidget> Content) {
	const FVector2D V = ViewportSize();
	// (width: min(1180px, 96vw); max-height: 94vh, the body scrolls past that)
	return SNew(SBox).WidthOverride(FMath::Min(1180.f, (float)V.X * 0.96f)).MaxDesiredHeight((float)V.Y * 0.94f)
		[SNew(SBorder).BorderImage(PanelBrush()).Padding(FMargin(22.f, 18.f))[Content]];
}

TSharedRef<SWidget> SATGPauseMenu::BuildPauseMenu() {
	TabNames = bIsFreeRoam
		? TArray<FName>{ "Map", "Teleport", "Vehicles", "Admin", "Online", "Stats", "Settings", "Controls" }
		: TArray<FName>{ "Map", "Brief", "Online", "Stats", "Settings", "Controls" };

	// `.tabs`: one tab per page, the current one underlined in gold
	TSharedRef<SHorizontalBox> TabRow = SNew(SHorizontalBox);
	for (const FName N : TabNames) {
		TSharedPtr<bool> Hot = MakeShared<bool>(false);
		TSharedPtr<bool> On = MakeShared<bool>(false);
		TSharedRef<SATGMenuRow> Row = SNew(SATGMenuRow)
			.Label(FText::FromName(N))
			.Tab(true)
			.Highlight(Hot)
			.Active(On)
			.OnClicked(FOnClicked::CreateSP(this, &SATGPauseMenu::OnTabButtonClicked, N));
		RegisterTabButton(Row->Button().ToSharedRef(), FOnClicked::CreateSP(this, &SATGPauseMenu::OnTabButtonClicked, N), N.ToString());
		TabNavigationHighlight.Last() = Hot;
		TabActiveFlags.Add(On);
		TWeakPtr<SButton> Weak = Row->Button();
		Row->SetOnHovered(FSimpleDelegate::CreateLambda([this, Weak]() { if (Weak.IsValid()) FocusRow(Weak.Pin()); }));
		TabRow->AddSlot().AutoWidth().Padding(FMargin(2.f, 0.f))[Row];
	}

	// .logo-small: AUTO THEFT in white with GRAND in gold (the title font, 30 px, 2 px apart)
	TSharedRef<SHorizontalBox> Logo = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
			SNew(STextBlock).Text(LOCTEXT("LogoSmall", "AUTO THEFT ")).Font(TitleFont(30, 2)).ColorAndOpacity(FSlateColor(FLinearColor::White))]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
			SNew(STextBlock).Text(LOCTEXT("LogoGrand", "GRAND")).Font(TitleFont(30, 2)).ColorAndOpacity(FSlateColor(Gold))];

	// `.pause-foot`: Resume (Esc) (primary), Save game and Quit to title, left aligned
	auto FooterRow = [this](TAttribute<FText> Text, FString Action, bool bPrimary) {
		TSharedPtr<bool> Flag = MakeShared<bool>(false);
		TSharedRef<SATGMenuRow> Row = SNew(SATGMenuRow)
			.Label(Text).Primary(bPrimary).Highlight(Flag)
			.OnClicked(FOnClicked::CreateSP(this, &SATGPauseMenu::OnActionButtonClicked, Action));
		FooterNavigationButtons.Add(Row->Button().ToSharedRef());
		FooterNavigationActions.Add(FOnClicked::CreateSP(this, &SATGPauseMenu::OnActionButtonClicked, Action));
		FooterNavigationLabels.Add(Action == TEXT("Resume") ? TEXT("Resume") : Action == TEXT("Save") ? TEXT("Save game") : TEXT("Quit to title"));
		FooterNavigationHighlight.Add(Flag);
		Rows.Add(Row);
		TWeakPtr<SButton> Weak = Row->Button();
		Row->SetOnHovered(FSimpleDelegate::CreateLambda([this, Weak]() { if (Weak.IsValid()) FocusRow(Weak.Pin()); }));
		return Row;
	};
	TSharedRef<SATGMenuRow> Resume = FooterRow(LOCTEXT("Resume", "Resume (Esc)"), TEXT("Resume"), true);
	TSharedRef<SATGMenuRow> Save = FooterRow(TAttribute<FText>::CreateLambda([this]() { return SaveText; }), TEXT("Save"), false);
	TSharedRef<SATGMenuRow> Quit = FooterRow(LOCTEXT("Quit", "Quit to title"), TEXT("Quit"), false);
	ResumeButton = Resume->Button(); SaveButton = Save->Button(); QuitButton = Quit->Button();

	TSharedRef<SHorizontalBox> Foot = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 6.f, 6.f, 0.f))[Resume]
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 6.f, 6.f, 0.f))[Save]
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 6.f, 6.f, 0.f))[Quit]
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNullWidget::NullWidget];

	SAssignNew(TabContentSwitcher, SWidgetSwitcher);
	TabBody = SAssignNew(TabScroll, SScrollBox)
		.Orientation(EOrientation::Orient_Vertical)
		+ SScrollBox::Slot()[TabContentSwitcher.ToSharedRef()];

	// `.pause-head` (the logo, the tabs on the right, a hairline under them), `.pause-body` (at least 320 px, 14 px
	// above and below), `.pause-foot` (a hairline over it)
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 20.f, 0.f))[Logo]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).HAlign(HAlign_Right)[TabRow]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[Hairline_()]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 14.f)[SNew(SBox).MinDesiredHeight(320.f)[TabBody.ToSharedRef()]]
		+ SVerticalBox::Slot().AutoHeight()[Hairline_()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)[Foot];

	// The viewport already applies Unreal's UI DPI curve. This menu measures in viewport pixels (CSS pixels),
	// so cancel that curve.
	const FVector2D V = ViewportSize();
	const float Scale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(V.X, V.Y));
	return SNew(SDPIScaler).DPIScale(1.f / FMath::Max(Scale, 0.1f))[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.72f))]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[MakePanel(Panel)]];
}

// ---------------------------------------------------------------- tab bodies
void SATGPauseMenu::SwitchTab(FName Name) {
	if (!TabContentSwitcher.IsValid()) return;
	if (!TabNames.Contains(Name)) Name = TabNames.Num() ? TabNames[0] : FName(TEXT("Map"));
	ActiveTab = Name;
	for (int32 I = 0; I < TabActiveFlags.Num(); I++) if (TabActiveFlags[I].IsValid()) *TabActiveFlags[I] = (TabNames.IsValidIndex(I) && TabNames[I] == Name);

	ContentNavigationButtons.Reset(); ContentNavigationActions.Reset(); ContentNavigationLabels.Reset(); ContentNavigationHighlight.Reset(); ContentNavigationIds.Reset();
	Rows.Reset();
	TSharedRef<SWidget> Content = CreateTabContent(Name);
	if (TabContentSwitcher->GetNumWidgets())
		for (int32 I = TabContentSwitcher->GetNumWidgets() - 1; I >= 0; --I)
			if (TSharedPtr<SWidget> Existing = TabContentSwitcher->GetWidget(I)) TabContentSwitcher->RemoveSlot(Existing.ToSharedRef());
	TabContentSwitcher->AddSlot()[Content];
	TabContentSwitcher->SetActiveWidget(Content);

	NavigationButtons = TabNavigationButtons; NavigationActions = TabNavigationActions; NavigationLabels = TabNavigationLabels; NavigationHighlight = TabNavigationHighlight;
	NavigationButtons.Append(ContentNavigationButtons); NavigationActions.Append(ContentNavigationActions); NavigationLabels.Append(ContentNavigationLabels); NavigationHighlight.Append(ContentNavigationHighlight);
	NavigationButtons.Append(FooterNavigationButtons); NavigationActions.Append(FooterNavigationActions); NavigationLabels.Append(FooterNavigationLabels); NavigationHighlight.Append(FooterNavigationHighlight);
	NavigationIds = TabNavigationLabels;
	NavigationIds.Append(ContentNavigationIds);
	NavigationIds.Append(TArray<FString>{ TEXT("Resume"), TEXT("Save"), TEXT("Quit") });
	NumTabButtons = TabNavigationButtons.Num();
	NavigationIndex = INDEX_NONE;
	if (TabScroll.IsValid()) TabScroll->SetScrollOffset(0.f);
	SetFocusIndex(TabNames.IndexOfByKey(Name));
}

// Rebuilding a tab after an action keeps the player on the row they were on.
void SATGPauseMenu::RefreshTab() {
	const FString Was = SelectedAction();
	const int32 WasIndex = NavigationIndex;
	const float Offset = TabScroll.IsValid() ? TabScroll->GetScrollOffset() : 0.f;
	SwitchTab(ActiveTab);
	if (TabScroll.IsValid()) TabScroll->SetScrollOffset(Offset);
	int32 Found = NavigationIds.IndexOfByKey(Was);
	if (Found == INDEX_NONE && NavigationLabels.IsValidIndex(WasIndex)) Found = WasIndex;
	if (Found != INDEX_NONE) SetFocusIndex(Found); else FocusSelection();
}

TSharedRef<SWidget> SATGPauseMenu::CreateTabContent(FName N) {
	if (N == "Map") return CreateMapTab();
	if (N == "Teleport") return CreateTeleportTab();
	if (N == "Vehicles") return CreateVehiclesTab();
	if (N == "Admin") return CreateAdminTab();
	if (N == "Online") return CreateOnlineTab();
	if (N == "Brief") return CreateBriefTab();
	if (N == "Stats") return CreateStatsTab();
	if (N == "Settings") return CreateSettingsTab();
	if (N == "Controls") return CreateControlsTab();
	return SNew(STextBlock).Text(LOCTEXT("Unknown", "Unknown tab"));
}

void SATGPauseMenu::InitializeTabs(bool bInFreeRoam) { bIsFreeRoam = bInFreeRoam; }

void SATGPauseMenu::AddHeader(TSharedPtr<SVerticalBox> Column, const FText& Text, const FText& Sub) {
	Column->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)[
		SNew(STextBlock).Text(FText::FromString(Text.ToString().ToUpper())).Font(HudFont(26)).ColorAndOpacity(FSlateColor(FLinearColor::White))];
	if (!Sub.IsEmpty())
		Column->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[
			SNew(STextBlock).Text(Sub).Font(TextFont(13)).ColorAndOpacity(FSlateColor(Srgb(0xaa, 0xaa, 0xaa))).AutoWrapText(true)];
}

void SATGPauseMenu::AddMuted(TSharedPtr<SVerticalBox> Column, const FText& Text) {
	Column->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[Muted_(Text)];
}

// A two-column table with a gold right column (`.stats table`)
TSharedRef<SWidget> SATGPauseMenu::MakeTable(const TArray<TPair<FString, FString>>& TableRows) {
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const TPair<FString, FString>& R : TableRows) {
		Box->AddSlot().AutoHeight()[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(0.f, 5.f))[
				SNew(STextBlock).Text(FText::FromString(R.Key)).Font(TextFont(16)).ColorAndOpacity(FSlateColor(RowText))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(12.f, 7.f))[
				SNew(STextBlock).Text(FText::FromString(R.Value)).Font(HudFont(22, 1)).ColorAndOpacity(FSlateColor(Gold))]];
		Box->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(1.f)[SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.08f))]];
	}
	// (`.stats table`: width min(640px, 100%))
	return SNew(SBox).MaxDesiredWidth(640.f)[Box];
}

// `.controls`: a heading, then key / action rows, in columns
TSharedRef<SWidget> SATGPauseMenu::MakeColumns(const TArray<FATGMenuGroup>& Groups) {
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).InnerSlotPadding(FVector2D(26.f, 14.f)).UseAllottedSize(true);
	for (const FATGMenuGroup& G : Groups) {
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		Col->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 6.f)[
			SNew(STextBlock).Text(FText::FromString(G.Name.ToUpper())).Font(HudFont(26)).ColorAndOpacity(FSlateColor(FLinearColor::White))];
		for (const FATGMenuRowDef& R : G.Rows) {
			Col->AddSlot().AutoHeight()[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 3.f, 16.f, 3.f))[KeyLabel(FText::FromString(R.Label))]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(0.f, 3.f))[
					SNew(STextBlock).Text(FText::FromString(R.Action)).Font(TextFont(15)).ColorAndOpacity(FSlateColor(RowText)).AutoWrapText(true)]];
		}
		Wrap->AddSlot()[SNew(SBox).MinDesiredWidth(250.f)[Col]];
	}
	return Wrap;
}

// `.tp-cols`: one column per group, each a headed list of buttons
TSharedRef<SWidget> SATGPauseMenu::MakeGroupGrid(const TArray<FATGMenuGroup>& Groups) {
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).InnerSlotPadding(FVector2D(22.f, 16.f)).UseAllottedSize(true);
	for (const FATGMenuGroup& G : Groups) {
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[
			SNew(STextBlock).Text(FText::FromString(G.Name.ToUpper())).Font(HudFont(24, 1)).ColorAndOpacity(FSlateColor(Gold))];
		for (const FATGMenuRowDef& R : G.Rows)
			AddRow(Col, MakeRow(FText::FromString(R.Label), R.Action, false, FText::FromString(R.Note), true), 1.f);
		Wrap->AddSlot()[SNew(SBox).WidthOverride(330.f)[Col]];
	}
	return Wrap;
}

TSharedRef<SWidget> SATGPauseMenu::CreateMapTab() {
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	// the canvas: min(innerWidth - 80, 1100) x min(innerHeight - 220, 700), a 2 px border with 4 px corners
	const FVector2D V = ViewportSize();
	// (the panel's head and foot are a little taller here than the browser's: 40 px more room keeps the legend in view)
	const float W = FMath::Max(200.f, FMath::Min((float)V.X - 80.f, 1100.f)), H = FMath::Max(150.f, FMath::Min((float)V.Y - 260.f, 700.f));
	static const FSlateRoundedBoxBrush Frame(FLinearColor(0.f, 0.f, 0.f, 0.f), 4.f, FLinearColor(1.f, 1.f, 1.f, 0.12f), 2.f);
	Out->AddSlot().AutoHeight().HAlign(HAlign_Center)[
		SNew(SOverlay)
		+ SOverlay::Slot()[SAssignNew(MapView, SATGMapView).PlayerController(PlayerController).Width(W).Height(H)]
		+ SOverlay::Slot()[SNew(SBorder).BorderImage(&Frame).Visibility(EVisibility::HitTestInvisible)]];
	// `.map-legend`: 13 px, #ccc, centred, the keys in bold, then the coloured dots
	auto Seg = [](const TCHAR* S, bool bBold) {
		return SNew(STextBlock).Text(FText::FromString(S)).Font(TextFont(13, bBold)).ColorAndOpacity(FSlateColor(Srgb(0xcc, 0xcc, 0xcc)));
	};
	Out->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Seg(TEXT("Right-click / double-click"), true)]
		+ SHorizontalBox::Slot().AutoWidth()[Seg(TEXT(" to set a waypoint · "), false)]
		+ SHorizontalBox::Slot().AutoWidth()[Seg(TEXT("Wheel"), true)]
		+ SHorizontalBox::Slot().AutoWidth()[Seg(TEXT(" to zoom · "), false)]
		+ SHorizontalBox::Slot().AutoWidth()[Seg(TEXT("Drag"), true)]
		+ SHorizontalBox::Slot().AutoWidth()[Seg(TEXT(" to pan"), false)]];
	TSharedRef<SHorizontalBox> Keys = SNew(SHorizontalBox);
	struct FKey { const TCHAR* Name; uint32 Hex; };
	static const FKey KeyList[] = { { TEXT("Mission"), 0xffd23f }, { TEXT("Gun Barn"), 0xff5a36 }, { TEXT("Spray Shack"), 0x6df0ff },
		{ TEXT("Safehouse"), 0x6cff6c }, { TEXT("Food"), 0xffd166 }, { TEXT("Waypoint"), 0xd96cff } };
	for (const FKey& K : KeyList) {
		Keys->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(12.f, 0.f, 4.f, 0.f))[SNew(SBox).WidthOverride(10.f).HeightOverride(10.f)[SNew(SImage).Image(Dot(HexColor(K.Hex)))]];
		Keys->AddSlot().AutoWidth().VAlign(VAlign_Center)[Seg(K.Name, false)];
	}
	Out->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 0.f)[Keys];
	return Out;
}

TSharedRef<SWidget> SATGPauseMenu::CreateTeleportTab() {
	atg::Game* G = GetSimulation();
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	if (!G || !G->freeroam || !G->freeroam->active()) {
		Out->AddSlot().AutoHeight()[Muted_(LOCTEXT("NoFR", "Teleport is a free roam feature. Pick Free Roam on the title screen."))];
		return Out;
	}
	const atg::Vehicle* V = G->player->vehicle;
	const atg::AirVehicle* Air = dynamic_cast<const atg::AirVehicle*>(V);
	const FText Note = !V ? LOCTEXT("TpFoot", "You'll arrive on foot at the pavement nearest each spot.")
		: V->def.train ? LOCTEXT("TpTrain", "You'll step off the train first — it can't leave the rails.")
		: (Air && !Air->grounded) ? FText::FromString(FString::Printf(TEXT("Your %s will arrive in the air over the destination."), UTF8_TO_TCHAR(V->def.name.c_str())))
		: FText::FromString(FString::Printf(TEXT("Your %s comes with you, parked at the kerb."), UTF8_TO_TCHAR(V->def.name.c_str())));
	AddMuted(Out, Note);

	TArray<FATGMenuGroup> Groups;
	for (const atg::FreeDestination& D : G->freeroam->destinations()) {
		const FString Group = UTF8_TO_TCHAR(D.group.c_str());
		const FString Name = UTF8_TO_TCHAR(D.name.c_str());
		int32 Found = INDEX_NONE;
		for (int32 I = 0; I < Groups.Num(); I++) if (Groups[I].Name == Group) { Found = I; break; }
		if (Found == INDEX_NONE) { FATGMenuGroup New; New.Name = Group; Groups.Add(MoveTemp(New)); Found = Groups.Num() - 1; }
		Groups[Found].Rows.Add(FATGMenuRowDef(Name, FString(TEXT("Teleport:")) + Name));
	}
	Out->AddSlot().AutoHeight()[MakeGroupGrid(Groups)];
	return Out;
}

// admin.js SPAWN_GROUPS
namespace {
void SpawnGroups(TArray<FATGMenuGroup>& Out) {
	struct FG { const char* Name; const char* Ids; };
	static const FG G[] = {
		{ "Cars", "meridian,pico,buffalo,kestrel,brawler,zenith,tempest,bouncer,summit,baller,taxi" },
		{ "Bikes & boards", "razor,trail,bmx,roadbike,skateboard" },
		{ "Boats", "dinghy,jetski,speedboat,cruiser,policeboat" },
		{ "Work & emergency", "hauler,parcel,boxer,stockade,police,sheriff,enforcer" },
		{ "Military", "ranger,barracks,mammoth" },
		{ "Aircraft", "skipper,skylark,hercules,warhawk,raptor" },
	};
	for (const FG& Group : G) {
		FATGMenuGroup Out_; Out_.Name = UTF8_TO_TCHAR(Group.Name);
		TArray<FString> Ids;
		FString(UTF8_TO_TCHAR(Group.Ids)).ParseIntoArray(Ids, TEXT(","), true);
		for (const FString& Id : Ids) {
			const atg::VehicleDef* D = atg::FindVehicle(TCHAR_TO_UTF8(*Id));
			if (!D) continue;
			const double Speed = atg::IsSet(D->vMax) ? atg::Max(D->vMax, D->top) : D->top;
			Out_.Rows.Add(FATGMenuRowDef(FString(UTF8_TO_TCHAR(D->name.c_str())), FString(TEXT("Spawn:")) + Id,
				FString::Printf(TEXT("%d mph"), (int32)FMath::RoundToInt((Speed > 0 ? Speed : 40) * 2.237))));
		}
		Out.Add(MoveTemp(Out_));
	}
}
} // namespace

TSharedRef<SWidget> SATGPauseMenu::CreateVehiclesTab() {
	atg::Game* G = GetSimulation();
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	if (!bIsFreeRoam || !G) {
		Out->AddSlot().AutoHeight()[Muted_(LOCTEXT("VehFree", "Spawning a vehicle is a free roam feature. Pick Free Roam on the title screen."))];
		return Out;
	}
	AddMuted(Out, LOCTEXT("VehTop", "Pick a vehicle to drive it away. Planes and helicopters can start in the air."));

	// the options (hud.js _spawnOpts: into, air, and the paint)
	TSharedRef<SWrapBox> Options = SNew(SWrapBox).InnerSlotPadding(FVector2D(6.f, 6.f)).UseAllottedSize(true);
	auto Toggle = [&](const FText& Text, bool bOn, const FString& Action) {
		Options->AddSlot()[SNew(SBox).WidthOverride(480.f)[Track(MakeRow(Text, Action, false, bOn ? LOCTEXT("On", "ON") : LOCTEXT("Off", "OFF"), true)).ToSharedRef()]];
	};
	Toggle(LOCTEXT("OptInto", "Put me in the driver's seat"), bSpawnInto, TEXT("SpawnInto"));
	Toggle(LOCTEXT("OptAir", "Planes & helicopters start in the air"), bSpawnAir, TEXT("SpawnAir"));
	Out->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[Options];

	static const uint32 Paints[] = { 0x111111, 0xf2f2f2, 0xc1121f, 0xf77f00, 0xffd60a, 0x2dc653, 0x00b4d8, 0x1d3557, 0x7b2cbf, 0xff4fa3, 0x8d99ae };
	TSharedRef<SHorizontalBox> Swatches = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)[Muted_(LOCTEXT("Paint", "PAINT"))];
	auto Swatch = [&](const FString& Action, FLinearColor Color, const FText& Name) {
		TSharedPtr<bool> Flag = MakeShared<bool>(false);
		TSharedRef<SATGMenuRow> Row = SNew(SATGMenuRow)
			.Label(Name).Swatch(Color).Highlight(Flag).Active(MakeShared<bool>(SpawnColor == FCString::Atoi64(*Action.RightChop(6)))).Padding(FMargin(3.f, 8.f))
			.OnClicked(FOnClicked::CreateSP(this, &SATGPauseMenu::OnActionButtonClicked, Action));
		ContentNavigationButtons.Add(Row->Button().ToSharedRef());
		ContentNavigationActions.Add(FOnClicked::CreateSP(this, &SATGPauseMenu::OnActionButtonClicked, Action));
		ContentNavigationLabels.Add(Name.IsEmpty() ? FString::Printf(TEXT("Paint #%06X"), (uint32)FCString::Atoi64(*Action.RightChop(6))) : Name.ToString());
		ContentNavigationIds.Add(Action);
		ContentNavigationHighlight.Add(Flag);
		Rows.Add(Row);
		TWeakPtr<SButton> Weak = Row->Button();
		Row->SetOnHovered(FSimpleDelegate::CreateLambda([this, Weak]() { if (Weak.IsValid()) FocusRow(Weak.Pin()); }));
		Swatches->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)[Row];
	};
	Swatch(TEXT("Paint:-1"), FLinearColor(0.f, 0.f, 0.f, 0.f), LOCTEXT("StockPaint", "Stock"));
	for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(Paints); I++) Swatch(FString::Printf(TEXT("Paint:%u"), Paints[I]), HexColor(Paints[I]), FText::GetEmpty());
	Out->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[Swatches];

	TArray<FATGMenuGroup> Groups;
	SpawnGroups(Groups);
	Out->AddSlot().AutoHeight()[MakeGroupGrid(Groups)];
	return Out;
}

TSharedRef<SWidget> SATGPauseMenu::CreateAdminTab() {
	atg::Game* G = GetSimulation();
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	if (!bIsFreeRoam || !G) {
		Out->AddSlot().AutoHeight()[Muted_(LOCTEXT("AdminFree", "Admin tools are a free roam feature. Pick Free Roam on the title screen."))];
		return Out;
	}
	// a wrapped grid of buttons, the way the Admin tab lays its sections out
	auto Grid = [this](const TArray<FATGMenuRowDef>& Defs) {
		TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).InnerSlotPadding(FVector2D(6.f, 6.f)).UseAllottedSize(true);
		for (const FATGMenuRowDef& D : Defs)
			Wrap->AddSlot()[SNew(SBox).WidthOverride(330.f)[Track(MakeRow(FText::FromString(D.Label), D.Action, false, FText::FromString(D.Note), true)).ToSharedRef()]];
		return Wrap;
	};

	AddHeader(Out, LOCTEXT("AdminCheats", "Cheats"), LOCTEXT("AdminCheatsSub", "Free roam, single player only."));
	struct FToggle { const char* L; bool atg::Cheats::*P; };
	static const FToggle Ts[] = {
		{ "God mode", &atg::Cheats::god }, { "Bulletproof vehicle", &atg::Cheats::vehGod }, { "Never wanted", &atg::Cheats::neverWanted },
		{ "Super jump", &atg::Cheats::superJump }, { "Super run", &atg::Cheats::superRun }, { "Infinite sprint", &atg::Cheats::infSprint },
		{ "Low gravity", &atg::Cheats::lowGravity }, { "Explosive bullets", &atg::Cheats::explosive }, { "One-hit kills", &atg::Cheats::oneHit },
		{ "Freeze time", &atg::Cheats::freezeTime }, { "Slow motion", &atg::Cheats::slowmo }, { "Riot", &atg::Cheats::riot },
	};
	TArray<FATGMenuRowDef> Cheats;
	for (const FToggle& T : Ts) {
		const bool bEnabled = G->freeroamActive() && (G->cheats.*(T.P));
		Cheats.Add(FATGMenuRowDef(FString(T.L), FString(TEXT("Toggle:")) + FString(T.L), bEnabled ? FString(TEXT("ON")) : FString(TEXT("OFF"))));
	}
	Out->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)[Grid(Cheats)];

	AddHeader(Out, LOCTEXT("AdminWanted", "Wanted level"), FText::GetEmpty());
	TArray<FATGMenuRowDef> Wanted;
	for (int32 N = 0; N <= 5; N++)
		Wanted.Add(FATGMenuRowDef(N ? FString::ChrN(N, TEXT('\u2605')) : FString(TEXT("Clear")), FString::Printf(TEXT("Wanted:%d"), N), G->policeSys && G->policeSys->level == N ? FString(TEXT("NOW")) : FString()));
	Out->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)[Grid(Wanted)];

	AddHeader(Out, LOCTEXT("AdminTime", "Time of day"), FText::GetEmpty());
	struct FHr { const char* L; double H; };
	static const FHr Hours[] = { { "Dawn", 6 }, { "Morning", 9 }, { "Noon", 12 }, { "Afternoon", 16 }, { "Sunset", 19.2 }, { "Night", 23 } };
	TArray<FATGMenuRowDef> Times;
	for (const FHr& H : Hours) Times.Add(FATGMenuRowDef(FString(H.L), FString::Printf(TEXT("TimeOfDay:%.1f"), H.H), FMath::Abs(G->env.hours - H.H) < 0.05 ? FString(TEXT("NOW")) : FString()));
	Out->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)[Grid(Times)];

	AddHeader(Out, LOCTEXT("AdminWeather", "Weather"), FText::GetEmpty());
	TArray<FATGMenuRowDef> Weathers;
	for (const char* W : { "clear", "cloudy", "rain", "storm", "fog" }) {
		FString Cap = FString(UTF8_TO_TCHAR(W));
		if (!Cap.IsEmpty()) Cap[0] = FChar::ToUpper(Cap[0]);
		Weathers.Add(FATGMenuRowDef(Cap, FString(TEXT("Weather:")) + UTF8_TO_TCHAR(W), G->env.weather == W ? FString(TEXT("NOW")) : FString()));
	}
	Out->AddSlot().AutoHeight()[Grid(Weathers)];
	return Out;
}

// ---------------------------------------------------------------- the Online tab (netui.js renderOnlineTab)
// Your name and colour, joining the public world or a private room code, who's here (with Go to / Waypoint) and
// the chat. The tab rebuilds when the connection, the players or the chat change; the text boxes keep their text.
namespace {

// `.online input`: #111, a 1 px #444 border with 3 px corners, white 15 px text, padding 7 x 9
const FEditableTextBoxStyle& InputStyle(bool bCode) {
	static const FSlateRoundedBoxBrush Normal(Srgb(0x11, 0x11, 0x11), 3.f, Srgb(0x44, 0x44, 0x44), 1.f);
	static const FSlateRoundedBoxBrush Focused(Srgb(0x11, 0x11, 0x11), 3.f, Srgb(0x77, 0x77, 0x77), 1.f);
	auto Make = [](bool bC) {
		FEditableTextBoxStyle S = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
		S.SetBackgroundImageNormal(Normal).SetBackgroundImageHovered(Normal).SetBackgroundImageFocused(Focused).SetBackgroundImageReadOnly(Normal)
			.SetPadding(FMargin(9.f, 7.f)).SetBackgroundColor(FSlateColor(FLinearColor::White))
			.SetForegroundColor(FSlateColor(FLinearColor::White)).SetFocusedForegroundColor(FSlateColor(FLinearColor::White))
			// (`.on-code`: the display font at 20 px, 3 px apart)
			.SetFont(bC ? HudFont(20, 3) : TextFont(15));
		return S;
	};
	static const FEditableTextBoxStyle Plain = Make(false), Code = Make(true);
	return bCode ? Code : Plain;
}

// a round brush: the player dots (`.on-p > i`, 12 px with a black border) and the colour swatches
const FSlateBrush* Round(const FLinearColor& C, float Radius, const FLinearColor& Edge, float EdgeW) {
	static TMap<FString, TSharedPtr<FSlateRoundedBoxBrush>> Cache;
	const FString Key = FString::Printf(TEXT("%08x %.1f %08x %.1f"), C.ToFColor(true).ToPackedARGB(), Radius, Edge.ToFColor(true).ToPackedARGB(), EdgeW);
	if (TSharedPtr<FSlateRoundedBoxBrush>* Found = Cache.Find(Key)) return Found->Get();
	return Cache.Add(Key, MakeShared<FSlateRoundedBoxBrush>(C, Radius, Edge, EdgeW)).Get();
}

// `.on-card`: white at 4 %, 4 px corners, padding 12 x 14
const FSlateBrush* CardBrush() {
	static const FSlateRoundedBoxBrush B(FLinearColor(1.f, 1.f, 1.f, 0.04f), 4.f);
	return &B;
}
// black at some opacity with rounded corners (`.on-p`, `.on-log`)
const FSlateBrush* Shade(float A, float R) {
	static TMap<FString, TSharedPtr<FSlateRoundedBoxBrush>> Cache;
	const FString Key = FString::Printf(TEXT("%.2f %.1f"), A, R);
	if (TSharedPtr<FSlateRoundedBoxBrush>* Found = Cache.Find(Key)) return Found->Get();
	return Cache.Add(Key, MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.f, 0.f, 0.f, A), R)).Get();
}

FLinearColor PlayerColor(uint32 Hex) { return Srgb((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255); }

FSlateFontInfo RunFont(const FATGNetRun& R, float Px) { return R.bHud ? HudFont(Px * 1.1f, 1) : ATGFonts::Get(EATGFont::Text, Px, R.bBold, R.bItalic); }

// a line of html (netui.js's innerHTML) as text: word by word, so the runs flow and wrap like inline html
TSharedRef<SWidget> HtmlLine(const FString& Html, const FLinearColor& Base, float Px, bool bItalic) {
	TSharedRef<SWrapBox> Box = SNew(SWrapBox).UseAllottedSize(true);
	for (const FATGNetRun& R : ATGNetRuns(Html, Base, bItalic)) {
		const FSlateFontInfo F = RunFont(R, Px);
		// (each word keeps the space after it; a run's leading space stays with the run)
		int32 I = 0;
		while (I < R.S.Len()) {
			int32 E = I;
			while (E < R.S.Len() && R.S[E] == TEXT(' ')) E++;
			while (E < R.S.Len() && R.S[E] != TEXT(' ')) E++;
			while (E < R.S.Len() && R.S[E] == TEXT(' ')) E++;
			Box->AddSlot().Padding(R.bHud ? FMargin(4.f, 0.f) : FMargin(0.f))[SNew(STextBlock).Text(FText::FromString(R.S.Mid(I, E - I))).Font(F).ColorAndOpacity(FSlateColor(R.Col))];
			I = E;
		}
	}
	return Box;
}

// a short line of html on one row (the status line)
TSharedRef<SWidget> HtmlRow(const FString& Html, const FLinearColor& Base, float Px) {
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
	for (const FATGNetRun& R : ATGNetRuns(Html, Base, false))
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(R.S)).Font(RunFont(R, Px)).ColorAndOpacity(FSlateColor(R.Col))];
	return Box;
}

FString RandomRoomCode() {
	static const TCHAR* A = TEXT("abcdefghjkmnpqrstuvwxyz23456789");
	const int32 N = FCString::Strlen(A);
	FString S;
	for (int32 I = 0; I < 5; I++) S.AppendChar(A[FMath::RandRange(0, N - 1)]);
	return S;
}

// netui.js: letters, digits, space, _ . - only, trimmed, at most 16
FString CleanName(const FString& In) {
	FString S;
	for (const TCHAR C : In) if (FChar::IsAlpha(C) || FChar::IsDigit(C) || C == TEXT(' ') || C == TEXT('_') || C == TEXT('.') || C == TEXT('-')) S.AppendChar(C);
	return S.TrimStartAndEnd().Left(16);
}

FString Std(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
FString EscHtml(const FString& S) { return S.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;")); }

} // namespace

TSharedPtr<bool> SATGPauseMenu::RegisterNav(TSharedRef<SButton> Button, const FString& Action, const FString& Label) {
	TSharedPtr<bool> Flag = MakeShared<bool>(false);
	ContentNavigationButtons.Add(Button);
	ContentNavigationActions.Add(FOnClicked::CreateSP(this, &SATGPauseMenu::OnActionButtonClicked, Action));
	ContentNavigationLabels.Add(Label);
	ContentNavigationIds.Add(Action);
	ContentNavigationHighlight.Add(Flag);
	TWeakPtr<SButton> Weak = Button;
	Button->SetOnHovered(FSimpleDelegate::CreateLambda([this, Weak]() { if (Weak.IsValid()) FocusRow(Weak.Pin()); }));
	return Flag;
}

TSharedRef<SWidget> SATGPauseMenu::MakeTextBox(TSharedPtr<SEditableTextBox>& Out, FString& Value, const FText& Hint, int32 MaxLen, bool bCode, const FString& EnterAction) {
	FString* V = &Value;
	return SAssignNew(Out, SEditableTextBox)
		.Style(&InputStyle(bCode))
		.Text(FText::FromString(Value))
		.HintText(Hint)
		.MaximumLength(MaxLen)
		.SelectAllTextWhenFocused(false)
		.RevertTextOnEscape(false)
		.ClearKeyboardFocusOnCommit(false)
		.OnTextChanged_Lambda([this, V, bCode](const FText& T) {
			*V = T.ToString();
			// (`.on-code` shows the code in capitals)
			if (bCode && !V->Equals(V->ToUpper(), ESearchCase::CaseSensitive)) { *V = V->ToUpper(); if (NetCodeBox.IsValid()) NetCodeBox->SetText(FText::FromString(*V)); }
		})
		.OnTextCommitted_Lambda([this, V, EnterAction](const FText& T, ETextCommit::Type How) {
			*V = T.ToString();
			if (How == ETextCommit::OnEnter && !EnterAction.IsEmpty()) OnActionButtonClicked(EnterAction);
			else if (EnterAction == TEXT("NetName")) NetApplyName(); // (name.onblur)
		});
}

void SATGPauseMenu::NetApplyName() {
	atg::Game* G = GetSimulation();
	if (!G || !G->net) return;
	const FString V = CleanName(NetNameText);
	if (!V.IsEmpty()) {
		G->net->name = TCHAR_TO_UTF8(*V);
		if (UATGGameInstance* GI = PlayerController ? Cast<UATGGameInstance>(PlayerController->GetGameInstance()) : nullptr)
			GI->SetNetSettings(V, G->net->color, G->net->pvp);
	}
	NetNameText = Std(G->net->name);
	if (NetNameBox.IsValid() && NetNameBox->GetText().ToString() != NetNameText) NetNameBox->SetText(FText::FromString(NetNameText));
}

bool SATGPauseMenu::TypeInto(const FString& Field, const FString& Text) {
	TSharedPtr<SEditableTextBox> Box = Field == TEXT("name") ? NetNameBox : Field == TEXT("code") ? NetCodeBox : Field == TEXT("say") ? NetSayBox : nullptr;
	if (!Box.IsValid()) return false;
	Box->SetText(FText::FromString(Text));
	(Field == TEXT("name") ? NetNameText : Field == TEXT("code") ? NetCodeText : NetSayText) = Field == TEXT("code") ? Text.ToUpper() : Text;
	return true;
}

void SATGPauseMenu::NetGo(const FString& Code) {
	atg::Game* G = GetSimulation();
	if (!G || !G->net) return;
	NetApplyName();
	G->net->connect(std::make_unique<FATGWsTransport>(FATGWsTransport::DefaultUrl()), TCHAR_TO_UTF8(*Code));
	RefreshOnline();
}

// what the tab shows, in short: the tab rebuilds when this changes (net.onChange / net.onChat)
FString SATGPauseMenu::OnlineSignature() const {
	atg::Game* G = GetSimulation();
	if (!G || !G->net) return FString();
	const atg::NetSystem& N = *G->net;
	FString S = FString::Printf(TEXT("%s|%d|%s|%s|%d|%d|%d|"), *Std(N.status), N.linkUp, *Std(N.error), *Std(N.code), (int32)N.chat.size(),
		N.chat.empty() ? 0 : (int32)N.chat.back().t, G->freeroamActive());
	for (const auto& KV : N.peers) {
		const atg::NetSystem::Peer& P = *KV.second;
		S += FString::Printf(TEXT("%s:%s:%d:%d:%d;"), *Std(KV.first), *Std(P.name), atg::JTruthy(P.st.get("d")), atg::JTruthy(P.st.get("pz")), atg::JTruthy(P.st.get("pvp")));
	}
	return S;
}

void SATGPauseMenu::RefreshOnline() {
	if (ActiveTab != FName(TEXT("Online"))) return;
	// (the box being typed in keeps the keyboard)
	int32 Focus = -1;
	const TSharedPtr<SEditableTextBox> Boxes[] = { NetNameBox, NetCodeBox, NetSayBox };
	for (int32 I = 0; I < 3; I++) if (Boxes[I].IsValid() && Boxes[I]->HasKeyboardFocus()) Focus = I;
	RefreshTab();
	const TSharedPtr<SEditableTextBox> Now[] = { NetNameBox, NetCodeBox, NetSayBox };
	if (Focus >= 0 && Now[Focus].IsValid() && FSlateApplication::IsInitialized()) {
		FSlateApplication::Get().SetKeyboardFocus(Now[Focus], EFocusCause::SetDirectly);
		Now[Focus]->GoTo(ETextLocation::EndOfDocument);
	}
}

void SATGPauseMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) {
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (ActiveTab != FName(TEXT("Online")) || (NetCheckT -= InDeltaTime) > 0.f) return;
	NetCheckT = 0.2f;
	if (OnlineSignature() != NetSignature) RefreshOnline();
}

TSharedRef<SWidget> SATGPauseMenu::CreateOnlineTab() {
	atg::Game* G = GetSimulation();
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	NetNameBox.Reset(); NetCodeBox.Reset(); NetSayBox.Reset();
	if (!G || !G->net) {
		AddMuted(Out, LOCTEXT("NoNet", "Multiplayer starts with the game: pick Free Roam or Multiplayer on the title screen."));
		return Out;
	}
	atg::NetSystem& N = *G->net;
	NetSignature = OnlineSignature();
	if (!bNetTextInit) { bNetTextInit = true; NetNameText = Std(N.name); NetCodeText = Std(N.code).ToUpper(); }
	const bool bOn = N.online();
	const FString Host = FATGWsTransport::DefaultUrl().Replace(TEXT("wss://"), TEXT("")).Replace(TEXT("ws://"), TEXT("")).Replace(TEXT("/mp"), TEXT(""));

	// ---- the status line: a dot, where you are, and how
	{
		const FString Where = N.code.empty() ? FString(TEXT("the <b>public world</b>")) : FString::Printf(TEXT("room <b>%s</b>"), *EscHtml(Std(N.code).ToUpper()));
		const FString St = N.status == "connecting" ? FString(TEXT("Connecting…"))
			: bOn ? FString::Printf(TEXT("Online — %s%s"), *Where, N.linkUp ? TEXT("") : TEXT(" (reconnecting…)"))
			: N.status == "error" ? FString(TEXT("Couldn't connect")) : FString(TEXT("Offline"));
		const FLinearColor DotC = bOn && N.linkUp ? Srgb(0x3d, 0xdc, 0x84) : N.status == "connecting" ? Srgb(0xe0, 0xa0, 0x30) : Srgb(0x77, 0x77, 0x77);
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		Box->AddSlot().AutoHeight()[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 7.f, 0.f)[SNew(SBox).WidthOverride(9.f).HeightOverride(9.f)[SNew(SImage).Image(Round(DotC, 4.5f, DotC, 0.f))]]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[HtmlRow(St, Srgb(0xee, 0xee, 0xee), 16.f)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)[
				SNew(STextBlock).Text(FText::FromString(TEXT("via local server"))).Font(TextFont(13)).ColorAndOpacity(FSlateColor(Muted))]];
		if (N.status == "error" && !N.error.empty())
			Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)[SNew(STextBlock).Text(FText::FromString(Std(N.error))).Font(TextFont(13)).ColorAndOpacity(FSlateColor(Srgb(0xff, 0x8a, 0x8a))).AutoWrapText(true)];
		Out->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[Box];
	}

	auto Card = [](const FText& Title, TSharedRef<SVerticalBox> Body) -> TSharedRef<SWidget> {
		Body->InsertSlot(0).AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[SNew(STextBlock).Text(Title).Font(HudFont(24, 1)).ColorAndOpacity(FSlateColor(Gold))];
		return SNew(SBorder).BorderImage(CardBrush()).Padding(FMargin(14.f, 12.f))[Body];
	};
	auto Btn = [this](const FText& Label, const FString& Action, bool bPrimary, bool bEnabled, bool bSmall) -> TSharedRef<SWidget> {
		TSharedPtr<bool> Flag = MakeShared<bool>(false);
		TSharedRef<SATGMenuRow> Row = SNew(SATGMenuRow).Label(Label).Primary(bPrimary).FontPx(bSmall ? 12.f : 0.f)
			.Padding(bSmall ? FMargin(10.f, 4.f) : FMargin(18.f, 10.f))
			.Highlight(Flag)
			.OnClicked(FOnClicked::CreateSP(this, &SATGPauseMenu::OnActionButtonClicked, Action));
		Row->Button()->SetEnabled(bEnabled);
		RegisterNav(Row->Button().ToSharedRef(), Action, Label.ToString());
		ContentNavigationHighlight.Last() = Flag;
		Rows.Add(Row);
		return Row;
	};

	// ---- you
	TSharedRef<SVerticalBox> Me = SNew(SVerticalBox);
	Me->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(58.f)[SNew(STextBlock).Text(LOCTEXT("OnName", "Name")).Font(TextFont(15)).ColorAndOpacity(FSlateColor(Srgb(0xaa, 0xaa, 0xaa)))]]
		+ SHorizontalBox::Slot().FillWidth(1.f)[MakeTextBox(NetNameBox, NetNameText, FText::GetEmpty(), 16, false, TEXT("NetName"))]];
	{
		TSharedRef<SHorizontalBox> Sw = SNew(SHorizontalBox);
		for (int32 I = 0; I < 8; I++) {
			const FLinearColor C = PlayerColor(atg::PLAYER_COLORS[I]);
			const bool bSel = I == N.color;
			const FString Action = FString::Printf(TEXT("NetColor:%d"), I);
			TSharedRef<SButton> B = SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(FMargin(0.f)).IsFocusable(false).ClickMethod(EButtonClickMethod::MouseDown)
				.ButtonColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.f))
				.OnClicked(FOnClicked::CreateSP(this, &SATGPauseMenu::OnActionButtonClicked, Action));
			const TSharedPtr<bool> Flag = RegisterNav(B, Action, FString::Printf(TEXT("Colour %d"), I + 1));
			// (`.on-swatches button`: 26 px, a 2 px dark border; the chosen one has a white ring 2 px out)
			B->SetContent(SNew(SBox).WidthOverride(34.f).HeightOverride(34.f)[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SImage)
					.Image_Lambda([Flag, bSel]() { return bSel ? Round(FLinearColor(0.f, 0.f, 0.f, 0.f), 17.f, FLinearColor::White, 2.f)
						: Flag.IsValid() && *Flag ? Round(FLinearColor(0.f, 0.f, 0.f, 0.f), 17.f, Gold, 2.f) : Round(FLinearColor(0.f, 0.f, 0.f, 0.f), 17.f, FLinearColor(0.f, 0.f, 0.f, 0.f), 0.f); })]
				+ SOverlay::Slot().Padding(4.f)[SNew(SImage).Image(Round(C, 13.f, FLinearColor(0.f, 0.f, 0.f, 0.6f), 2.f))]]);
			Sw->AddSlot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f)[B];
		}
		Me->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[Sw];
	}
	{
		TSharedPtr<SATGMenuRow> Pvp = MakeRow(LOCTEXT("OnPvp", "Player damage — others can hurt you (and you them)"), TEXT("NetPvp"), false, N.pvp ? LOCTEXT("On", "ON") : LOCTEXT("Off", "OFF"), true);
		Rows.Add(Pvp);
		Me->AddSlot().AutoHeight()[Pvp.ToSharedRef()];
	}

	// ---- join
	TSharedRef<SVerticalBox> Join = SNew(SVerticalBox);
	const bool bAvail = N.status != "connecting";
	Join->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[Btn(LOCTEXT("OnPublic", "Join the public world"), TEXT("NetPublic"), true, bAvail, false)];
	Join->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).MaxWidth(170.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[MakeTextBox(NetCodeBox, NetCodeText, LOCTEXT("OnCodeHint", "ROOM CODE"), 12, true, TEXT("NetJoin"))]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[Btn(LOCTEXT("OnJoinRoom", "Join room"), TEXT("NetJoin"), false, bAvail, false)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Btn(LOCTEXT("OnNewRoom", "New room"), TEXT("NetNew"), false, bAvail, false)]];
	if (bOn) Join->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 0.f, 0.f, 8.f)[Btn(LOCTEXT("OnLeave", "Leave multiplayer"), TEXT("NetLeave"), false, true, false)];
	{
		// (the browser's text for its local server; this port always plays through one)
		const FString Help = bOn
			? FString::Printf(TEXT("You're connected to the local game server. Friends on your network can open <b>%s</b> in their browser and join the same room code."), *EscHtml(Host))
			: FString::Printf(TEXT("Multiplayer goes through the game's own server: run <b>node server.mjs</b> in the game's folder, then join. Friends on your network can open <b>%s</b> in their browser and join the same room code. For a private session, press <b>New room</b> and share the code."), *EscHtml(Host));
		Join->AddSlot().AutoHeight()[HtmlLine(Help, Muted, 13.f, false)];
	}

	// ---- players
	const std::vector<atg::NetSystem::RosterRow> Roster = N.roster();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	auto PlayerLine = [](TSharedRef<SHorizontalBox> Line) -> TSharedRef<SWidget> {
		return SNew(SBorder).BorderImage(Shade(0.25f, 3.f)).Padding(FMargin(8.f, 6.f))[Line];
	};
	auto DotOf = [](uint32 Hex) -> TSharedRef<SWidget> { return SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)[SNew(SImage).Image(Round(PlayerColor(Hex), 6.f, FLinearColor::Black, 1.f))]; };
	if (bOn) {
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[PlayerLine(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[DotOf(atg::PLAYER_COLORS[N.color])]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Std(N.name))).Font(TextFont(15, true)).ColorAndOpacity(FSlateColor(RowText))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)[SNew(STextBlock).Text(LOCTEXT("OnYou", "(you)")).Font(TextFont(15)).ColorAndOpacity(FSlateColor(Muted))])];
	}
	const bool bFree = G->freeroamActive();
	for (const atg::NetSystem::RosterRow& P : Roster) {
		TArray<FString> Tags;
		if (!P.vehicle.empty()) Tags.Add(Std(P.vehicle));
		if (P.dead) Tags.Add(TEXT("wasted"));
		if (P.paused) Tags.Add(TEXT("paused"));
		if (!P.pvp) Tags.Add(TEXT("passive"));
		if (P.story) Tags.Add(TEXT("story mode"));
		if (P.guest) Tags.Add(TEXT("guest"));
		const FString Dist = P.dist < 1000 ? FString::Printf(TEXT("%d m"), (int32)FMath::RoundHalfFromZero(P.dist)) : FString::Printf(TEXT("%.1f km"), P.dist / 1000);
		const FString Where = Std(P.where) + TEXT(" · ") + Dist + (Tags.Num() ? TEXT(" · ") + FString::Join(Tags, TEXT(" · ")) : FString());
		const FString Id = Std(P.id);
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[PlayerLine(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[DotOf(P.col)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[SNew(STextBlock).Text(FText::FromString(Std(P.name))).Font(TextFont(15, true)).ColorAndOpacity(FSlateColor(RowText))]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[SNew(STextBlock).Text(FText::FromString(Where)).Font(TextFont(12)).ColorAndOpacity(FSlateColor(Srgb(0xaa, 0xaa, 0xaa))).AutoWrapText(true)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)[Btn(LOCTEXT("OnGoTo", "Go to"), TEXT("NetGoto:") + Id, false, bFree, true)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Btn(NetWaypointSet.Contains(Id) ? LOCTEXT("OnWpSet", "Set ✓") : LOCTEXT("OnWp", "Waypoint"), TEXT("NetWp:") + Id, false, true, true)])];
	}
	if (bOn && Roster.empty()) List->AddSlot().AutoHeight()[Muted_(LOCTEXT("OnNobody", "Nobody else here yet — share the room code, or wait for someone to join the public world."))];
	if (!bOn) List->AddSlot().AutoHeight()[Muted_(LOCTEXT("OnJoinFirst", "Join a world to see who's playing."))];
	TSharedRef<SVerticalBox> People = SNew(SVerticalBox);
	People->AddSlot().AutoHeight()[SNew(SBox).MaxDesiredHeight(240.f)[SNew(SScrollBox) + SScrollBox::Slot()[List]]];

	// ---- chat
	TSharedRef<SVerticalBox> Chat = SNew(SVerticalBox);
	{
		TSharedRef<SVerticalBox> Log = SNew(SVerticalBox);
		const int32 From = FMath::Max(0, (int32)N.chat.size() - 40);
		for (int32 I = From; I < (int32)N.chat.size(); I++) {
			const atg::NetSystem::ChatLine& L = N.chat[I];
			Log->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)[HtmlLine(ATGNetLineHtml(L.sys, L.name, L.text, L.col), L.sys ? Srgb(0xaa, 0xaa, 0xaa) : RowText, 14.f, L.sys)];
		}
		if (N.chat.empty()) Log->AddSlot().AutoHeight()[HtmlLine(TEXT("No messages yet."), Srgb(0xaa, 0xaa, 0xaa), 14.f, true)];
		TSharedRef<SScrollBox> Scroll = SNew(SScrollBox) + SScrollBox::Slot()[Log];
		Scroll->ScrollToEnd();
		Chat->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[
			SNew(SBorder).BorderImage(Shade(0.3f, 3.f)).Padding(FMargin(8.f, 6.f))[SNew(SBox).HeightOverride(158.f)[Scroll]]];
		Chat->AddSlot().AutoHeight()[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[MakeTextBox(NetSayBox, NetSayText, LOCTEXT("OnSayHint", "Say something… (in game: press / )"), 140, false, TEXT("NetSend"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Btn(LOCTEXT("OnSend", "Send"), TEXT("NetSend"), false, true, false)]];
	}

	// `.on-grid`: repeat(auto-fit, minmax(300px, 1fr)) with 14 px gaps, inside the panel's padding
	const float Avail = FMath::Min(1180.f, (float)ViewportSize().X * 0.96f) - 44.f - 14.f;
	const int32 Cols = FMath::Clamp((int32)((Avail + 14.f) / 314.f), 1, 4);
	TArray<TSharedRef<SWidget>> Cards;
	Cards.Add(Card(LOCTEXT("OnYouH", "You"), Me));
	Cards.Add(Card(LOCTEXT("OnJoinH", "Join a world"), Join));
	Cards.Add(Card(bOn ? FText::FromString(FString::Printf(TEXT("Players (%d)"), (int32)Roster.size() + 1)) : LOCTEXT("OnPlayersH", "Players"), People));
	Cards.Add(Card(LOCTEXT("OnChatH", "Chat"), Chat));
	TSharedRef<SVerticalBox> Grid = SNew(SVerticalBox);
	for (int32 I = 0; I < Cards.Num(); I += Cols) {
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
		for (int32 C = 0; C < Cols; C++)
			Line->AddSlot().FillWidth(1.f).Padding(C ? 14.f : 0.f, 0.f, 0.f, 0.f)[I + C < Cards.Num() ? Cards[I + C] : SNullWidget::NullWidget];
		Grid->AddSlot().AutoHeight().Padding(0.f, I ? 14.f : 0.f, 0.f, 0.f)[Line];
	}
	Out->AddSlot().AutoHeight()[Grid];
	return Out;
}

TSharedRef<SWidget> SATGPauseMenu::CreateBriefTab() {
	atg::Game* G = GetSimulation();
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	if (G && G->missions) {
		const std::string Title = G->missions->active ? G->missions->active->def.title : std::string();
		AddHeader(Out, Title.empty() ? LOCTEXT("NoMission", "No active mission")
				: FText::FromString(FString::Printf(TEXT("Current mission: %s"), UTF8_TO_TCHAR(Title.c_str()))),
			Title.empty() ? LOCTEXT("BriefHint", "Look for the yellow letter blips on the radar to start the next story mission.") : FText::GetEmpty());
		AddHeader(Out, LOCTEXT("StorySoFar", "Story so far"), FText::GetEmpty());
		TArray<FString> Log;
		for (auto I = G->missions->log.rbegin(); I != G->missions->log.rend() && Log.Num() < 14; ++I) Log.Add(FString(UTF8_TO_TCHAR(I->c_str())));
		Algo::Reverse(Log);
		for (const FString& Line : Log)
			Out->AddSlot().AutoHeight().Padding(0.f, 3.f)[
				SNew(STextBlock).Text(FText::FromString(TEXT("• ") + Line)).Font(TextFont(15)).ColorAndOpacity(FSlateColor(BodyText)).AutoWrapText(true)];
	}
	return Out;
}

TSharedRef<SWidget> SATGPauseMenu::CreateStatsTab() {
	atg::Game* G = GetSimulation();
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	if (!G) return Out;
	const auto& S = G->stats;
	const int32 Done = G->missions ? (int32)G->missions->completed.size() : 0;
	const int32 Total = G->missions ? (int32)G->missions->story.size() : 0;
	FString Stars;
	for (int32 I = 0; I < (int32)S.maxWanted; I++) Stars += TEXT("\u2605");
	TArray<TPair<FString, FString>> Table;
	Table.Add({ "Story progress", FString::Printf(TEXT("%d%% (%d/%d missions)"), Total ? (int32)FMath::RoundToInt(100.0 * Done / Total) : 0, Done, Total) });
	Table.Add({ "Cash", FString::Printf(TEXT("$%.0f"), G->player->money) });
	Table.Add({ "Time played", FString::Printf(TEXT("%d min"), (int32)(S.playTime / 60)) });
	Table.Add({ "People killed", FString::Printf(TEXT("%.0f"), S.kills) });
	Table.Add({ "Cops killed", FString::Printf(TEXT("%.0f"), S.copKills) });
	Table.Add({ "Headshots", FString::Printf(TEXT("%.0f"), S.headshots) });
	Table.Add({ "Vehicles stolen", FString::Printf(TEXT("%.0f"), S.carsStolen) });
	Table.Add({ "Vehicles destroyed", FString::Printf(TEXT("%.0f"), S.carsDestroyed) });
	Table.Add({ "Pedestrians run over", FString::Printf(TEXT("%.0f"), S.runOver) });
	Table.Add({ "Times wasted", FString::Printf(TEXT("%.0f"), S.wasted) });
	Table.Add({ "Times busted", FString::Printf(TEXT("%.0f"), S.busted) });
	Table.Add({ "Highest wanted level", Stars.IsEmpty() ? FString(TEXT("—")) : Stars });
	Table.Add({ "Longest drift", FString::Printf(TEXT("%.0f m"), S.bestDrift) });
	Table.Add({ "Hidden packages", FString::Printf(TEXT("%d / 30"), G->pickupsSys ? (int32)G->pickupsSys->collectedPackages.size() : 0) });
	Table.Add({ "Distance driven", FString::Printf(TEXT("%.1f km"), S.driven / 1000) });
	Table.Add({ "Distance on foot", FString::Printf(TEXT("%.1f km"), S.walked / 1000) });
	Out->AddSlot().AutoHeight().HAlign(HAlign_Left)[SNew(SBox).MinDesiredWidth(640.f)[MakeTable(Table)]];
	return Out;
}

TSharedRef<SWidget> SATGPauseMenu::CreateSettingsTab() {
	atg::Game* G = GetSimulation();
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	if (!G) return Out;
	const UATGGameInstance* GI = PlayerController ? Cast<UATGGameInstance>(PlayerController->GetGameInstance()) : nullptr;
	auto Cap = [](const std::string& S) { FString F(UTF8_TO_TCHAR(S.c_str())); if (!F.IsEmpty()) F[0] = FChar::ToUpper(F[0]); return F; };
	const double Ts = G->env.timeScale;
	const FString Speed = Ts == 1 ? TEXT("1 min / sec (default)") : Ts == 2 ? TEXT("2 min / sec") : Ts == 0.5 ? TEXT("Slow (0.5)") : Ts == 0 ? TEXT("Freeze time") : FString::Printf(TEXT("%.1f min / sec"), Ts);
	auto OnOff = [](bool b) { return b ? FString(TEXT("ON")) : FString(TEXT("OFF")); };
	TArray<FATGMenuRowDef> Defs;
	Defs.Add(FATGMenuRowDef(FString(TEXT("Graphics quality")), FString(TEXT("Quality")), Cap(G->settings.quality)));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Master volume")), FString(TEXT("Volume:0")), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(G->settings.volume * 100))));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Radio volume")), FString(TEXT("Volume:1")), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(G->settings.music * 100))));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Mouse sensitivity")), FString(TEXT("Sensitivity")), FString::Printf(TEXT("%.2f"), G->settings.sensitivity)));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Invert mouse Y")), FString(TEXT("InvertY")), OnOff(G->settings.invertY)));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Ray-traced reflections (wet streets, glass, water)")), FString(TEXT("Reflections")), OnOff(!GI || GI->IsReflectionsEnabled())));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Ambient occlusion (contact shadows)")), FString(TEXT("AO")), OnOff(!GI || GI->IsAOEnabled())));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Show FPS")), FString(TEXT("ShowFPS")), OnOff(GI && GI->IsShowFPS())));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Street crime (NPCs jaywalk, speed, mug, steal cars; police respond)")), FString(TEXT("Crime")), Cap(G->settings.npcCrime)));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Day/night speed")), FString(TEXT("TimeScale")), Speed));
	// (`.settings`: a column of rows, at most 520 px wide, 10 px apart)
	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
	for (const FATGMenuRowDef& D : Defs) AddRow(Col, MakeRow(FText::FromString(D.Label), D.Action, false, FText::FromString(D.Note), true), 5.f);
	Out->AddSlot().AutoHeight().HAlign(HAlign_Left)[SNew(SBox).WidthOverride(620.f)[Col]];
	return Out;
}

TSharedRef<SWidget> SATGPauseMenu::CreateControlsTab() {
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	AddMuted(Out, LOCTEXT("CtlHint", "Keyboard and mouse, then the standard pad layout (Xbox, PlayStation and Logitech). A selects and B / Menu resumes; LB / RB change pause tabs. D-pad / left stick navigates or scrolls, and left / right adjusts Settings."));
	TArray<FATGMenuGroup> Groups;
	Groups.Add(FATGMenuGroup("On foot", {
		{ "WASD", "Move" }, { "Mouse", "Look" }, { "Shift", "Sprint" }, { "Space", "Jump" }, { "C / Ctrl", "Crouch" },
		{ "Left mouse", "Punch / fire" }, { "Right mouse", "Aim" }, { "R", "Reload" }, { "Q / E, wheel, 1-9", "Switch weapon" },
		{ "Tab (hold)", "Weapon wheel" }, { "F / Enter", "Enter or steal a vehicle" }, { "K", "Whistle for your pet" } }));
	Groups.Add(FATGMenuGroup("In a vehicle", {
		{ "W / S", "Accelerate / brake-reverse" }, { "A / D", "Steer" }, { "Space", "Handbrake (drift!)" }, { "H", "Horn (Shift+H: siren)" },
		{ "N", "Next radio station" }, { "V", "Change camera" }, { "X (hold)", "Cinematic camera" }, { "B", "Look behind" },
		{ "Right + left mouse", "Drive-by (pistol / SMG)" }, { "G", "Hydraulics (lowriders)" }, { "F", "Exit (bail out when fast)" } }));
	Groups.Add(FATGMenuGroup("Planes & jets", {
		{ "W / S", "Throttle up / down" }, { "Mouse, up / down", "Pitch (down pulls up)" }, { "A / D", "Roll (bank to turn)" },
		{ "Q / E", "Rudder" }, { "Space", "Wheel brakes" }, { "Left / right mouse", "Cannon / homing missile" }, { "F", "Bail out" } }));
	Groups.Add(FATGMenuGroup("Helicopters", {
		{ "Space / Shift", "Climb / descend" }, { "W / S", "Fly forward / back" }, { "A / D", "Turn" }, { "Q / E", "Strafe" },
		{ "Left / right mouse", "Minigun / rockets" } }));
	Groups.Add(FATGMenuGroup("Tank", { { "W / S, A / D", "Drive, turn on the spot" }, { "Mouse / left mouse", "Aim turret / fire" } }));
	Groups.Add(FATGMenuGroup("General", {
		{ "Esc / P / Menu", "Pause, map and settings" }, { "I / D-pad up", "Phone (contacts, cheats, Snapmatic)" },
		{ "Caps Lock / Z / LS+RS", "Special ability" }, { "M / D-pad down", "Map" }, { "T", "Teleport (free roam)" },
		{ "Backtick", "Admin console (free roam)" }, { "/", "Chat (multiplayer)" }, { "H (on foot)", "Whistle for a taxi" },
		{ "Space / Enter", "Skip a cutscene line" } }));
	Groups.Add(FATGMenuGroup("Controller - on foot", {
		{ "Left stick", "Move" }, { "Right stick", "Look" }, { "A", "Sprint (hold) / select in menus" }, { "X", "Jump or open the parachute" },
		{ "LS click", "Crouch" }, { "RT", "Fire / punch / throw" }, { "LT (hold)", "Aim" }, { "B", "Reload / back in menus" },
		{ "LB / RB", "Previous / next weapon, pause tabs" }, { "LB (hold)", "Weapon wheel" }, { "Y", "Enter or steal a vehicle" },
		{ "LS + RS", "Special ability" }, { "RS click", "Whistle for your pet" }, { "View", "Change camera" }, { "Menu", "Pause" } }));
	Groups.Add(FATGMenuGroup("Controller - in a vehicle", {
		{ "RT / LT", "Accelerate / brake-reverse" }, { "Left stick", "Steer" }, { "RB", "Handbrake (drift!)" }, { "LS click", "Horn" },
		{ "LB (hold) + RB", "Drive-by: aim, shoot" }, { "RS click", "Look behind" }, { "D-pad right", "Next radio station" },
		{ "D-pad up", "Phone (hydraulics in lowriders)" }, { "A", "Nitrous (fitted at Customs)" }, { "Y", "Exit (bail out when fast)" } }));
	Out->AddSlot().AutoHeight()[MakeColumns(Groups)];
	return Out;
}

FReply SATGPauseMenu::OnTabButtonClicked(FName Name) {
	SwitchTab(Name);
	SetFocusIndex(TabNames.IndexOfByKey(Name));
	return FReply::Handled();
}
FReply SATGPauseMenu::OnResumeClicked() { if (OnAction.IsBound()) OnAction.Execute(TEXT("Resume")); return FReply::Handled(); }
FReply SATGPauseMenu::OnSaveClicked() {
	atg::Game* G = GetSimulation();
	if (!G) return FReply::Handled();
	if (G->missions && G->missions->active) { SaveText = LOCTEXT("NoSaveMission", "Can't save during a mission"); return FReply::Handled(); }
	if (G->freeRoam) { SaveText = LOCTEXT("NoSaveFree", "Free roam isn't saved"); return FReply::Handled(); }
	UATGGameInstance* GI = PlayerController ? Cast<UATGGameInstance>(PlayerController->GetGameInstance()) : nullptr;
	SaveText = GI && GI->SaveGame() ? LOCTEXT("Saved", "Saved \u2713") : LOCTEXT("SaveFailed", "Save failed · try again");
	return FReply::Handled();
}
FReply SATGPauseMenu::OnQuitClicked() { if (OnAction.IsBound()) OnAction.Execute(TEXT("Quit")); return FReply::Handled(); }

void SATGPauseMenu::MoveSelection(int32 X, int32 Y) {
	if (NavigationButtons.IsEmpty() || (X == 0 && Y == 0)) return;
	const bool bHorizontal = X != 0 && Y == 0;
	// Left/right on a settings row changes its value; left/right on the tab strip changes tabs.
	const	FString Row = SelectedAction();
	const bool bSetting = IsSetting(Row);
	if (bHorizontal && bSetting) { OnControllerAdjust(X); return; }
	if (Y != 0) X = 0; // dominant vertical input avoids diagonal jumps across the panel.
	if (ContentNavigationButtons.IsEmpty() && TabScroll.IsValid() && Y != 0 && TabScroll->GetScrollOffsetOfEnd() > 1.f) {
		const float Offset = TabScroll->GetScrollOffset();
		const float End = TabScroll->GetScrollOffsetOfEnd();
		if ((Y > 0 && Offset < End - 1.f) || (Y < 0 && Offset > 1.f)) {
			TabScroll->SetScrollOffset(FMath::Clamp(Offset + Y * 100.f, 0.f, End));
			return;
		}
	}
	if (bHorizontal && NavigationIndex >= 0 && NavigationIndex < NumTabButtons) { CycleTab(X); return; }
	if (!NavigationButtons.IsValidIndex(NavigationIndex)) { FocusSelection(); return; }
	if (NavigationIndex < NumTabButtons && Y > 0 && !ContentNavigationButtons.IsEmpty()) {
		for (int32 I = NumTabButtons; I < NumTabButtons + ContentNavigationButtons.Num(); ++I) {
			if (NavigationButtons[I]->IsEnabled()) { SetFocusIndex(I); return; }
		}
	}

	const FGeometry& CurrentGeometry = NavigationButtons[NavigationIndex]->GetCachedGeometry();
	const FVector2D CurrentSize = CurrentGeometry.GetLocalSize();
	if (CurrentSize.X <= 1.f || CurrentSize.Y <= 1.f) return;
	const FVector2D CurrentCenter = CurrentGeometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
	const FVector2D Direction((float)X, (float)Y);
	int32 BestIndex = INDEX_NONE;
	double BestScore = TNumericLimits<double>::Max();
	auto Consider = [&](int32 I) {
		if (I == NavigationIndex || !NavigationButtons[I].IsValid() || !NavigationButtons[I]->IsEnabled()) return;
		const FGeometry& Geometry = NavigationButtons[I]->GetCachedGeometry();
		const FVector2D Size = Geometry.GetLocalSize();
		if (Size.X <= 1.f || Size.Y <= 1.f) return;
		const FVector2D Delta = Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)) - CurrentCenter;
		const double Along = FVector2D::DotProduct(Delta, Direction);
		if (Along <= 1.0) return;
		// Horizontal movement stays on the current visual row instead of jumping to nearby swatches.
		if (X != 0 && FMath::Abs(Delta.Y) > FMath::Min(Size.Y, CurrentSize.Y) * 0.5) return;
		const double Across = FMath::Abs(Delta.X * Direction.Y - Delta.Y * Direction.X);
		const double Score = Along + Across * 2.0;
		if (Score < BestScore) { BestScore = Score; BestIndex = I; }
	};
	const bool bOnTabs = NavigationIndex < NumTabButtons;
	const bool bOnFooter = NavigationIndex >= NumTabButtons + ContentNavigationButtons.Num();
	const int32 ContentStart = NumTabButtons;
	const int32 ContentEnd = NumTabButtons + ContentNavigationButtons.Num();
	const int32 FooterStart = ContentEnd;
	if (bOnTabs) {
		if (Y > 0) for (int32 I = ContentStart; I < ContentEnd; ++I) Consider(I);
		else if (Y < 0) for (int32 I = FooterStart; I < NavigationButtons.Num(); ++I) Consider(I);
		else for (int32 I = 0; I < NumTabButtons; ++I) Consider(I);
	} else if (bOnFooter) {
		if (Y < 0) for (int32 I = ContentStart; I < ContentEnd; ++I) Consider(I);
		else if (Y > 0) for (int32 I = 0; I < NumTabButtons; ++I) Consider(I);
		else for (int32 I = FooterStart; I < NavigationButtons.Num(); ++I) Consider(I);
	} else {
		for (int32 I = ContentStart; I < ContentEnd; ++I) Consider(I);
		if (BestIndex == INDEX_NONE) {
			if (Y > 0) for (int32 I = FooterStart; I < NavigationButtons.Num(); ++I) Consider(I);
			else if (Y < 0) for (int32 I = 0; I < NumTabButtons; ++I) Consider(I);
		}
	}
	if (BestIndex == INDEX_NONE) {
		// Empty tabs have no body controls; use the footer so navigation never dead-ends.
		for (int32 I = FooterStart; I < NavigationButtons.Num(); ++I) Consider(I);
	}
	if (BestIndex == INDEX_NONE && bOnFooter && Y < 0 && ContentNavigationButtons.IsEmpty()) {
		SetFocusIndex(TabNames.IndexOfByKey(ActiveTab));
		return;
	}
	if (BestIndex == INDEX_NONE) return;
	if (BestIndex < NumTabButtons) {
		SwitchTab(TabNames[BestIndex]);
		SetFocusIndex(BestIndex);
	} else {
		SetFocusIndex(BestIndex);
	}
}

void SATGPauseMenu::ActivateSelection() {
	if (!NavigationButtons.IsValidIndex(NavigationIndex) || !NavigationButtons[NavigationIndex]->IsEnabled()) return;
	if (NavigationActions.IsValidIndex(NavigationIndex)) {
		const FOnClicked Action = NavigationActions[NavigationIndex];
		if (Action.IsBound()) Action.Execute();
	}
}

void SATGPauseMenu::Back() { OnResumeClicked(); }

void SATGPauseMenu::CycleTab(int32 Direction) {
	if (TabNames.IsEmpty() || Direction == 0) return;
	int32 Index = TabNames.IndexOfByKey(ActiveTab);
	if (Index == INDEX_NONE) Index = 0;
	const int32 Next = (Index + Direction + TabNames.Num()) % TabNames.Num();
	SwitchTab(TabNames[Next]);
	SetFocusIndex(Next);
}

FString SATGPauseMenu::SelectionLabel() const {
	return NavigationLabels.IsValidIndex(NavigationIndex) ? NavigationLabels[NavigationIndex] : FString();
}

FString SATGPauseMenu::SelectionValue() const {
	if (!NavigationButtons.IsValidIndex(NavigationIndex)) return {};
	for (const TSharedPtr<SATGMenuRow>& Row : Rows) if (Row->Button() == NavigationButtons[NavigationIndex]) return Row->Value();
	return {};
}

// A controller's left / right on the row the player is on: the Settings rows step, and the rest of the panel
// leaves it to the tab buttons' own arrows.
bool SATGPauseMenu::IsSetting(const FString& Action) {
	return Action == TEXT("Quality") || Action == TEXT("Volume:0") || Action == TEXT("Volume:1")
		|| Action == TEXT("Sensitivity") || Action == TEXT("InvertY") || Action == TEXT("Crime") || Action == TEXT("TimeScale")
		|| Action == TEXT("Reflections") || Action == TEXT("AO") || Action == TEXT("ShowFPS");
}

void SATGPauseMenu::OnControllerAdjust(int32 Direction) {
	if (Direction && IsSetting(SelectedAction())) ApplySetting(SelectedAction(), Direction);
}

FReply SATGPauseMenu::ApplySetting(FString Action, int32 Direction) {
	atg::Game* G = GetSimulation();
	if (!G || !Direction) return FReply::Handled();
	const int32 Step = Direction < 0 ? -1 : 1;
	UATGGameInstance* GI = PlayerController ? Cast<UATGGameInstance>(PlayerController->GetGameInstance()) : nullptr;
	if (Action == TEXT("Quality")) {
		static const char* Choices[] = { "low", "medium", "high", "ultra" };
		int32 I = 2;
		for (int32 K = 0; K < 4; K++) if (G->settings.quality == Choices[K]) I = K;
		G->settings.quality = Choices[(I + Step + 4) % 4];
		G->quality = atg::QualityPreset(G->settings.quality);
		if (G->traffic) G->traffic->maxCars = G->quality.traffic;
		if (G->peds) G->peds->maxPeds = G->quality.peds;
		if (GI) GI->ApplyQuality(FName(UTF8_TO_TCHAR(G->settings.quality.c_str())));
	} else if (Action.StartsWith(TEXT("Volume:"))) {
		const bool bMaster = Action == TEXT("Volume:0");
		double& Value = bMaster ? G->settings.volume : G->settings.music;
		Value = FMath::Clamp(FMath::RoundToDouble((Value + Step * 0.05) * 100.0) / 100.0, 0.0, 1.0);
		if (G->audioSys) { if (bMaster) G->audioSys->setVolume(Value); else G->audioSys->setMusic(Value); }
		if (GI) { if (bMaster) GI->SetVolume(Value); else GI->SetMusicVolume(Value); }
	} else if (Action == TEXT("Sensitivity")) {
		G->settings.sensitivity = FMath::Clamp(FMath::RoundToDouble((G->settings.sensitivity + Step * 0.05) * 100.0) / 100.0, 0.2, 3.0);
		G->input.sensitivity = G->settings.sensitivity;
		if (GI) GI->SetSensitivity(G->settings.sensitivity);
	} else if (Action == TEXT("InvertY")) {
		G->settings.invertY = !G->settings.invertY;
		G->input.invertY = G->settings.invertY;
		if (GI) GI->SetInvertY(G->settings.invertY);
	} else if (Action == TEXT("Crime")) {
		static const char* Choices[] = { "off", "normal", "high" };
		int32 I = 1;
		for (int32 K = 0; K < 3; K++) if (G->settings.npcCrime == Choices[K]) I = K;
		G->settings.npcCrime = Choices[(I + Step + 3) % 3];
		if (GI) GI->SetNPCrimeLevel(FName(UTF8_TO_TCHAR(G->settings.npcCrime.c_str())));
	} else if (Action == TEXT("Reflections")) {
		if (GI) GI->SetReflectionsEnabled(!GI->IsReflectionsEnabled());
	} else if (Action == TEXT("AO")) {
		if (GI) GI->SetAOEnabled(!GI->IsAOEnabled());
	} else if (Action == TEXT("ShowFPS")) {
		if (GI) GI->SetShowFPS(!GI->IsShowFPS());
	} else if (Action == TEXT("TimeScale")) {
		// (the browser's list: 1 min / sec (default), 2 min / sec, Slow (0.5), Freeze time)
		static const double Choices[] = { 0, 0.5, 1, 2 };
		int32 I = Step > 0 ? -1 : 4;
		for (int32 K = 0; K < 4; K++) {
			if (Step > 0 && Choices[K] <= G->env.timeScale + 0.001) I = K;
			if (Step < 0 && Choices[K] >= G->env.timeScale - 0.001 && I == 4) I = K;
		}
		G->env.timeScale = Choices[(I + Step + 4) % 4];
		if (GI) GI->SetTimeScale(G->env.timeScale);
	}
	RefreshTab();
	return FReply::Handled();
}

FReply SATGPauseMenu::OnActionButtonClicked(FString Action) {
	atg::Game* G = GetSimulation();
	// the shell's own buttons
	if (Action == TEXT("Resume")) return OnResumeClicked();
	if (Action == TEXT("Save")) return OnSaveClicked();
	if (Action == TEXT("Quit")) return OnQuitClicked();
	if (!G) return FReply::Handled();
	if (IsSetting(Action)) return ApplySetting(Action, 1);

	// the Online tab
	if (Action.StartsWith(TEXT("Net")) && G->net) {
		atg::NetSystem& N = *G->net;
		UATGGameInstance* GI = PlayerController ? Cast<UATGGameInstance>(PlayerController->GetGameInstance()) : nullptr;
		if (Action == TEXT("NetName")) NetApplyName();
		else if (Action == TEXT("NetPublic")) NetGo(FString());
		else if (Action == TEXT("NetJoin")) {
			const FString C = NetCodeText.TrimStartAndEnd();
			if (!C.IsEmpty()) NetGo(C);
			else if (NetCodeBox.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(NetCodeBox, EFocusCause::SetDirectly);
		}
		else if (Action == TEXT("NetNew")) { const FString C = RandomRoomCode(); NetCodeText = C.ToUpper(); NetGo(C); }
		else if (Action == TEXT("NetLeave")) { N.disconnect(); RefreshOnline(); }
		else if (Action.StartsWith(TEXT("NetColor:"))) {
			N.color = FMath::Clamp(FCString::Atoi(*Action.RightChop(9)), 0, 7);
			if (GI) GI->SetNetSettings(Std(N.name), N.color, N.pvp);
			RefreshOnline();
		}
		else if (Action == TEXT("NetPvp")) {
			N.pvp = !N.pvp;
			if (GI) GI->SetNetSettings(Std(N.name), N.color, N.pvp);
			RefreshOnline();
		}
		else if (Action == TEXT("NetSend")) {
			if (!NetSayText.TrimStartAndEnd().IsEmpty()) { N.say(TCHAR_TO_UTF8(*NetSayText)); NetSayText.Reset(); if (NetSayBox.IsValid()) NetSayBox->SetText(FText::GetEmpty()); }
			RefreshOnline();
		}
		else if (Action.StartsWith(TEXT("NetGoto:")) || Action.StartsWith(TEXT("NetWp:"))) {
			const bool bGo = Action.StartsWith(TEXT("NetGoto:"));
			const std::string Id = TCHAR_TO_UTF8(*Action.RightChop(bGo ? 8 : 6));
			for (const atg::NetSystem::RosterRow& R : N.roster()) {
				if (R.id != Id && Id != "*") continue; // (* = the nearest: test scripts)
				if (bGo && G->freeroam && G->freeroamActive()) {
					// (hud.closeOverlay, then freeroam.teleport next to them, on foot)
					atg::FreeDestination D; D.name = R.name; D.x = R.x + 4; D.z = R.z + 4; D.foot = true;
					if (OnAction.IsBound()) OnAction.Execute(TEXT("Resume"));
					G->freeroam->teleport(D);
					return FReply::Handled();
				}
				if (!bGo && G->hud) { G->hud->setWaypoint(R.x, R.z); NetWaypointSet.Add(Std(R.id)); RefreshOnline(); }
				break;
			}
		}
		return FReply::Handled();
	}
	if (Action == TEXT("SpawnInto")) { bSpawnInto = !bSpawnInto; RefreshTab(); }
	else if (Action == TEXT("SpawnAir")) { bSpawnAir = !bSpawnAir; RefreshTab(); }
	else if (Action.StartsWith(TEXT("Paint:"))) { SpawnColor = FCString::Atoi64(*Action.RightChop(6)); RefreshTab(); }
	else if (Action.StartsWith(TEXT("Teleport:")) && G->freeroam) {
		const FString Name = Action.RightChop(9);
		for (const atg::FreeDestination& D : G->freeroam->destinations())
			if (Name == UTF8_TO_TCHAR(D.name.c_str())) { G->freeroam->teleport(D, true); if (OnAction.IsBound()) OnAction.Execute(TEXT("Resume")); break; }
	}
	else if (Action.StartsWith(TEXT("Wanted:")) && G->freeroam) { G->freeroam->setWanted(FCString::Atoi(*Action.RightChop(7))); RefreshTab(); }
	else if (Action.StartsWith(TEXT("Weather:")) && G->freeroam) { G->freeroam->setWeather(TCHAR_TO_UTF8(*Action.RightChop(8))); RefreshTab(); }
	else if (Action.StartsWith(TEXT("TimeOfDay:")) && G->freeroam) { G->freeroam->setTimeOfDay(FCString::Atod(*Action.RightChop(10))); RefreshTab(); }
	else if (Action.StartsWith(TEXT("Toggle:")) && G->freeroam) {
		const FString Toggle = Action.RightChop(7);
		G->freeroam->toggleCheat(TCHAR_TO_UTF8(*Toggle));
		RefreshTab();
	}
	// the vehicle spawns last: they live on the Vehicles tab
	else if (Action.StartsWith(TEXT("Spawn:"))) {
		const FString Id = Action.RightChop(6);
		const atg::VehicleDef* Def = atg::FindVehicle(TCHAR_TO_UTF8(*Id));
		if (Def && G->freeRoam) {
			const atg::V3 P = G->player->vehicle ? G->player->vehicle->pos : G->player->pos;
			atg::SpawnOpts Opts;
			Opts.persistent = true;
			if (SpawnColor >= 0) { Opts.hasColor = true; Opts.color = (uint32)SpawnColor; }
			if (Def->aircraft && bSpawnAir) { Opts.hasY = true; Opts.y = P.y + 80; }
			atg::Vehicle* V = G->vehicles.spawn(TCHAR_TO_UTF8(*Id), P.x + 7, P.z + 7, G->player->yaw, Opts);
			if (V) {
				if (G->player->vehicle) G->vehicles.exit(G->player.get());
				if (Def->aircraft && bSpawnAir) { V->airborne = true; if (auto* Air = dynamic_cast<atg::AirVehicle*>(V)) Air->grounded = false; }
				if (bSpawnInto) G->vehicles.enter(G->player.get(), V, 0, true);
			}
		}
		RefreshTab();
	}
	return FReply::Handled();
}

// ============================================================================ UATGMenuSystem
UATGMenuSystem::UATGMenuSystem() {}
void UATGMenuSystem::Initialize(AATGPlayerController* PC, UATGGameInstance* GI) { if (!MenuSystemImpl.IsValid()) MenuSystemImpl = MakeShared<FATGMenuSystem>(); MenuSystemImpl->Initialize(PC, GI); MenuSystemImpl->SetMulticastDelegates(&OnMainMenuClosed, &OnPauseMenuClosed, &OnMainMenuAction, &OnPauseMenuAction); }
void UATGMenuSystem::ShowMainMenu() { if (MenuSystemImpl.IsValid()) MenuSystemImpl->ShowMainMenu(); }
void UATGMenuSystem::HideMainMenu() { if (MenuSystemImpl.IsValid()) MenuSystemImpl->HideMainMenu(); }
void UATGMenuSystem::ShowPauseMenu(FName N) { if (MenuSystemImpl.IsValid()) MenuSystemImpl->ShowPauseMenu(N); }
void UATGMenuSystem::HidePauseMenu() { if (MenuSystemImpl.IsValid()) MenuSystemImpl->HidePauseMenu(); }
void UATGMenuSystem::SwitchPauseTab(FName N) { if (MenuSystemImpl.IsValid()) MenuSystemImpl->SwitchPauseTab(N); }
void UATGMenuSystem::MoveSelection(int32 X, int32 Y) { if (MenuSystemImpl.IsValid()) MenuSystemImpl->MoveSelection(X, Y); }
void UATGMenuSystem::ActivateSelection() { if (MenuSystemImpl.IsValid()) MenuSystemImpl->ActivateSelection(); }
void UATGMenuSystem::Back() { if (MenuSystemImpl.IsValid()) MenuSystemImpl->Back(); }
void UATGMenuSystem::CyclePauseTab(int32 D) { if (MenuSystemImpl.IsValid()) MenuSystemImpl->CyclePauseTab(D); }
void UATGMenuSystem::AdjustSelection(int32 D) { if (MenuSystemImpl.IsValid()) MenuSystemImpl->AdjustSelection(D); }
FString UATGMenuSystem::SelectionLabel() const { return MenuSystemImpl.IsValid() ? MenuSystemImpl->SelectionLabel() : FString(); }
bool UATGMenuSystem::SetMapWaypoint(float Fx, float Fy) { return MenuSystemImpl.IsValid() && MenuSystemImpl->SetMapWaypoint(Fx, Fy); }
bool UATGMenuSystem::PauseDo(const FString& A) { return MenuSystemImpl.IsValid() && MenuSystemImpl->PauseDo(A); }
bool UATGMenuSystem::PauseType(const FString& F, const FString& T) { return MenuSystemImpl.IsValid() && MenuSystemImpl->PauseType(F, T); }
FString UATGMenuSystem::SelectionValue() const { return MenuSystemImpl.IsValid() ? MenuSystemImpl->SelectionValue() : FString(); }
FATGMenuSystem::EATGMenuState UATGMenuSystem::GetMenuState() const { return MenuSystemImpl.IsValid() ? MenuSystemImpl->GetMenuState() : FATGMenuSystem::EATGMenuState::None; }
bool UATGMenuSystem::IsMenuOpen() const { return MenuSystemImpl.IsValid() && MenuSystemImpl->IsMenuOpen(); }
bool UATGMenuSystem::IsPauseMenuOpen() const { return MenuSystemImpl.IsValid() && MenuSystemImpl->IsPauseMenuOpen(); }
FName UATGMenuSystem::GetActivePauseTab() const { return MenuSystemImpl.IsValid() ? MenuSystemImpl->GetActivePauseTab() : NAME_None; }

#undef LOCTEXT_NAMESPACE
