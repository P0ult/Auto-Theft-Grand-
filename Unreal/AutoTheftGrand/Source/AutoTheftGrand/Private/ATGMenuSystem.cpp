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
#include "AutoTheftGrand.h"
#include "Gen/VehicleDefs.h"
#include "Sim/Aircraft.h"
#include "Sim/Audio.h"
#include "Sim/Env.h"
#include "Sim/FreeRoam.h"
#include "Sim/Game.h"
#include "Sim/Missions.h"
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
#include "Widgets/Images/SImage.h"
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
const FLinearColor Gold(0.93f, 0.69f, 0.30f, 1.f);
const FLinearColor GoldLight(1.f, 0.82f, 0.48f, 1.f);
const FLinearColor GoldDeep(0.55f, 0.34f, 0.10f, 1.f);
const FLinearColor MoneyGreen(0.34f, 0.78f, 0.49f, 1.f);
const FLinearColor TextDim(0.72f, 0.75f, 0.80f, 1.f);
const FLinearColor Muted(0.58f, 0.63f, 0.70f, 1.f);
const FLinearColor RowText(0.92f, 0.94f, 0.97f, 1.f);
const FLinearColor Hairline(0.70f, 0.76f, 0.84f, 0.20f);
const FLinearColor PanelFill(0.018f, 0.026f, 0.042f, 0.96f);
const FLinearColor RowFill(0.11f, 0.14f, 0.19f, 0.74f);
const FLinearColor RowHot(0.23f, 0.28f, 0.35f, 0.94f);
const FLinearColor TitleRowFill(0.035f, 0.045f, 0.065f, 0.50f);
const FLinearColor TitleRowHot(0.12f, 0.15f, 0.19f, 0.82f);

const FSlateBrush* White() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }

// `.menu-panel` (a translucent panel with a hairline border and 6 px corners)
const FSlateBrush* PanelBrush() {
	static const FSlateRoundedBoxBrush B(PanelFill, FVector4(8, 8, 8, 8), FLinearColor(0.72f, 0.79f, 0.88f, 0.24f), 1.f);
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
	return SNew(STextBlock).Text(Text).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13)).ColorAndOpacity(FSlateColor(Muted)).AutoWrapText(true);
}

// the gold key label a controls row is keyed by (`.controls td:first-child`)
TSharedRef<SWidget> KeyLabel(const FText& Text) {
	return SNew(STextBlock).Text(Text).Font(FCoreStyle::GetDefaultFontStyle("Bold", 18)).ColorAndOpacity(FSlateColor(Gold));
}

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
	FontSize = bTitle ? 30.f : 16.f;
	if (RowPadding == FMargin(0.f)) RowPadding = bTab ? FMargin(14.f, 8.f) : bTitle ? FMargin(18.f, 8.f) : FMargin(12.f, 9.f);

	ChildSlot[
		SAssignNew(InnerButton, SButton)
		.ButtonStyle(&FlatButton())
		.ContentPadding(FMargin(0.f))
		.ClickMethod(EButtonClickMethod::MouseDown)
		.IsFocusable(false) // (the menu drives the highlight; Slate's own navigation would double up with it)
		.OnClicked(FOnClicked::CreateSP(this, &SATGMenuRow::HandleClicked))
		.OnHovered(FSimpleDelegate::CreateSP(this, &SATGMenuRow::HandleHovered))
		.ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &SATGMenuRow::Background))
		[BuildContent()]
	];
}

FSlateColor SATGMenuRow::Background() const {
	const bool bHot = Highlight.IsValid() && *Highlight;
	if (bPrimary) return bHot ? FSlateColor(GoldLight) : FSlateColor(Gold);
	if (bTitle) return bHot ? FSlateColor(TitleRowHot) : FSlateColor(TitleRowFill);
	if (bTab) return FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	return bHot ? FSlateColor(RowHot) : FSlateColor(RowFill);
}

FSlateColor SATGMenuRow::TextColor() const {
	const bool bHot = Highlight.IsValid() && *Highlight;
	const bool bOn = Active.IsValid() && *Active;
	if (bPrimary) return FSlateColor(FLinearColor(0.09f, 0.07f, 0.04f, 1.f));
	if (bHot) return FSlateColor(GoldLight);
	if (bOn) return FSlateColor(FLinearColor::White);
	if (bTab) return FSlateColor(TextDim);
	return FSlateColor(RowText);
}

