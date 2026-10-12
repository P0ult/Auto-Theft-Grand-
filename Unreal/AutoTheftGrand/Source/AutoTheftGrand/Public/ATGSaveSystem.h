// UObject wrapper for the atg::SaveSystem (port of src/game/save.js)
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ATGSaveSystem.generated.h"

namespace atg
{
	class SaveSystem;
	class Game;
}

UCLASS()
class AUTOTHEFTGRAND_API UATGSaveSystem : public UObject
{
	GENERATED_BODY()

public:
	UATGSaveSystem();

	void Initialize(atg::Game* InGame);
	void SetSavePath(const FString& InPath);

	UFUNCTION(BlueprintCallable, Category = "ATG Save")
	bool HasSave() const;

	UFUNCTION(BlueprintCallable, Category = "ATG Save")
	bool LoadGame();

	UFUNCTION(BlueprintCallable, Category = "ATG Save")
	bool SaveGame(bool bAutoSave = false);

	UFUNCTION(BlueprintCallable, Category = "ATG Save")
	void ClearSave();

private:
	atg::SaveSystem* LiveSaveImpl() const;
	FString SavePath;
};