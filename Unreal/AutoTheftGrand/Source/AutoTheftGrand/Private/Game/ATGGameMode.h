// Starts the game: spawns the world (which generates everything and creates the simulation), then every
// frame feeds the simulation the player's input, advances it, and keeps an actor drawing each of its
// vehicles and people.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ATGMenuSystem.h"
#include "ATGGameMode.generated.h"

class AATGCar;
class AATGEffects;
class AATGPerson;
class AATGWorld;
class USpotLightComponent;

UCLASS()
class AATGGameMode : public AGameModeBase {
	GENERATED_BODY()
public:
	AATGGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float Dt) override;

	// fixed steps for tests (ATG.Step): the game advances only when asked
	bool bManual = false;
	void StepFrames(int32 Frames, double Dt);

	// Menu system
	void OnMainMenuAction(const FString& Action);
	void OnPauseMenuAction(const FString& Action);
	void ShowPauseMenu(FName Tab = NAME_None);
	void HidePauseMenu();
	bool IsMenuOpen() const { return MenuSystem && MenuSystem->IsMenuOpen(); }
	// the title screen (and the menus opened from it): main.js puts body.menu on, which hides the HUD
	bool IsOnTitle() const { return MenuSystem && bInMenu; }
	bool IsPauseMenuOpen() const { return MenuSystem && MenuSystem->IsPauseMenuOpen(); }
	void MenuNavigate(int32 X, int32 Y) { if (MenuSystem) MenuSystem->MoveSelection(X, Y); }
	void MenuActivate() { if (MenuSystem) MenuSystem->ActivateSelection(); }
	void MenuBack() { if (MenuSystem) MenuSystem->Back(); }
	void MenuCycleTab(int32 Direction) { if (MenuSystem) MenuSystem->CyclePauseTab(Direction); }
	void MenuAdjust(int32 Direction) { if (MenuSystem) MenuSystem->AdjustSelection(Direction); }
	UATGMenuSystem* GetMenuSystem() const { return MenuSystem; }

private:
	void TryInitializeMenuSystem();
	UPROPERTY(Transient) TObjectPtr<AATGWorld> World = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<AATGCar>> Cars;
	UPROPERTY(Transient) TArray<TObjectPtr<AATGPerson>> People;
	UPROPERTY(Transient) TObjectPtr<USpotLightComponent> Headlight;
	UPROPERTY(Transient) TObjectPtr<AATGEffects> Effects = nullptr;
	UPROPERTY(Transient) TObjectPtr<class AATGPoliceView> PoliceView = nullptr;
	UPROPERTY(Transient) TObjectPtr<class AATGPickups> PickupsView = nullptr;
	UPROPERTY(Transient) TObjectPtr<class AATGAudio> Audio = nullptr;
	UPROPERTY(Transient) TObjectPtr<class AATGAnimals> AnimalsView = nullptr;
	UPROPERTY(Transient) TObjectPtr<class AATGMissionView> MissionView = nullptr;
	bool bStarted = false;
	bool bInMenu = true;
	bool bAutomatedScript = false;
	// (-ATGMenu with a test script: keep the title screen and the menus, don't start a journey by itself)
	bool bMenuTest = false;
	FString PendingJourneyMode;
	bool bOpenOnlineTab = false; // (the title's Multiplayer: free roam, then the pause menu on its Online tab)
	UPROPERTY() TObjectPtr<class UATGMenuSystem> MenuSystem;
	// (a session has run in the current simulation: a new game starts a fresh one, as the browser game does by
	// reloading the page)
	bool bWorldUsed = false;
	void StartGame(const FString& JourneyMode);
	// destroy everything drawn from the simulation (the cars, the people and the view actors that hold its
	// pointers), so the simulation can be rebuilt underneath them
	void TearDownViews();
	// tear the views down and build the simulation again: a clean world, as reloading the page gives
	void ResetSession();
	void Frame(double Dt);
	void SyncViews(float Dt);
};