FSlateColor SATGMenuRow::StripColor() const {
	const bool bHot = Highlight.IsValid() && *Highlight;
	const bool bOn = Active.IsValid() && *Active;
	return (bHot || bOn) ? FSlateColor(Gold) : FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
}

TSharedRef<SWidget> SATGMenuRow::BuildContent() {
	TAttribute<FSlateColor> Text = TAttribute<FSlateColor>::CreateSP(this, &SATGMenuRow::TextColor);
	TAttribute<FSlateColor> Strip = TAttribute<FSlateColor>::CreateSP(this, &SATGMenuRow::StripColor);
	const bool bSwatch = Swatch.A > 0.f;

	TSharedRef<SHorizontalBox> Body = SNew(SHorizontalBox);
	if (bSwatch)
		Body->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(4.f, 0.f, 6.f, 0.f))[
			SNew(SBox).WidthOverride(26.f).HeightOverride(22.f)[SNew(SImage).Image(White()).ColorAndOpacity(Swatch)]];
	Body->AddSlot().FillWidth(1.f).VAlign(VAlign_Center).Padding(RowPadding)[
		SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Bold", FontSize)).ColorAndOpacity(Text).AutoWrapText(!bTab)];
	if (!Note.IsEmpty())
		Body->AddSlot().AutoWidth().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(FMargin(12.f, 0.f, 14.f, 0.f))[
			SNew(SBorder).BorderImage(PanelBrush()).Padding(FMargin(9.f, 5.f))[
				SNew(STextBlock).Text(Note).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				.ColorAndOpacity(FSlateColor(Note.ToString() == TEXT("OFF") ? TextDim : Note.ToString() == TEXT("ON") ? MoneyGreen : GoldLight))]];

	if (bTab) {
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center)[Body]
			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f)[SNew(SBox).HeightOverride(3.f)[SNew(SImage).Image(White()).ColorAndOpacity(Strip)]];
	}
	// `.title-menu button` / `.btn` / `.tp-btn`: a 4 px left border, transparent until the row is selected
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 5.f)[SNew(SBox).WidthOverride(3.f)[SNew(SImage).Image(White()).ColorAndOpacity(Strip)]]
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
		.Title(true)			.Padding(FMargin(22.f, 12.f));
	Row->Button()->SetEnabled(bEnabled);
	const int32 Index = NavigationButtons.Num();
	RegisterNavigationButton(Row->Button().ToSharedRef(), Action, ActionName);
	NavigationHighlight[Index] = Flag;
	if (Column.IsValid()) Column->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))[Row];
	TWeakPtr<SButton> Weak = Row->Button();
	Row->SetOnHovered(FSimpleDelegate::CreateLambda([this, Weak]() { if (Weak.IsValid()) FocusRow(Weak.Pin()); }));
	return Row;
}

