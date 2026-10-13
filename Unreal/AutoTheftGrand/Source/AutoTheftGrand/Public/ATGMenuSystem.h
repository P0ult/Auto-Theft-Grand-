// Main menu and pause menu system for Auto Theft Grand using Slate (C++ UI)
// Port of src/main.js (title screen) and src/ui/hud.js (pause menu), with the looks from css/style.css.
#pragma once

#include "CoreMinimal.h"
#include <string>
#include "Framework/Docking/TabManager.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Input/Reply.h"
#include "ATGMenuSystem.generated.h"

class UATGGameInstance;
class AATGPlayerController;
class AATGGameMode;
class SVerticalBox;
class SATGMainMenu;
class SATGPauseMenu;
namespace atg { class Game; }

DECLARE_DELEGATE_OneParam(FOnMenuAction, const FString&);
DECLARE_DELEGATE(FOnMenuClosed);

// Multicast versions for UObject/Blueprint binding
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMenuActionMulticast, const FString&);
DECLARE_MULTICAST_DELEGATE(FOnMenuClosedMulticast);


// A menu row: the browser's title-menu button, a pause tab, a .btn or a .tp-btn. One place decides what the
// highlight looks like, so the controller, the keyboard and the mouse all agree (css/style.css).
// The menu system itself (declared after the widget classes so the two widget accessors below use
// complete types; MSVC misparses a class with two members returning pointers to incomplete classes).
class AUTOTHEFTGRAND_API FATGMenuSystem : public TSharedFromThis<FATGMenuSystem>
{
public:
	FATGMenuSystem();
	~FATGMenuSystem();

	void Initialize(AATGPlayerController* InPlayerController, UATGGameInstance* InGameInstance);

	// Main menu
	void ShowMainMenu();
	void HideMainMenu();

	// Pause menu
	void ShowPauseMenu(FName InTab = NAME_None);
	void HidePauseMenu();
	void SwitchPauseTab(FName TabName);
	void MoveSelection(int32 X, int32 Y);
	void ActivateSelection();
	void Back();
	void CyclePauseTab(int32 Direction);
	void AdjustSelection(int32 Direction);
	FString SelectionLabel() const;
	FString SelectionValue() const;
	// a waypoint at a point of the open Map tab (fractions of its width and height); false when it isn't open
	bool SetMapWaypoint(float Fx, float Fy);
	// (test scripts: press a pause-menu action by its id, or type into one of the Online tab's boxes)
	bool PauseDo(const FString& Action);
	bool PauseType(const FString& Field, const FString& Text);
	bool IsPauseMenuOpen() const;
	bool IsReturningToMainMenu() const { return bPauseReturnsToMainMenu; }
	void ClearReturnToMainMenu() { bPauseReturnsToMainMenu = false; }
	void SetFreeRoamMode(bool bValue) { bIsFreeRoam = bValue; }

	// State

	bool IsMenuOpen() const;

	enum class EATGMenuState { None, MainMenu, PauseMenu };
	EATGMenuState GetMenuState() const;
	FName GetActivePauseTab() const;

	// Events (single-cast for Slate binding)
	FOnMenuClosed OnMainMenuClosed;
	FOnMenuClosed OnPauseMenuClosed;
	FOnMenuAction OnMainMenuAction;
	FOnMenuAction OnPauseMenuAction;

	// Multicast delegates for UObject binding
	FOnMenuClosedMulticast* OnMainMenuClosedMulticast = nullptr;
	FOnMenuClosedMulticast* OnPauseMenuClosedMulticast = nullptr;
	FOnMenuActionMulticast* OnMainMenuActionMulticast = nullptr;
	FOnMenuActionMulticast* OnPauseMenuActionMulticast = nullptr;

	void SetMulticastDelegates(FOnMenuClosedMulticast* InMainMenuClosed, FOnMenuClosedMulticast* InPauseMenuClosed,
		FOnMenuActionMulticast* InMainMenuAction, FOnMenuActionMulticast* InPauseMenuAction)
	{
		OnMainMenuClosedMulticast = InMainMenuClosed;
		OnPauseMenuClosedMulticast = InPauseMenuClosed;
		OnMainMenuActionMulticast = InMainMenuAction;
		OnPauseMenuActionMulticast = InPauseMenuAction;
	}

