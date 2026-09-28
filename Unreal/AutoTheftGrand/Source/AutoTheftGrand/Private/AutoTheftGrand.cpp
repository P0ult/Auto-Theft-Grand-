#include "AutoTheftGrand.h"
#include "Game/ATGMaterials.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogATG);

class FAutoTheftGrandModule : public FDefaultGameModuleImpl {
public:
	virtual void StartupModule() override {
#if WITH_EDITOR
		// the materials are generated (and saved as assets) the first time the editor runs
		FCoreDelegates::OnPostEngineInit.AddLambda([]() {
			if (GIsEditor && !IsRunningCommandlet()) ATGMaterials::EnsureAssets();
		});
#endif
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAutoTheftGrandModule, AutoTheftGrand, "AutoTheftGrand");