TSharedRef<SWidget> SATGMainMenu::BuildMenu() {
	const FVector2D V = ViewportSize();
	const float L1 = FMath::Clamp((float)(V.X * 0.075), 46.f, 120.f);
	const float L2 = FMath::Clamp((float)(V.X * 0.095), 54.f, 160.f);
	const float L3 = FMath::Clamp((float)(V.X * 0.026), 22.f, 40.f);
	const float MenuWidth = FMath::Clamp((float)(V.X * 0.26), 300.f, 420.f);

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	AddRow(Column, LOCTEXT("Continue", "Continue"), FOnClicked::CreateSP(this, &SATGMainMenu::OnContinueClicked), TEXT("Continue"), bHasSave);
	AddRow(Column,
		TAttribute<FText>::CreateLambda([this]() { return FText::FromString(bNewGameArmed ? TEXT("New Game? Click again") : TEXT("New Game")); }),
		FOnClicked::CreateSP(this, &SATGMainMenu::OnNewGameClicked), TEXT("New Game"), true);
	AddRow(Column, LOCTEXT("FreeRoam", "Free Roam"), FOnClicked::CreateSP(this, &SATGMainMenu::OnFreeRoamClicked), TEXT("Free Roam"), true);
	AddRow(Column, LOCTEXT("Multiplayer", "Multiplayer"), FOnClicked::CreateSP(this, &SATGMainMenu::OnMultiplayerClicked), TEXT("Multiplayer"), true);
	AddRow(Column, LOCTEXT("Settings", "Settings"), FOnClicked::CreateSP(this, &SATGMainMenu::OnSettingsClicked), TEXT("Settings"), true);
	AddRow(Column, LOCTEXT("Controls", "Controls"), FOnClicked::CreateSP(this, &SATGMainMenu::OnControlsClicked), TEXT("Controls"), true);

	TSharedRef<SVerticalBox> TitleBlock = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)[
			SNew(STextBlock).Text(LOCTEXT("TitleEyebrow", "AN OPEN WORLD CRIME STORY")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			.ColorAndOpacity(FSlateColor(TextDim)).Justification(ETextJustify::Center)]
		+ SVerticalBox::Slot().AutoHeight()[
			SNew(STextBlock).Text(LOCTEXT("Title1", "AUTO THEFT")).Font(FCoreStyle::GetDefaultFontStyle("Bold", (int32)L1))
			.ColorAndOpacity(FSlateColor(FLinearColor::White)).Justification(ETextJustify::Center)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, -L2 * 0.12f, 0.f, 0.f)[
			SNew(STextBlock).Text(LOCTEXT("Title2", "GRAND")).Font(FCoreStyle::GetDefaultFontStyle("Bold", (int32)L2))
			.ColorAndOpacity(FSlateColor(Gold)).Justification(ETextJustify::Center)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[
			SNew(STextBlock).Text(LOCTEXT("Title3", "LOS SOLES  /  SAN AURELIO")).Font(FCoreStyle::GetDefaultFontStyle("Bold", (int32)L3))
			.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.839f, 0.627f))).Justification(ETextJustify::Center)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 15.f, 0.f, 0.f)[Hairline_()];

	return SNew(SOverlay)
		// Darken the flyover uniformly while leaving the title typography and navigation floating over the scene.
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0.015f, 0.025f, 0.05f, 0.56f))]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(30.f, 26.f, 30.f, 26.f))[TitleBlock]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(MenuWidth)[Column]]]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(24.f, 0.f, 24.f, 14.f))[
			SNew(STextBlock)
			.Text(LOCTEXT("Footer", "WASD / D-PAD TO NAVIGATE     ·     ENTER / A TO SELECT"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11)).ColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.82f))).Justification(ETextJustify::Center)];
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
	const float Width = FMath::Min(1180.f, V.X * 0.96f);
	const float Height = FMath::Max(240.f, FMath::Min(900.f, V.Y * 0.90f));
	return SNew(SBox).WidthOverride(Width).HeightOverride(Height)
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
		TabRow->AddSlot().FillWidth(1.f).Padding(FMargin(2.f, 0.f))[Row];
	}

	// .logo-small: AUTO THEFT in white with GRAND in gold
	TSharedRef<SHorizontalBox> Logo = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
			SNew(STextBlock).Text(LOCTEXT("LogoSmall", "AUTO THEFT "))		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26)).ColorAndOpacity(FSlateColor(FLinearColor::White))]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
			SNew(STextBlock).Text(LOCTEXT("LogoGrand", "GRAND")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 26)).ColorAndOpacity(FSlateColor(Gold))];

	// `.pause-foot`: Resume (primary), Save game and Quit to title
	auto FooterRow = [this](TAttribute<FText> Text, FString Action, bool bPrimary) {
		TSharedPtr<bool> Flag = MakeShared<bool>(false);
		TSharedRef<SATGMenuRow> Row = SNew(SATGMenuRow)
			.Label(Text).Primary(bPrimary).Highlight(Flag).Padding(FMargin(20.f, 9.f))
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
	TSharedRef<SATGMenuRow> Resume = FooterRow(LOCTEXT("Resume", "Resume"), TEXT("Resume"), true);
	TSharedRef<SATGMenuRow> Save = FooterRow(TAttribute<FText>::CreateLambda([this]() { return SaveText; }), TEXT("Save"), false);
	TSharedRef<SATGMenuRow> Quit = FooterRow(LOCTEXT("Quit", "Quit to title"), TEXT("Quit"), false);
	ResumeButton = Resume->Button(); SaveButton = Save->Button(); QuitButton = Quit->Button();
	atg::Game* G = GetSimulation();
	const bool bCanSave = G && !G->freeRoam && !(G->missions && G->missions->active);
	SaveButton->SetEnabled(bCanSave);
	if (!bCanSave) SaveText = LOCTEXT("SaveUnavailable", "Save unavailable");
	if (!bCanSave) SaveButton->SetToolTipText(G && G->freeRoam ? LOCTEXT("FreeSaveReason", "Free roam sessions are not saved.") : LOCTEXT("MissionSaveReason", "Finish the current mission before saving."));

	TSharedRef<SHorizontalBox> Foot = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 8.f, 0.f))[Resume]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(4.f, 0.f))[Save]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(8.f, 0.f, 0.f, 0.f))[Quit];

	SAssignNew(TabContentSwitcher, SWidgetSwitcher);
	TabBody = SAssignNew(TabScroll, SScrollBox)
		.Orientation(EOrientation::Orient_Vertical)
		.ScrollBarAlwaysVisible(true)
		+ SScrollBox::Slot()[TabContentSwitcher.ToSharedRef()];

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 28.f, 0.f))[Logo]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).HAlign(HAlign_Right)[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[
					SNew(STextBlock).Text(FText::FromString(bIsFreeRoam ? TEXT("FREE ROAM  /  PAUSED") : TEXT("STORY  /  PAUSED")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15)).ColorAndOpacity(FSlateColor(Gold))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f).HAlign(HAlign_Right)[
					SNew(STextBlock).Text(LOCTEXT("PauseHint", "LOS SOLES  /  SAN AURELIO"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)).ColorAndOpacity(FSlateColor(TextDim))]]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)[SNew(SBox).HeightOverride(52.f)[TabRow]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[Hairline_()]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 14.f)[TabBody.ToSharedRef()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[SNew(SBox).HeightOverride(62.f)[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[Hairline_()]
			+ SVerticalBox::Slot().FillHeight(1.f)[Foot]]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[
			SNew(STextBlock).Text(LOCTEXT("PauseNavigation", "D-PAD / LS  Navigate     A / ENTER  Select     B / MENU / ESC  Back     LB / RB  Tabs     LEFT / RIGHT  Adjust"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11)).ColorAndOpacity(FSlateColor(TextDim)).Justification(ETextJustify::Center)];

	// The viewport already applies Unreal's UI DPI curve. This menu measures in viewport pixels,
	// so cancel that curve to keep text readable and the panel inside the 720p safe area.
	const FVector2D V = ViewportSize();
	const float Scale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(V.X, V.Y));
	return SNew(SDPIScaler).DPIScale(1.f / FMath::Max(Scale, 0.1f))[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0.005f, 0.010f, 0.022f, 0.70f))]
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
		SNew(STextBlock).Text(Text).Font(FCoreStyle::GetDefaultFontStyle("Bold", 24)).ColorAndOpacity(FSlateColor(FLinearColor::White))];
	if (!Sub.IsEmpty())
		Column->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[
			SNew(STextBlock).Text(Sub).Font(FCoreStyle::GetDefaultFontStyle("Regular", 14)).ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.75f, 0.8f))).AutoWrapText(true)];
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
				SNew(STextBlock).Text(FText::FromString(R.Key)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 15)).ColorAndOpacity(FSlateColor(RowText))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(12.f, 5.f))[
				SNew(STextBlock).Text(FText::FromString(R.Value)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 20)).ColorAndOpacity(FSlateColor(Gold))]];
		Box->AddSlot().AutoHeight()[Hairline_(1.f)];
	}
	return Box;
}