	// the simulation (null until the world is ready): the menus pause it, free the input and flag a full-screen
	// menu, as hud.js openPause / closeOverlay do (game.paused, game.menuOpen)
	atg::Game* Simulation() const;

protected:
	AATGPlayerController* PlayerController = nullptr;
	UATGGameInstance* GameInstance = nullptr;

	TSharedPtr<class SWidget> CurrentMenuWidget;
	TSharedPtr<class SWidget> CurrentViewportWidget;

	EATGMenuState MenuState = EATGMenuState::None;
	FName ActivePauseTab = NAME_None;
	bool bIsFreeRoam = false;
	bool bPauseReturnsToMainMenu = false;
	bool bPauseCameFromMainMenu = false;

private:
	// Which widget is up. (A static cast from the viewport widget is never null, so the two menus must be told
	// apart by the menu state, not by which pointer cast looks like it worked.)
	SATGMainMenu* MainWidget() const;
	SATGPauseMenu* PauseWidget() const;

	// Widget creators
	TSharedRef<class SWidget> CreateMainMenu();
	TSharedRef<class SWidget> CreatePauseMenu();

	void ShowMainMenuWidget();
	void ShowPauseMenuWidget(FName InTab);
	void SwitchPauseTabInternal(FName TabName);

	// Action handlers
	void OnMainMenuActionReceived(const FString& Action);
	void OnPauseMenuActionReceived(const FString& Action);
};

class SATGMenuRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SATGMenuRow)
		: _Swatch(0.f, 0.f, 0.f, 0.f), _Primary(false), _Tab(false), _Title(false), _List(false), _Padding(0.f), _FontPx(0.f) {}
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ARGUMENT(FText, Note)       // (a right-aligned second column, e.g. a vehicle's top speed)
		SLATE_ARGUMENT(FLinearColor, Swatch) // (a paint colour: the row shows a block of it)
		SLATE_ARGUMENT(FOnClicked, OnClicked)
		SLATE_ARGUMENT(FOnClicked, OnRightClicked)
		SLATE_ARGUMENT(TSharedPtr<bool>, Highlight)
		SLATE_ARGUMENT(TSharedPtr<bool>, Active) // (the tab being shown: its underline stays gold)
		SLATE_ARGUMENT(bool, Primary)     // (`.btn.primary`: a solid gold button)
		SLATE_ARGUMENT(bool, Tab)         // (a pause tab: a gold underline, not a gold left border)
		SLATE_ARGUMENT(bool, Title)       // (`.title-menu button`: big, left aligned, wider spacing)
		SLATE_ARGUMENT(bool, List)        // (`.tp-btn`: a small, left-aligned list button)
		SLATE_ARGUMENT(FMargin, Padding)
		SLATE_ARGUMENT(float, FontPx)     // (another text size: `.btn.small` is 12 px)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;

	TSharedPtr<SButton> Button() const { return InnerButton; }
	FString Value() const { return Note.ToString(); }
	TSharedPtr<bool> HighlightFlag() const { return Highlight; }
	void SetOnHovered(FSimpleDelegate In) { OnHovered = In; }
	// (the menu's own selection: the same look the mouse gives a hovered row)
	void SetHighlighted(bool b) const { if (Highlight.IsValid()) *Highlight = b; }

private:
	FOnClicked OnClicked;
	FOnClicked OnRightClicked;
	TSharedPtr<bool> Highlight;
	TSharedPtr<bool> Active;
	TSharedPtr<SButton> InnerButton;
	FSimpleDelegate OnHovered;
	bool bPrimary = false, bTab = false, bTitle = false, bList = false;
	TAttribute<FText> Label;
	FText Note;
	FLinearColor Swatch = FLinearColor(0.f, 0.f, 0.f, 0.f);
	float FontSize = 18.f;
	FMargin RowPadding;
	// what the row looks like when it is the one the controller is on
	FSlateColor Background() const;
	FSlateColor TextColor() const;
	FSlateColor StripColor() const;
	bool IsHot() const;   // (selected and enabled)
	float Fade() const;   // (a disabled row: opacity 0.35 / 0.4)
	FReply HandleClicked();
	void HandleHovered();
	TSharedRef<SWidget> BuildContent();
};

