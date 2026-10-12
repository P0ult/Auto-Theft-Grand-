#include "ATGSaveSystem.h"
#include "ATGGameInstance.h"
#include "Game/ATGWorld.h"
#include "Sim/SaveSystem.h"
#include "Sim/Game.h"
#include "HAL/FileManager.h"

UATGSaveSystem::UATGSaveSystem()
{
}

atg::SaveSystem* UATGSaveSystem::LiveSaveImpl() const
{
	// Resolve from the world's current simulation every time. RestartSimulation replaces that simulation,
	// so retaining a SaveSystem pointer here would leave a dangling pointer after Quit to title.
	const AATGWorld* W = AATGWorld::Get(this);
	atg::Game* G = W ? W->Game() : nullptr;
	atg::SaveSystem* S = G ? G->saveSys : nullptr;
	if (S && !SavePath.IsEmpty()) S->path = TCHAR_TO_UTF8(*SavePath);
	return S;
}

void UATGSaveSystem::Initialize(atg::Game* InGame)
{
	if (InGame && InGame->saveSys && !SavePath.IsEmpty())
	{
		InGame->saveSys->path = TCHAR_TO_UTF8(*SavePath);
	}
}

void UATGSaveSystem::SetSavePath(const FString& InPath)
{
	SavePath = InPath;
	if (atg::SaveSystem* S = LiveSaveImpl())
	{
		S->path = TCHAR_TO_UTF8(*SavePath);
	}
}

bool UATGSaveSystem::HasSave() const
{
	if (const atg::SaveSystem* S = LiveSaveImpl()) return S->hasSave();
	return !SavePath.IsEmpty() && IFileManager::Get().FileExists(*SavePath);
}

bool UATGSaveSystem::LoadGame()
{
	if (atg::SaveSystem* S = LiveSaveImpl())
	{
		return S->load();
	}
	return false;
}

bool UATGSaveSystem::SaveGame(bool bAutoSave)
{
	if (atg::SaveSystem* S = LiveSaveImpl())
	{
		return S->save(bAutoSave);
	}

	return false;
}

void UATGSaveSystem::ClearSave()
{
	if (atg::SaveSystem* S = LiveSaveImpl())
	{
		S->clear();
	}
	else if (!SavePath.IsEmpty())
	{
		IFileManager::Get().Delete(*SavePath, false, true);
	}
}