// `.controls`: a heading, then key / action rows, in columns
TSharedRef<SWidget> SATGPauseMenu::MakeColumns(const TArray<FATGMenuGroup>& Groups) {
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).InnerSlotPadding(FVector2D(26.f, 14.f)).UseAllottedSize(true);
	for (const FATGMenuGroup& G : Groups) {
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		Col->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 6.f)[
			SNew(STextBlock).Text(FText::FromString(G.Name)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 22)).ColorAndOpacity(FSlateColor(FLinearColor::White))];
		for (const FATGMenuRowDef& R : G.Rows) {
			Col->AddSlot().AutoHeight()[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 3.f, 16.f, 3.f))[KeyLabel(FText::FromString(R.Label))]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(0.f, 3.f))[
					SNew(STextBlock).Text(FText::FromString(R.Action)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 14)).ColorAndOpacity(FSlateColor(RowText)).AutoWrapText(true)]];
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
			SNew(STextBlock).Text(FText::FromString(G.Name)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 22)).ColorAndOpacity(FSlateColor(Gold))];
		for (const FATGMenuRowDef& R : G.Rows)
			AddRow(Col, MakeRow(FText::FromString(R.Label), R.Action, false, FText::FromString(R.Note), true), 1.f);
		Wrap->AddSlot()[SNew(SBox).WidthOverride(330.f)[Col]];
	}
	return Wrap;
}