// Slate widget for main menu
class SATGMainMenu : public SCompoundWidget
{
	friend class FATGMenuSystem;
public:
	SLATE_BEGIN_ARGS(SATGMainMenu) {}
		SLATE_EVENT(FOnMenuAction, OnAction)
		SLATE_ARGUMENT(bool, bHasSave)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FOnMenuAction OnAction;
	bool bHasSave = false;

	TSharedRef<SWidget> BuildMenu();
	void Rebuild();
	FVector2D BuiltFor = FVector2D::ZeroVector; // (the viewport size the layout was worked out for)
	FReply OnContinueClicked();
	FReply OnNewGameClicked();
	FReply OnFreeRoamClicked();
	FReply OnMultiplayerClicked();
	FReply OnSettingsClicked();
	FReply OnControlsClicked();
	bool IsContinueEnabled() const;
	void SetFocusIndex(int32 Index);
	void FocusRow(TSharedPtr<SButton> RowButton);
	void FocusSelection();
	void MoveSelection(int32 X, int32 Y);
	void ActivateSelection();
	FString SelectionLabel() const;
	TArray<TSharedPtr<SButton>> NavigationButtons;
	TArray<FOnClicked> NavigationActions;
	TArray<FString> NavigationLabels;
	TArray<TSharedPtr<bool>> NavigationHighlight;
	int32 NavigationIndex = INDEX_NONE;
	// (New Game asks again before wiping a save: main.js)
	bool bNewGameArmed = false;
	double NewGameArmedAt = 0.0;
	void RegisterNavigationButton(TSharedRef<SButton> Button, FOnClicked Action, const FString& Label);
	TSharedPtr<SATGMenuRow> AddRow(TSharedPtr<class SVerticalBox> Column, TAttribute<FText> Label, FOnClicked Action, const FString& ActionName, bool bEnabled);
};

// One row of a pause tab: its label, what pressing it does (or the action string) and an optional right-hand
// note (a setting's value, a vehicle's top speed).
struct FATGMenuRowDef {
	FString Label, Action, Note;
	FATGMenuRowDef() = default;
	FATGMenuRowDef(const char* InLabel, const char* InAction, const char* InNote = "") : Label(UTF8_TO_TCHAR(InLabel)), Action(UTF8_TO_TCHAR(InAction)), Note(UTF8_TO_TCHAR(InNote)) {}
	FATGMenuRowDef(const FString& InLabel, const FString& InAction, const FString& InNote = FString()) : Label(InLabel), Action(InAction), Note(InNote) {}
	FATGMenuRowDef(const std::string& InLabel, const std::string& InAction, const std::string& InNote = std::string()) : Label(UTF8_TO_TCHAR(InLabel.c_str())), Action(UTF8_TO_TCHAR(InAction.c_str())), Note(UTF8_TO_TCHAR(InNote.c_str())) {}
};
// a column of rows with a heading (`.tp-group`)
struct FATGMenuGroup {
	FString Name;
	TArray<FATGMenuRowDef> Rows;
	FATGMenuGroup() = default;
	FATGMenuGroup(const char* InName, TArray<FATGMenuRowDef> InRows) : Name(UTF8_TO_TCHAR(InName)), Rows(MoveTemp(InRows)) {}
};

