// Game instance for Auto Theft Grand - stores persistent settings and save data
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ATGGameInstance.generated.h"

class UATGSaveSystem;
namespace atg { class Game; }

UCLASS(Config = Game)
class AUTOTHEFTGRAND_API UATGGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UATGGameInstance();

	virtual void Init() override;
	virtual void Shutdown() override;

	// Save system
	UFUNCTION(BlueprintCallable, Category = "ATG Save")
	bool HasSave() const;

	UFUNCTION(BlueprintCallable, Category = "ATG Save")
	bool LoadGame();

	UFUNCTION(BlueprintCallable, Category = "ATG Save")
	bool SaveGame(bool bAutoSave = false);

	UFUNCTION(BlueprintCallable, Category = "ATG Save")
	void ClearSave();
	void InitializeSaveSystem(atg::Game* Game, const FString& SavePath);

	// Free roam
	UFUNCTION(BlueprintCallable, Category = "ATG Free Roam")
	bool IsFreeRoamActive() const;

	UFUNCTION(BlueprintCallable, Category = "ATG Free Roam")
	void SetFreeRoamActive(bool bActive);

	// Settings
	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void ApplyQuality(FName Quality);

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetHUDStyle(FName Style);

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetVolume(float Volume);

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetMusicVolume(float Volume);

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetSensitivity(float Sensitivity);

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetInvertY(bool bInvert);

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetReflectionsEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetAOEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetShowFPS(bool bShow);

	bool IsReflectionsEnabled() const { return bReflectionsEnabled; }
	bool IsAOEnabled() const { return bAOEnabled; }
	bool IsShowFPS() const { return bShowFPS; }
	// the two render toggles as console variables (Lumen reflections; screen-space and Lumen short-range AO)
	void ApplyRenderToggles() const;

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetNPCrimeLevel(FName Level);

	UFUNCTION(BlueprintCallable, Category = "ATG Settings")
	void SetTimeScale(float Scale);

	float GetMasterVolume() const { return MasterVolume; }
	float GetMusicVolume() const { return MusicVolume; }
	FString GetQuality() const { return QualitySetting.ToString(); }
	float GetSensitivity() const { return MouseSensitivity; }
	bool GetInvertY() const { return bInvertMouseY; }
	FString GetNPCrimeLevel() const { return NPCrimeLevel.ToString(); }

protected:
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings") FName QualitySetting = TEXT("high");
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings", meta = (ClampMin = "0", ClampMax = "1")) float MasterVolume = 0.8f;
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings", meta = (ClampMin = "0", ClampMax = "1")) float MusicVolume = 0.6f;
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings", meta = (ClampMin = "0.2", ClampMax = "3")) float MouseSensitivity = 1.f;
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings") bool bInvertMouseY = false;
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings") FName NPCrimeLevel = TEXT("normal");
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings") FName HUDStyle = TEXT("modern");
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings") bool bReflectionsEnabled = true;
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings") bool bAOEnabled = true;
	UPROPERTY(Config, EditAnywhere, Category = "ATG Settings") bool bShowFPS = false;

	UPROPERTY()
	TObjectPtr<UATGSaveSystem> SaveSystem;

	UPROPERTY()
	bool bFreeRoamActive = false;
};