TSharedRef<SWidget> SATGPauseMenu::CreateMapTab() {
	atg::Game* G = GetSimulation();
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	const FVector2D V = ViewportSize();
	const float MapAspect = 7200.f / 6800.f; // WORLD bounds: retain the generated map's geographic proportions.
	const float MaxMapWidth = FMath::Min(1040.f, V.X * 0.88f);
	const float MaxMapHeight = FMath::Min(560.f, V.Y * 0.40f);
	const float MW = FMath::Min(MaxMapWidth, MaxMapHeight * MapAspect);
	const float MH = MW / MapAspect;

	Out->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[
		SNew(STextBlock)
		.Text(G ? FText::FromString(FString::Printf(TEXT("%s · $%.0f · %d/%d missions"),
			UTF8_TO_TCHAR(G->env.timeString().c_str()), G->player->money, G->missions ? (int32)G->missions->completed.size() : 0, G->missions ? (int32)G->missions->story.size() : 0))
			: LOCTEXT("Loading", "Loading the world..."))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18)).ColorAndOpacity(FSlateColor(GoldLight))];

	if (G && PlayerController) {
		if (AATGWorld* W = AATGWorld::Get(PlayerController)) {
			const FBox2D Rect = W->MapWorldRect;
			const FVector2D Size = Rect.GetSize();
			TSharedRef<SConstraintCanvas> Layers = SNew(SConstraintCanvas);
			if (W->MapWorldTex) {
				// Match the texture's pixel aspect ratio; treating this wide world map as square stretches the geography.
				static const FSlateImageBrush* MapBrush = nullptr;
				static const UTexture2D* MapTex = nullptr;
				if (MapTex != W->MapWorldTex) {
					delete MapBrush;
					const float TextureWidth = (float)W->MapWorldTex->GetSizeX();
					const float TextureHeight = (float)W->MapWorldTex->GetSizeY();
					MapBrush = new FSlateImageBrush(W->MapWorldTex, FVector2D(TextureWidth, TextureHeight));
					MapTex = W->MapWorldTex;
				}
				Layers->AddSlot()
					.Anchors(FAnchors(0.f, 0.f))
					.Offset(FMargin(0.f, 0.f, MW, MH))
					[SNew(SImage).Image(MapBrush)];
			}
			auto Placed = [&](double X, double Z) { return FVector2D(Size.X > 0 ? (float)((X - Rect.Min.X) / Size.X) * MW : 0.f, Size.Y > 0 ? (float)((Z - Rect.Min.Y) / Size.Y) * MH : 0.f); };
			for (const auto& Bl : G->blips) {
				const uint32 Color = Bl->color ? Bl->color : 0xffd23f;
				const FVector2D P = Placed(Bl->x, Bl->z);
				Layers->AddSlot().Anchors(FAnchors(0.f, 0.f)).Offset(FMargin(P.X - 5.f, P.Y - 5.f, 10.f, 10.f))[
					SNew(SImage).Image(White()).ColorAndOpacity(HexColor(Color))];
			}
			const atg::V3 P0 = G->player->vehicle ? G->player->vehicle->pos : G->player->pos;
			const FVector2D P = Placed(P0.x, P0.z);
			Layers->AddSlot().Anchors(FAnchors(0.f, 0.f)).Offset(FMargin(P.X - 8.f, P.Y - 8.f, 16.f, 16.f))[
				SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0.85f, 0.15f, 0.15f))];
			Out->AddSlot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(MW).HeightOverride(MH)[Layers]];
		}
	}
	// the legend under the map (hud.js _tab_map)
	Out->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[
		SNew(STextBlock)
		.Text(LOCTEXT("MapLegend", "Mission · Gun Barn · Spray Shack · Safehouse · Food · Waypoint. In free roam, the Teleport tab (T) fast travels; set a waypoint on the radar in the browser game."))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13)).ColorAndOpacity(FSlateColor(TextDim)).AutoWrapText(true)];
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