// Slate widget for pause menu
class SATGPauseMenu : public SCompoundWidget
{
	friend class FATGMenuSystem;
public:
	SLATE_BEGIN_ARGS(SATGPauseMenu) {}
		SLATE_EVENT(FOnMenuAction, OnAction)
		SLATE_EVENT(FOnMenuClosed, OnClosed)
		SLATE_ARGUMENT(bool, bIsFreeRoam)
		SLATE_ARGUMENT(AATGPlayerController*, PlayerController)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void InitializeTabs(bool bInFreeRoam);
	void SwitchTab(FName TabName);
	void MoveSelection(int32 X, int32 Y);
	void ActivateSelection();
	void Back();
	void CycleTab(int32 Direction);

private:
	FOnMenuAction OnAction;
	FOnMenuClosed OnClosed;
	bool bIsFreeRoam = false;
	AATGPlayerController* PlayerController = nullptr;
	FName ActiveTab = NAME_None;
	// every button on screen, tabs first, then the tab's content, then the footer buttons
	TArray<TSharedPtr<SButton>> NavigationButtons;
	TArray<FOnClicked> NavigationActions;
	TArray<FString> NavigationLabels;
	TArray<TSharedPtr<bool>> NavigationHighlight;
	TArray<TSharedPtr<SButton>> TabNavigationButtons;
	TArray<FOnClicked> TabNavigationActions;
	TArray<FString> TabNavigationLabels;
	TArray<TSharedPtr<bool>> TabNavigationHighlight;
	TArray<TSharedPtr<SButton>> ContentNavigationButtons;
	TArray<FOnClicked> ContentNavigationActions;
	TArray<FString> ContentNavigationLabels;
	TArray<FString> ContentNavigationIds;
	TArray<FString> NavigationIds;
	TArray<TSharedPtr<bool>> ContentNavigationHighlight;
	TArray<TSharedPtr<SButton>> FooterNavigationButtons;
	TArray<FOnClicked> FooterNavigationActions;
	TArray<FString> FooterNavigationLabels;
	TArray<TSharedPtr<bool>> FooterNavigationHighlight;
	TArray<TSharedPtr<bool>> TabActiveFlags;
	TArray<TSharedPtr<SATGMenuRow>> Rows;
	FText SaveText;
	TArray<FName> TabNames;
	int32 NavigationIndex = INDEX_NONE;
	int32 NumTabButtons = 0;
	// the Vehicles tab's options (hud.js _spawnOpts: into, air, and the paint swatches)
	bool bSpawnInto = true;
	bool bSpawnAir = true;
	int64 SpawnColor = -1;
	FString VehMsg;   // (.vs-msg: why the spawner said no)
	FString AdminMsg; // (.adm-msg: what the last admin action said)
	FString CtlView, CtlGlyphs; // (the Controls tab: kb / pad, xbox / playstation)
	// (the selected row stays put when a tab is rebuilt after an action)
	void SetFocusIndex(int32 Index);
	void FocusRow(TSharedPtr<SButton> RowButton);
	void FocusSelection();
	void RefreshTab();
	FString SelectedAction() const;
	void RegisterFooterButton(TSharedRef<SButton> Button, FOnClicked Action, const FString& Label);
	void RegisterContentButton(TSharedRef<SButton> Button, FOnClicked Action, const FString& Label);
	void RegisterTabButton(TSharedRef<SButton> Button, FOnClicked Action, const FString& Label);
	atg::Game* GetSimulation() const;
	TSharedPtr<SATGMenuRow> MakeRow(TAttribute<FText> Label, const FString& Action, bool bPrimary = false, const FText& Note = FText::GetEmpty(), bool bList = false);
	// (keeps a row alive while the tab is up; AddRow also drops it into a column)
	TSharedPtr<SATGMenuRow> Track(TSharedPtr<SATGMenuRow> Row);
	void AddRow(TSharedPtr<class SVerticalBox> Column, TSharedPtr<SATGMenuRow> Row, float Padding = 3.f);
	void AddRows(TSharedPtr<class SVerticalBox> Column, const TArray<FATGMenuRowDef>& Defs);
	// a CSS-style bordered panel (`.menu-panel.pause`)
	TSharedRef<SWidget> MakePanel(TSharedRef<SWidget> Content);
	void AddHeader(TSharedPtr<class SVerticalBox> Column, const FText& Text, const FText& Sub);
	void AddMuted(TSharedPtr<class SVerticalBox> Column, const FText& Text);
	TSharedRef<SWidget> MakeTable(const TArray<TPair<FString, FString>>& TableRows);
	TSharedRef<SWidget> MakeColumns(const TArray<FATGMenuGroup>& Groups);
	TSharedRef<SWidget> MakeGroupGrid(const TArray<FATGMenuGroup>& Groups);
	TSharedRef<SWidget> BuildPauseMenu();
	TSharedRef<SWidget> BuildTabButtons();
	TSharedRef<SWidget> CreateTabContent(FName TabName);
	TSharedRef<SWidget> CreateMapTab();
	TSharedRef<SWidget> CreateTeleportTab();
	TSharedRef<SWidget> CreateVehiclesTab();
	TSharedRef<SWidget> CreateAdminTab();
	TSharedRef<SWidget> CreateOnlineTab();
	TSharedRef<SWidget> CreateBriefTab();
	TSharedRef<SWidget> CreateStatsTab();
	TSharedRef<SWidget> CreateSettingsTab();
	TSharedRef<SWidget> CreateControlsTab();

