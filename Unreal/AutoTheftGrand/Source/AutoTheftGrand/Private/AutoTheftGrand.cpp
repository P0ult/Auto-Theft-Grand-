#include "AutoTheftGrand.h"
#include "Game/ATGMaterials.h"
#include "Game/ATGMeshComponent.h"
#include "Game/ATGFonts.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogATG);

class FAutoTheftGrandModule : public FDefaultGameModuleImpl {
public:
	virtual void StartupModule() override {
#if WITH_EDITOR
		// the materials are generated (and saved as assets) the first time the editor runs
		FCoreDelegates::GetOnPostEngineInit().AddLambda([]() {
			if (GIsEditor && !IsRunningCommandlet()) ATGMaterials::EnsureAssets();
		});
#endif
	}
	virtual void ShutdownModule() override {
		// the meshes' last GPU buffers (the render thread has stopped; the ray tracing geometry manager still runs)
		ATGMeshGraveyard::Flush();
		ATGFonts::Shutdown();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAutoTheftGrandModule, AutoTheftGrand, "AutoTheftGrand");