TSharedRef<SWidget> SATGPauseMenu::CreateOnlineTab() {
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	AddHeader(Out, LOCTEXT("OnlineTitle", "Online"), FText::GetEmpty());
	AddMuted(Out, LOCTEXT("OnlineText", "Multiplayer is not in the Unreal port yet. In the browser game this tab holds the room code, the player list and the chat."));
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
				SNew(STextBlock).Text(FText::FromString(TEXT("• ") + Line)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 15)).ColorAndOpacity(FSlateColor(RowText)).AutoWrapText(true)];
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
	Out->AddSlot().AutoHeight()[MakeTable(Table)];
	return Out;
}

TSharedRef<SWidget> SATGPauseMenu::CreateSettingsTab() {
	atg::Game* G = GetSimulation();
	TSharedRef<SVerticalBox> Out = SNew(SVerticalBox);
	if (!G) return Out;
	AddMuted(Out, LOCTEXT("SetHint", "Left / right adjusts the selected value. A or left-click increases; right-click decreases. Toggles show their current ON / OFF state. Changes apply immediately."));
	TArray<FATGMenuRowDef> Defs;
	Defs.Add(FATGMenuRowDef(FString(TEXT("Graphics quality")), FString(TEXT("Quality")), FString(UTF8_TO_TCHAR(G->settings.quality.c_str()))));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Master volume")), FString(TEXT("Volume:0")), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(G->settings.volume * 100))));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Radio volume")), FString(TEXT("Volume:1")), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(G->settings.music * 100))));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Mouse sensitivity")), FString(TEXT("Sensitivity")), FString::Printf(TEXT("%.2f"), G->settings.sensitivity)));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Invert mouse Y")), FString(TEXT("InvertY")), G->settings.invertY ? FString(TEXT("ON")) : FString(TEXT("OFF"))));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Street crime")), FString(TEXT("Crime")), FString(UTF8_TO_TCHAR(G->settings.npcCrime.c_str()))));
	Defs.Add(FATGMenuRowDef(FString(TEXT("Day/night speed")), FString(TEXT("TimeScale")), FString::Printf(TEXT("%.1f min/sec"), G->env.timeScale)));
	AddHeader(Out, LOCTEXT("SettingsHeading", "Settings"), FText::GetEmpty());
	AddRows(Out, Defs);
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
	if (!G || G->freeRoam || (G->missions && G->missions->active)) return FReply::Handled();
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
		|| Action == TEXT("Sensitivity") || Action == TEXT("InvertY") || Action == TEXT("Crime") || Action == TEXT("TimeScale");
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
	} else if (Action == TEXT("TimeScale")) {
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
FString UATGMenuSystem::SelectionValue() const { return MenuSystemImpl.IsValid() ? MenuSystemImpl->SelectionValue() : FString(); }
FATGMenuSystem::EATGMenuState UATGMenuSystem::GetMenuState() const { return MenuSystemImpl.IsValid() ? MenuSystemImpl->GetMenuState() : FATGMenuSystem::EATGMenuState::None; }
bool UATGMenuSystem::IsMenuOpen() const { return MenuSystemImpl.IsValid() && MenuSystemImpl->IsMenuOpen(); }
bool UATGMenuSystem::IsPauseMenuOpen() const { return MenuSystemImpl.IsValid() && MenuSystemImpl->IsPauseMenuOpen(); }
FName UATGMenuSystem::GetActivePauseTab() const { return MenuSystemImpl.IsValid() ? MenuSystemImpl->GetActivePauseTab() : NAME_None; }

#undef LOCTEXT_NAMESPACE