	FReply OnResumeClicked();
	FReply OnSaveClicked();
	FReply OnQuitClicked();
	FReply OnTabButtonClicked(FName TabName);
	FReply OnActionButtonClicked(FString Action);
	FReply ApplySetting(FString Action, int32 Direction);
	static bool IsSetting(const FString& Action);
	FString SelectionLabel() const;
	FString SelectionValue() const;
	void OnControllerAdjust(int32 Direction);
	TSharedPtr<class SWidgetSwitcher> TabContentSwitcher;
	TSharedPtr<class SScrollBox> TabScroll;
	TSharedPtr<SWidget> TabBody;
	TSharedPtr<SButton> ResumeButton;
	TSharedPtr<SButton> SaveButton;
	TSharedPtr<SButton> QuitButton;
	TSharedPtr<class STextBlock> SaveLabel;
	TSharedPtr<class SATGMapView> MapView;

	// the Online tab (netui.js): the text boxes keep what's typed across the tab's rebuilds
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	TSharedPtr<class SEditableTextBox> NetNameBox, NetCodeBox, NetSayBox;
	FString NetNameText, NetCodeText, NetSayText;
	bool bNetTextInit = false;
	FString NetSignature;
	float NetCheckT = 0.f;
	TSet<FString> NetWaypointSet;
	TSharedPtr<bool> RegisterNav(TSharedRef<SButton> Button, const FString& Action, const FString& Label);
	TSharedRef<SWidget> MakeTextBox(TSharedPtr<class SEditableTextBox>& Out, FString& Value, const FText& Hint, int32 MaxLen, bool bCode, const FString& EnterAction);
	FString OnlineSignature() const;
	void RefreshOnline();
	void NetApplyName();
	void NetGo(const FString& Code);
public:
	FReply DoAction(const FString& Action) { return OnActionButtonClicked(Action); }
	bool TypeInto(const FString& Field, const FString& Text);
};



UCLASS(Blueprintable, BlueprintType)
class AUTOTHEFTGRAND_API UATGMenuSystem : public UObject
{
	GENERATED_BODY()

public:
	UATGMenuSystem();

	void Initialize(AATGPlayerController* InPlayerController, UATGGameInstance* InGameInstance);

	void ShowMainMenu();
	void HideMainMenu();
	void ShowPauseMenu(FName InTab = NAME_None);
	void HidePauseMenu();
	void SwitchPauseTab(FName TabName);
	void MoveSelection(int32 X, int32 Y);
	void ActivateSelection();
	void Back();
	void CyclePauseTab(int32 Direction);
	void AdjustSelection(int32 Direction);
	FString SelectionLabel() const;
	FString SelectionValue() const;
	bool SetMapWaypoint(float Fx, float Fy);
	// (test scripts: press a pause-menu action by its id, or type into one of the Online tab's boxes)
	bool PauseDo(const FString& Action);
	bool PauseType(const FString& Field, const FString& Text);
	FATGMenuSystem::EATGMenuState GetMenuState() const;
	bool IsPauseMenuOpen() const;
	bool IsMenuOpen() const;
	FName GetActivePauseTab() const;

	FOnMenuClosedMulticast OnMainMenuClosed;
	FOnMenuClosedMulticast OnPauseMenuClosed;
	FOnMenuActionMulticast OnMainMenuAction;
	FOnMenuActionMulticast OnPauseMenuAction;

protected:
	AATGPlayerController* PlayerController = nullptr;
	UATGGameInstance* GameInstance = nullptr;

	TSharedPtr<FATGMenuSystem> MenuSystemImpl;
};
